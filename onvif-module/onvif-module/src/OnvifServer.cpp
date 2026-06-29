#include "OnvifServer.h"
#include "services/Media2Service.h"
#include "soapH.h"
#include <iostream>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

extern struct Namespace namespaces[];

// ── Custom header handler ─────────────────────────────────────────────────────
// gSOAP tự động fault khi nhận header có mustUnderstand="1" mà nó không
// nhận ra. ONVIF tool luôn gửi wsse:Security với mustUnderstand="1".
// Callback này chấp nhận header đó để code của chúng ta xử lý sau.
static int acceptMustUnderstandHeaders(struct soap* soap) {
    soap->mustUnderstand = 0; // Reset mustUnderstand flag whenever any header is parsed
    return SOAP_OK; // Mark all headers as "understood"
}

static size_t (*original_frecv)(struct soap*, char*, size_t) = nullptr;
static int (*original_fsend)(struct soap*, const char*, size_t) = nullptr;

static size_t custom_frecv(struct soap *soap, char *buf, size_t len) {
    size_t n = original_frecv ? original_frecv(soap, buf, len) : 0;
    if (n > 0) {
        std::string data(buf, n);
        std::cout << "[DEBUG RECV] " << data << std::endl;
    }
    return n;
}

static int custom_fsend(struct soap *soap, const char *buf, size_t len) {
    std::string data(buf, len);
    std::cout << "[DEBUG SEND] " << data << std::endl;
    return original_fsend ? original_fsend(soap, buf, len) : -1;
}

OnvifServer::OnvifServer(const ServiceConfig& cfg, std::shared_ptr<ICameraBackend> backend)
    : cfg_(cfg), backend_(std::move(backend)) {}

OnvifServer::~OnvifServer() {
    stop();
}

bool OnvifServer::start() {
    if (running_) return true;
    running_ = true;
    serverThread_ = std::thread(&OnvifServer::listenLoop, this);
    return true;
}

void OnvifServer::stop() {
    if (masterSocket_ >= 0) {
#ifdef _WIN32
        closesocket(masterSocket_);
#else
        close(masterSocket_);
#endif
        masterSocket_ = -1;
    }

    running_ = false;

    if (serverThread_.joinable()) {
        serverThread_.join();
    }
}

#include <vector>

// Hàm tạo bảng namespace chứa cả "ter"
static struct Namespace* get_custom_namespaces() {
    static std::vector<struct Namespace> custom_ns;
    if (custom_ns.empty()) {
        // Copy từ bảng namespaces tĩnh extern
        for (int i = 0; namespaces[i].id != nullptr || namespaces[i].ns != nullptr; ++i) {
            custom_ns.push_back(namespaces[i]);
        }
        // Thêm namespace "ter" của ONVIF error
        struct Namespace ter_ns = {"ter", "http://www.onvif.org/ver10/error", nullptr, nullptr};
        custom_ns.push_back(ter_ns);
        // Thêm phần tử đánh dấu kết thúc bảng (sentinel)
        struct Namespace end_ns = {nullptr, nullptr, nullptr, nullptr};
        custom_ns.push_back(end_ns);
    }
    return custom_ns.data();
}

void OnvifServer::listenLoop() {
    struct soap* soap = soap_new();
    if (!soap) {
        std::cerr << "[OnvifServer] Failed to create soap context!" << std::endl;
        running_ = false;
        return;
    }

    // Thiết lập hooks cho debug
    original_frecv = soap->frecv;
    soap->frecv = custom_frecv;
    original_fsend = soap->fsend;
    soap->fsend = custom_fsend;

    // Gán bảng ánh xạ namespace tùy biến có chứa "ter"
    soap->namespaces = get_custom_namespaces();
    std::cout << "[OnvifServer] Active namespaces:" << std::endl;
    for (int i = 0; soap->namespaces[i].id != nullptr; ++i) {
        std::cout << "  " << soap->namespaces[i].id << " -> " 
                  << (soap->namespaces[i].ns ? soap->namespaces[i].ns : "NULL") << std::endl;
    }

    // Chấp nhận wsse:Security header (mustUnderstand="1") không bị fault
    soap->fheader = acceptMustUnderstandHeaders;

    // Cấu hình timeouts và giải phóng địa chỉ port nhanh (REUSEADDR)
    soap->accept_timeout = 1; // 1 giây check running_ một lần
    soap->recv_timeout = 10;
    soap->send_timeout = 10;
    soap->bind_flags = SO_REUSEADDR;

    masterSocket_ = soap_bind(soap, nullptr, cfg_.httpPort, 100);
    if (masterSocket_ < 0) {
        std::cerr << "[OnvifServer] soap_bind failed on port " << cfg_.httpPort << std::endl;
        soap_print_fault(soap, stderr);
        soap_free(soap);
        running_ = false;
        return;
    }

    std::cout << "[OnvifServer] Listening on port " << cfg_.httpPort << std::endl;

    while (running_) {
        int clientSocket = soap_accept(soap);
        if (clientSocket < 0) {
            if (soap->errnum == 0) continue; // Hết timeout, lặp lại
            if (!running_) break;            // Đã dừng
            std::cerr << "[OnvifServer] soap_accept failed" << std::endl;
            break;
        }

        // ── Khởi tạo các Service trước để tránh constructor reset state của soap context ────────────────
        DeviceService deviceSvc(soap, cfg_, backend_);
        Media2Service media2Svc(soap, cfg_, backend_);

        // Thiết lập cấu hình lại cho soap context sau khi bị Service constructors ghi đè/reset
        soap->namespaces = get_custom_namespaces();
        soap->fheader = acceptMustUnderstandHeaders;
        soap->mustUnderstand = 0;

        if (deviceSvc.soap) {
            deviceSvc.soap->namespaces = get_custom_namespaces();
            deviceSvc.soap->fheader = acceptMustUnderstandHeaders;
            deviceSvc.soap->mustUnderstand = 0;
            deviceSvc.soap->frecv = custom_frecv;
            deviceSvc.soap->fsend = custom_fsend;
        }
        if (media2Svc.soap) {
            media2Svc.soap->namespaces = get_custom_namespaces();
            media2Svc.soap->fheader = acceptMustUnderstandHeaders;
            media2Svc.soap->mustUnderstand = 0;
            media2Svc.soap->frecv = custom_frecv;
            media2Svc.soap->fsend = custom_fsend;
        }

#include "auth/DigestAuthHandler.h"

static bool isAuthRequired(const char* http_headers) {
    if (!http_headers) return true;
    std::string headers(http_headers);
    
    // Các API được phép bypass xác thực theo đặc tả ONVIF
    if (headers.find("GetSystemDateAndTime") != std::string::npos ||
        headers.find("GetServices") != std::string::npos ||
        headers.find("GetServiceCapabilities") != std::string::npos ||
        headers.find("GetCapabilities") != std::string::npos) {
        return false;
    }
    return true;
}

        // ── Dispatch dựa trên URL path ────────────────────────────────
        int serveResult = SOAP_OK;
        if (soap_begin_serve(soap) == SOAP_OK) {
            // Lấy path từ HTTP request (soap->path được gSOAP set sau soap_begin_serve)
            const char* rawPath = soap->path ? soap->path : "";
            std::string path(rawPath);

            // Bỏ qua lỗi MustUnderstand check
            soap->mustUnderstand = 0;
            if (deviceSvc.soap) deviceSvc.soap->mustUnderstand = 0;
            if (media2Svc.soap) media2Svc.soap->mustUnderstand = 0;

            // Kiểm tra xác thực HTTP Digest
            if (isAuthRequired(soap->http_headers)) {
                DigestAuthHandler digestAuth(cfg_.username, cfg_.password);
                std::string method = "POST";
                
                if (!digestAuth.validate(soap->http_headers ? soap->http_headers : "", method)) {
                    static std::string challenge;
                    challenge = digestAuth.generateChallenge();
                    
                    soap->http_extra_header = challenge.c_str();
                    soap->error = 401;
                    soap_send_empty_response(soap, 401, 0);
                    
                    soap_destroy(soap);
                    soap_end(soap);
                    continue;
                }
            }

#include "services/MockSubscriptionManager.h"

            if (path.find("/onvif/event") != std::string::npos) {
                std::string headers = soap->http_headers ? soap->http_headers : "";
                std::string responseXml = "";
                
                std::string subId = "";
                size_t subPos = path.find("/onvif/event/");
                if (subPos != std::string::npos) {
                    subId = path.substr(subPos + 13);
                }

                auto& manager = MockSubscriptionManager::getInstance();

                if (headers.find("CreatePullPointSubscription") != std::string::npos) {
                    responseXml = manager.handleCreateSubscription(cfg_.deviceIp, cfg_.httpPort, headers);
                } else if (headers.find("PullMessages") != std::string::npos) {
                    responseXml = manager.handlePullMessages(subId, headers);
                } else if (headers.find("Renew") != std::string::npos) {
                    responseXml = manager.handleRenew(subId, headers);
                } else if (headers.find("Unsubscribe") != std::string::npos) {
                    responseXml = manager.handleUnsubscribe(subId, headers);
                }

                if (!responseXml.empty()) {
                    soap->error = SOAP_OK;
                    soap_response(soap, SOAP_OK);
                    soap_send_raw(soap, responseXml.c_str(), responseXml.size());
                    soap_end_send(soap);
                    
                    soap_destroy(soap);
                    soap_end(soap);
                    continue;
                }
            }

            if (path.find("/onvif/media") != std::string::npos) {
                // Yêu cầu đến Media2Service
                serveResult = media2Svc.dispatch();
                soap->error = media2Svc.soap->error;
                soap->fault = media2Svc.soap->fault;
            } else {
                // Mặc định: DeviceService (/onvif/device hoặc /onvif/device_service)
                serveResult = deviceSvc.dispatch();
                soap->error = deviceSvc.soap->error;
                soap->fault = deviceSvc.soap->fault;
            }
        } else {
            serveResult = soap->error;
        }

        if (serveResult != SOAP_OK && serveResult != SOAP_STOP) {
            std::cerr << "[OnvifServer] Error processing SOAP request:" << std::endl;
            soap_print_fault(soap, stderr);
            soap_send_fault(soap);
        }

        // Giải phóng vùng nhớ của phiên kết nối này
        soap_destroy(soap);
        soap_end(soap);
    }

    if (masterSocket_ >= 0) {
#ifdef _WIN32
        closesocket(masterSocket_);
#else
        close(masterSocket_);
#endif
        masterSocket_ = -1;
    }

    soap_free(soap);
    std::cout << "[OnvifServer] Server stopped." << std::endl;
}
