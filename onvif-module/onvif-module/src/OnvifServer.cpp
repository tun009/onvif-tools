#include "OnvifServer.h"
#include "services/Media2Service.h"
#include "soapH.h"
#include "auth/DigestAuthHandler.h"
#include "services/MockSubscriptionManager.h"
#include <iostream>
#include <cstring>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#endif

extern struct Namespace namespaces[];

thread_local std::string g_current_headers;
thread_local bool g_http_digest_authenticated = false;

// ── Base64 Encoder / Decoder and RTSP-over-HTTP Tunnel Proxy ──────────────────
#include <map>
#include <mutex>
#include <algorithm>

static void setSocketBlocking(SOAP_SOCKET sock) {
#ifdef _WIN32
    u_long mode = 0;
    ioctlsocket(sock, FIONBIO, &mode);
#else
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags != -1) {
        fcntl(sock, F_SETFL, flags & ~O_NONBLOCK);
    }
#endif
}

static std::map<std::string, SOAP_SOCKET> g_get_sessions;
static std::mutex g_sessions_mutex;

static const std::string base64_chars = 
             "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
             "abcdefghijklmnopqrstuvwxyz"
             "0123456789+/";

static inline bool is_base64(unsigned char c) {
    return (isalnum(c) || (c == '+') || (c == '/'));
}

static std::string base64_encode(unsigned char const* bytes_to_encode, unsigned int in_len) {
    std::string ret;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    while (in_len--) {
        char_array_3[i++] = *(bytes_to_encode++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for(i = 0; (i < 4) ; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i) {
        for(j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (j = 0; (j < i + 1); j++)
            ret += base64_chars[char_array_4[j]];

        while((i++ < 3))
            ret += '=';
    }

    return ret;
}

static std::string base64_decode(std::string const& encoded_string) {
    std::string ret;
    static const int T[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
        52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
        -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
        15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
        -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
        41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
    };

    int val = 0, valb = -8;
    for (unsigned char c : encoded_string) {
        if (T[c] != -1) {
            val = (val << 6) + T[c];
            valb += 6;
            if (valb >= 0) {
                ret.push_back(char((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
    }
    return ret;
}


// Thread 1: Đọc Base64 từ POST socket, giải mã và gửi raw sang MediaMTX
static void decodeAndForward(SOAP_SOCKET postSock, SOAP_SOCKET mtxSock) {
    setSocketBlocking(postSock);
    setSocketBlocking(mtxSock);
    std::cout << "[Proxy-POST] Thread started. Sockets set to BLOCKING mode." << std::endl;
    char buf[8192];
    std::string headerAccumulator;
    size_t bodyStartPos = std::string::npos;
    
    // 1. Đọc và bỏ qua HTTP Header (kết thúc bởi \r\n\r\n)
    while (true) {
        int n = recv(postSock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            std::cout << "[Proxy-POST] Recv failed or closed in header reading, n=" << n 
                      << ", errno=" << errno << " (" << strerror(errno) << ")" << std::endl;
#ifdef _WIN32
            closesocket(postSock);
            closesocket(mtxSock);
#else
            close(postSock);
            close(mtxSock);
#endif
            return;
        }
        buf[n] = '\0';
        headerAccumulator.append(buf, n);
        
        size_t pos = headerAccumulator.find("\r\n\r\n");
        if (pos != std::string::npos) {
            bodyStartPos = pos + 4;
            std::cout << "[Proxy-POST] Found end of HTTP header. Total header bytes read: " << bodyStartPos << std::endl;
            break;
        }
        
        // Đề phòng header quá lớn bất thường
        if (headerAccumulator.length() > 16384) {
            std::cout << "[Proxy-POST] Header too large, aborting." << std::endl;
#ifdef _WIN32
            closesocket(postSock);
            closesocket(mtxSock);
#else
            close(postSock);
            close(mtxSock);
#endif
            return;
        }
    }
    
    // 2. Lấy phần body thừa đã lỡ đọc được ở bước 1 và giải mã/gửi đi ngay
    std::string accumulator;
    if (bodyStartPos != std::string::npos && bodyStartPos < headerAccumulator.length()) {
        std::string initialBody = headerAccumulator.substr(bodyStartPos);
        std::cout << "[Proxy-POST] Found leftover body in header packet, size=" << initialBody.size() << " bytes." << std::endl;
        for (char c : initialBody) {
            if (is_base64(c) || c == '=') {
                accumulator += c;
            }
        }
        
        size_t len = accumulator.length();
        size_t processLen = len - (len % 4);
        if (processLen > 0) {
            std::string toDecode = accumulator.substr(0, processLen);
            accumulator = accumulator.substr(processLen);
            
            std::string raw = base64_decode(toDecode);
            if (!raw.empty()) {
                std::cout << "[Proxy-POST] Decoded & sending leftover body raw size=" << raw.size() << " bytes to MediaMTX..." << std::endl;
                int sent = 0;
                int total = raw.size();
                while (sent < total) {
                    int w = send(mtxSock, raw.data() + sent, total - sent, 0);
                    if (w <= 0) {
                        std::cout << "[Proxy-POST] Send leftover to MediaMTX failed, w=" << w << std::endl;
                        break;
                    }
                    sent += w;
                }
            }
        }
    }
    
    // 3. Tiếp tục đọc body và giải mã Base64 sang raw gửi tới MediaMTX
    while (true) {
        int n = recv(postSock, buf, sizeof(buf), 0);
        if (n <= 0) {
            std::cout << "[Proxy-POST] Recv body finished or connection closed, n=" << n 
                      << ", errno=" << errno << " (" << strerror(errno) << ")" << std::endl;
            break;
        }
        std::cout << "[Proxy-POST] Recv " << n << " Base64 bytes from client..." << std::endl;
        
        for (int i = 0; i < n; ++i) {
            char c = buf[i];
            if (is_base64(c) || c == '=') {
                accumulator += c;
            }
        }
        
        size_t len = accumulator.length();
        size_t processLen = len - (len % 4);
        if (processLen > 0) {
            std::string toDecode = accumulator.substr(0, processLen);
            accumulator = accumulator.substr(processLen);
            
            std::string raw = base64_decode(toDecode);
            if (!raw.empty()) {
                std::cout << "[Proxy-POST] Decoded & sending raw size=" << raw.size() << " bytes to MediaMTX..." << std::endl;
                int sent = 0;
                int total = raw.size();
                while (sent < total) {
                    int w = send(mtxSock, raw.data() + sent, total - sent, 0);
                    if (w <= 0) {
                        std::cout << "[Proxy-POST] Send to MediaMTX failed, w=" << w << std::endl;
                        break;
                    }
                    sent += w;
                }
            }
        }
    }
    std::cout << "[Proxy-POST] Thread exiting." << std::endl;
#ifdef _WIN32
    closesocket(postSock);
    closesocket(mtxSock);
#else
    close(postSock);
    close(mtxSock);
#endif
}

// Thread 2: Đọc raw từ MediaMTX, mã hóa Base64 và gửi sang GET socket
static void encodeAndForward(SOAP_SOCKET mtxSock, SOAP_SOCKET getSock) {
    setSocketBlocking(mtxSock);
    setSocketBlocking(getSock);
    std::cout << "[Proxy-GET] Thread started. Sockets set to BLOCKING mode." << std::endl;
    unsigned char buf[4096];
    while (true) {
        int n = recv(mtxSock, (char*)buf, sizeof(buf), 0);
        if (n <= 0) {
            std::cout << "[Proxy-GET] Recv from MediaMTX finished or closed, n=" << n 
                      << ", errno=" << errno << " (" << strerror(errno) << ")" << std::endl;
            break;
        }
        std::cout << "[Proxy-GET] Recv " << n << " raw bytes from MediaMTX..." << std::endl;
        
        std::string b64 = base64_encode(buf, n);
        if (!b64.empty()) {
            std::cout << "[Proxy-GET] Encoded & sending " << b64.size() << " Base64 bytes to GET client..." << std::endl;
            int sent = 0;
            int total = b64.size();
            while (sent < total) {
                int w = send(getSock, b64.data() + sent, total - sent, 0);
                if (w <= 0) {
                    std::cout << "[Proxy-GET] Send to GET client failed, w=" << w << std::endl;
                    break;
                }
                sent += w;
            }
        }
    }
    std::cout << "[Proxy-GET] Thread exiting." << std::endl;
#ifdef _WIN32
    closesocket(mtxSock);
    closesocket(getSock);
#else
    close(mtxSock);
    close(getSock);
#endif
}

// ── Custom header handler ─────────────────────────────────────────────────────
// gSOAP tự động fault khi nhận header có mustUnderstand="1" mà nó không
// nhận ra. ONVIF tool luôn gửi wsse:Security với mustUnderstand="1".
// Callback này chấp nhận header đó để code của chúng ta xử lý sau.
static int acceptMustUnderstandHeaders(struct soap* soap) {
    soap->mustUnderstand = 0; // Reset mustUnderstand flag whenever any header is parsed
    return SOAP_OK; // Mark all headers as "understood"
}

static bool isAuthRequired(const std::string& headers) {
    // Các API được phép bypass xác thực theo đặc tả ONVIF
    if (headers.find("GetSystemDateAndTime") != std::string::npos ||
        headers.find("GetServices") != std::string::npos ||
        headers.find("GetServiceCapabilities") != std::string::npos ||
        headers.find("GetCapabilities") != std::string::npos) {
        return false;
    }
    return true;
}

static size_t (*original_frecv)(struct soap*, char*, size_t) = nullptr;
static int (*original_fsend)(struct soap*, const char*, size_t) = nullptr;

static size_t custom_frecv(struct soap *soap, char *buf, size_t len) {
    size_t n = original_frecv ? original_frecv(soap, buf, len) : 0;
    if (n > 0) {
        g_current_headers.append(buf, n);
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
        SOAP_SOCKET clientSocket = soap_accept(soap);
        if (clientSocket == SOAP_INVALID_SOCKET) {
            if (soap->errnum == 0) continue; // Hết timeout, lặp lại
            if (!running_) break;            // Đã dừng
            std::cerr << "[OnvifServer] soap_accept failed" << std::endl;
            break;
        }

        // Kiểm tra xem có phải là RTSP over HTTP tunnel request không
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000; // Đợi tối đa 100ms
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(clientSocket, &rfds);
        
        bool isRtspTunnel = false;
        std::string requestStr;
        if (select((int)clientSocket + 1, &rfds, nullptr, nullptr, &tv) > 0) {
            char peekBuf[1024];
            std::memset(peekBuf, 0, sizeof(peekBuf));
            int peekLen = recv(clientSocket, peekBuf, sizeof(peekBuf) - 1, MSG_PEEK);
            if (peekLen > 0) {
                requestStr.assign(peekBuf, peekLen);
                if (requestStr.find("x-rtsp-tunnelled") != std::string::npos ||
                    requestStr.find("application/x-rtsp-tunnelled") != std::string::npos ||
                    (requestStr.find("GET ") == 0 && requestStr.find("/onvif") == std::string::npos && requestStr.find("HTTP/") != std::string::npos) ||
                    (requestStr.find("POST ") == 0 && requestStr.find("/onvif") == std::string::npos && requestStr.find("HTTP/") != std::string::npos)) {
                    isRtspTunnel = true;
                }
            }
        }

        if (isRtspTunnel) {
            // Trích xuất x-sessioncookie từ request header
            std::string cookie;
            size_t cookiePos = requestStr.find("x-sessioncookie:");
            if (cookiePos == std::string::npos) {
                cookiePos = requestStr.find("X-SessionCookie:");
            }
            if (cookiePos != std::string::npos) {
                size_t start = cookiePos + 16;
                while (start < requestStr.length() && (requestStr[start] == ' ' || requestStr[start] == '\t')) {
                    start++;
                }
                size_t end = start;
                while (end < requestStr.length() && requestStr[end] != '\r' && requestStr[end] != '\n') {
                    end++;
                }
                cookie = requestStr.substr(start, end - start);
            }

            bool isGet = (requestStr.find("GET ") == 0);
            bool isPost = (requestStr.find("POST ") == 0);

            if (!cookie.empty()) {
                if (isGet) {
                    std::cout << "[OnvifServer] RTSP-over-HTTP GET tunnel request. Cookie=" << cookie << std::endl;
                    
                    // Trả về HTTP 200 OK Content-Type: application/x-rtsp-tunnelled (Keep connection open)
                    std::string resp = "HTTP/1.1 200 OK\r\n"
                                       "Server: MockONVIF\r\n"
                                       "Cache-Control: no-store\r\n"
                                       "Pragma: no-cache\r\n"
                                       "Content-Type: application/x-rtsp-tunnelled\r\n\r\n";
                    send(clientSocket, resp.data(), resp.size(), 0);

                    // Lưu GET socket vào session map
                    {
                        std::lock_guard<std::mutex> lock(g_sessions_mutex);
                        g_get_sessions[cookie] = clientSocket;
                    }
                    
                    // Giải phóng khỏi gSOAP mà không đóng socket
                    soap->socket = SOAP_INVALID_SOCKET;
                    soap_destroy(soap);
                    soap_end(soap);
                    continue;
                }
                else if (isPost) {
                    std::cout << "[OnvifServer] RTSP-over-HTTP POST tunnel request. Cookie=" << cookie << std::endl;
                    
                    SOAP_SOCKET getSock = SOAP_INVALID_SOCKET;
                    {
                        std::lock_guard<std::mutex> lock(g_sessions_mutex);
                        auto it = g_get_sessions.find(cookie);
                        if (it != g_get_sessions.end()) {
                            getSock = it->second;
                            g_get_sessions.erase(it);
                        }
                    }

                    if (getSock != SOAP_INVALID_SOCKET) {
                        // Kết nối tới MediaMTX (localhost:8554)
                        SOAP_SOCKET mtxSocket = socket(AF_INET, SOCK_STREAM, 0);
                        bool connected = false;
                        if (mtxSocket != SOAP_INVALID_SOCKET) {
                            struct sockaddr_in addr;
                            std::memset(&addr, 0, sizeof(addr));
                            addr.sin_family = AF_INET;
                            addr.sin_port = htons(cfg_.rtspPort);
                            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

                            if (connect(mtxSocket, (struct sockaddr*)&addr, sizeof(addr)) >= 0) {
                                connected = true;
                            }
                        }

                        if (connected) {
                            // Trả về HTTP 200 OK cho POST request (Keep connection open)
                            std::string resp = "HTTP/1.1 200 OK\r\n"
                                               "Server: MockONVIF\r\n"
                                               "Connection: keep-alive\r\n"
                                               "Content-Type: application/x-rtsp-tunnelled\r\n\r\n";
                            send(clientSocket, resp.data(), resp.size(), 0);

                            std::cout << "[OnvifServer] Successfully paired GET & POST sockets. Starting Base64 Proxy to MediaMTX on port " << cfg_.rtspPort << "..." << std::endl;
                            // Chạy 2 thread chuyển tiếp Base64 <-> Raw
                            std::thread(decodeAndForward, clientSocket, mtxSocket).detach();
                            std::thread(encodeAndForward, mtxSocket, getSock).detach();
                            
                            // Cả 2 socket đã giao cho thread quản lý
                            soap->socket = SOAP_INVALID_SOCKET;
                            soap_destroy(soap);
                            soap_end(soap);
                            continue;
                        } else {
                            std::cerr << "[OnvifServer] Failed to connect to MediaMTX on port " << cfg_.rtspPort << std::endl;
                            if (mtxSocket != SOAP_INVALID_SOCKET) {
#ifdef _WIN32
                                closesocket(mtxSocket);
#else
                                close(mtxSocket);
#endif
                            }
                        }
                    } else {
                        std::cerr << "[OnvifServer] Received POST tunnel but no matching GET tunnel found for cookie=" << cookie << std::endl;
                    }
                }
            }

            // Nếu cookie trống hoặc xử lý lỗi, đóng client socket
#ifdef _WIN32
            closesocket(clientSocket);
#else
            close(clientSocket);
#endif
            soap->socket = SOAP_INVALID_SOCKET;
            soap_destroy(soap);
            soap_end(soap);
            continue;
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
        // ── Dispatch dựa trên URL path ────────────────────────────────
        g_current_headers.clear();
        g_http_digest_authenticated = false;
        int serveResult = SOAP_OK;
        if (soap_begin_serve(soap) == SOAP_OK) {
            // Lấy path từ HTTP request (soap->path được gSOAP set sau soap_begin_serve)
            const char* rawPath = soap->path;
            std::string path(rawPath);

            // Bỏ qua lỗi MustUnderstand check
            soap->mustUnderstand = 0;
            if (deviceSvc.soap) deviceSvc.soap->mustUnderstand = 0;
            if (media2Svc.soap) media2Svc.soap->mustUnderstand = 0;

            // Kiểm tra xác thực HTTP Digest
            if (isAuthRequired(g_current_headers)) {
                DigestAuthHandler digestAuth(cfg_.username, cfg_.password);
                std::string method = "POST";
                
                if (!digestAuth.validate(g_current_headers, method)) {
                    static std::string challenge;
                    challenge = digestAuth.generateChallenge();
                    
                    soap->http_extra_header = challenge.c_str();
                    soap->error = 401;
                    soap_send_empty_response(soap, 401);
                    
                    soap_destroy(soap);
                    soap_end(soap);
                    continue;
                } else {
                    g_http_digest_authenticated = true;
                }
            } else {
                g_http_digest_authenticated = true;
            }

            if (path.find("/onvif/event") != std::string::npos) {
                std::string headers = g_current_headers;
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
