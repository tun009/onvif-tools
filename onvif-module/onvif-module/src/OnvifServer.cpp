#include "OnvifServer.h"
#include "services/Media2Service.h"
#include "soapH.h"
#include <iostream>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

extern struct Namespace namespaces[];

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

void OnvifServer::listenLoop() {
    struct soap* soap = soap_new();
    if (!soap) {
        std::cerr << "[OnvifServer] Failed to create soap context!" << std::endl;
        running_ = false;
        return;
    }

    // Gán bảng ánh xạ namespace cho context
    soap->namespaces = namespaces;

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

        // ── Dispatch dựa trên URL path ────────────────────────────────
        // Lấy path từ HTTP request (soap->path được gSOAP set sau soap_accept)
        const char* rawPath = soap->path ? soap->path : "";
        std::string path(rawPath);

        int serveResult = SOAP_OK;
        if (path.find("/onvif/media") != std::string::npos) {
            // Yêu cầu đến Media2Service
            Media2Service media2Svc(soap, cfg_, backend_);
            serveResult = media2Svc.serve();
        } else {
            // Mặc định: DeviceService (/onvif/device hoặc /onvif/device_service)
            DeviceService deviceSvc(soap, cfg_, backend_);
            serveResult = deviceSvc.serve();
        }

        if (serveResult != SOAP_OK && serveResult != SOAP_STOP) {
            std::cerr << "[OnvifServer] Error processing SOAP request:" << std::endl;
            soap_print_fault(soap, stderr);
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
