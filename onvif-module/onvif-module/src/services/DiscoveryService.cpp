// DiscoveryService.cpp
// WS-Discovery (ONVIF) tự viết — UDP multicast 239.255.255.250:3702.
// WS-Discovery 2005/04 + WS-Addressing 2004/08.

#include "services/DiscoveryService.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <ctime>
#include <random>
#include <sstream>
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>

// Singleton pointer để DeviceService::SystemReboot triệu hồi.
static DiscoveryService* g_discoveryInstance = nullptr;

DiscoveryService* DiscoveryService::current() { return g_discoveryInstance; }

namespace {
constexpr const char* MCAST_ADDR = "239.255.255.250";
constexpr int         MCAST_PORT = 3702;

// Namespace URIs (ONVIF dùng discovery 2005/04 + addressing 2004/08)
constexpr const char* NS_SOAP = "http://www.w3.org/2003/05/soap-envelope";
constexpr const char* NS_WSA  = "http://schemas.xmlsoap.org/ws/2004/08/addressing";
constexpr const char* NS_WSD  = "http://schemas.xmlsoap.org/ws/2005/04/discovery";
constexpr const char* NS_DN   = "http://www.onvif.org/ver10/network/wsdl";
constexpr const char* NS_TDS  = "http://www.onvif.org/ver10/device/wsdl";

constexpr const char* ANON =
    "http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous";
constexpr const char* TO_DISCOVERY =
    "urn:schemas-xmlsoap-org:ws:2005:04:discovery";

constexpr const char* ACT_HELLO =
    "http://schemas.xmlsoap.org/ws/2005/04/discovery/Hello";
constexpr const char* ACT_BYE =
    "http://schemas.xmlsoap.org/ws/2005/04/discovery/Bye";
constexpr const char* ACT_PROBE_MATCHES =
    "http://schemas.xmlsoap.org/ws/2005/04/discovery/ProbeMatches";
constexpr const char* ACT_RESOLVE_MATCHES =
    "http://schemas.xmlsoap.org/ws/2005/04/discovery/ResolveMatches";
} // namespace

DiscoveryService::DiscoveryService(const ServiceConfig& cfg) : cfg_(cfg) {
    instanceId_ = static_cast<uint32_t>(::time(nullptr));
}

DiscoveryService::~DiscoveryService() {
    stop();
}

// ── Helpers ─────────────────────────────────────────────────────────────────

std::string DiscoveryService::xaddr() const {
    std::ostringstream os;
    os << "http://" << cfg_.deviceIp << ":" << cfg_.httpPort
       << "/onvif/device_service";
    return os.str();
}

std::string DiscoveryService::endpointRef() const {
    // deviceUuid dạng "12345678-..." → urn:uuid:...
    std::string u = cfg_.deviceUuid;
    if (u.rfind("urn:uuid:", 0) != 0) u = "urn:uuid:" + u;
    return u;
}

// Danh sách scope PHẢI khớp DeviceService::GetScopes (test kiểm tra consistency)
std::string DiscoveryService::scopesLine() const {
    static const std::vector<std::string> scopes = {
        "onvif://www.onvif.org/type/NetworkVideoTransmitter",
        "onvif://www.onvif.org/type/video_encoder",
        "onvif://www.onvif.org/name/MockCam-4K",
        "onvif://www.onvif.org/hardware/JetsonOrinNX-8GB",
        "onvif://www.onvif.org/Profile/Streaming",
        "onvif://www.onvif.org/Profile/T"
    };
    std::string out;
    for (size_t i = 0; i < scopes.size(); ++i) {
        if (i) out += " ";
        out += scopes[i];
    }
    return out;
}

std::string DiscoveryService::newMessageId() const {
    static thread_local std::mt19937_64 rng(
        static_cast<uint64_t>(::time(nullptr)) ^
        reinterpret_cast<uintptr_t>(&rng));
    std::uniform_int_distribution<uint32_t> d(0, 0xFFFF);
    char buf[64];
    std::snprintf(buf, sizeof(buf),
        "urn:uuid:%04x%04x-%04x-%04x-%04x-%04x%04x%04x",
        d(rng), d(rng), d(rng),
        (d(rng) & 0x0FFF) | 0x4000,
        (d(rng) & 0x3FFF) | 0x8000,
        d(rng), d(rng), d(rng));
    return buf;
}

// Trích nội dung phần tử <...localName>VALUE</...localName> đầu tiên (bỏ qua prefix).
std::string DiscoveryService::extractTag(const std::string& xml,
                                         const std::string& localName) {
    const std::string openEnd = localName + ">";
    size_t p = xml.find(openEnd);
    if (p == std::string::npos) return "";
    size_t start = p + openEnd.size();
    size_t end = xml.find('<', start);
    if (end == std::string::npos) return "";
    std::string v = xml.substr(start, end - start);
    // trim
    size_t a = v.find_first_not_of(" \t\r\n");
    size_t b = v.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return v.substr(a, b - a + 1);
}

bool DiscoveryService::probeMatches(const std::string& probe) const {
    // Types: rỗng → match; có thì phải chứa NetworkVideoTransmitter hoặc Device
    std::string types = extractTag(probe, "Types");
    if (!types.empty()) {
        if (types.find("NetworkVideoTransmitter") == std::string::npos &&
            types.find("Device") == std::string::npos) {
            return false;
        }
    }
    // Scopes: rỗng → match; có thì mỗi scope probe phải là prefix của scope thiết bị
    std::string scopes = extractTag(probe, "Scopes");
    if (!scopes.empty()) {
        std::string ours = scopesLine();
        std::istringstream iss(scopes);
        std::string tok;
        while (iss >> tok) {
            if (ours.find(tok) == std::string::npos) return false;
        }
    }
    return true;
}

// ── Dựng bản tin ──────────────────────────────────────────────────────────

std::string DiscoveryService::buildProbeMatch(const std::string& relatesTo) const {
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
       << "<SOAP-ENV:Envelope"
       << " xmlns:SOAP-ENV=\"" << NS_SOAP << "\""
       << " xmlns:wsa=\"" << NS_WSA << "\""
       << " xmlns:wsdd=\"" << NS_WSD << "\""
       << " xmlns:tdn=\"" << NS_DN << "\""
       << " xmlns:tds=\"" << NS_TDS << "\">"
       << "<SOAP-ENV:Header>"
       << "<wsa:MessageID>" << newMessageId() << "</wsa:MessageID>"
       << "<wsa:RelatesTo>" << relatesTo << "</wsa:RelatesTo>"
       << "<wsa:To>" << ANON << "</wsa:To>"
       << "<wsa:Action>" << ACT_PROBE_MATCHES << "</wsa:Action>"
       << "<wsdd:AppSequence InstanceId=\"" << instanceId_
       << "\" MessageNumber=\"" << (++messageNumber_) << "\"/>"
       << "</SOAP-ENV:Header>"
       << "<SOAP-ENV:Body>"
       << "<wsdd:ProbeMatches><wsdd:ProbeMatch>"
       << "<wsa:EndpointReference><wsa:Address>" << endpointRef()
       << "</wsa:Address></wsa:EndpointReference>"
       << "<wsdd:Types>tdn:NetworkVideoTransmitter tds:Device</wsdd:Types>"
       << "<wsdd:Scopes>" << scopesLine() << "</wsdd:Scopes>"
       << "<wsdd:XAddrs>" << xaddr() << "</wsdd:XAddrs>"
       << "<wsdd:MetadataVersion>1</wsdd:MetadataVersion>"
       << "</wsdd:ProbeMatch></wsdd:ProbeMatches>"
       << "</SOAP-ENV:Body></SOAP-ENV:Envelope>";
    return os.str();
}

std::string DiscoveryService::buildResolveMatch(const std::string& relatesTo) const {
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
       << "<SOAP-ENV:Envelope"
       << " xmlns:SOAP-ENV=\"" << NS_SOAP << "\""
       << " xmlns:wsa=\"" << NS_WSA << "\""
       << " xmlns:wsdd=\"" << NS_WSD << "\""
       << " xmlns:tdn=\"" << NS_DN << "\""
       << " xmlns:tds=\"" << NS_TDS << "\">"
       << "<SOAP-ENV:Header>"
       << "<wsa:MessageID>" << newMessageId() << "</wsa:MessageID>"
       << "<wsa:RelatesTo>" << relatesTo << "</wsa:RelatesTo>"
       << "<wsa:To>" << ANON << "</wsa:To>"
       << "<wsa:Action>" << ACT_RESOLVE_MATCHES << "</wsa:Action>"
       << "<wsdd:AppSequence InstanceId=\"" << instanceId_
       << "\" MessageNumber=\"" << (++messageNumber_) << "\"/>"
       << "</SOAP-ENV:Header>"
       << "<SOAP-ENV:Body>"
       << "<wsdd:ResolveMatches><wsdd:ResolveMatch>"
       << "<wsa:EndpointReference><wsa:Address>" << endpointRef()
       << "</wsa:Address></wsa:EndpointReference>"
       << "<wsdd:Types>tdn:NetworkVideoTransmitter tds:Device</wsdd:Types>"
       << "<wsdd:Scopes>" << scopesLine() << "</wsdd:Scopes>"
       << "<wsdd:XAddrs>" << xaddr() << "</wsdd:XAddrs>"
       << "<wsdd:MetadataVersion>1</wsdd:MetadataVersion>"
       << "</wsdd:ResolveMatch></wsdd:ResolveMatches>"
       << "</SOAP-ENV:Body></SOAP-ENV:Envelope>";
    return os.str();
}

std::string DiscoveryService::buildHello() const {
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
       << "<SOAP-ENV:Envelope"
       << " xmlns:SOAP-ENV=\"" << NS_SOAP << "\""
       << " xmlns:wsa=\"" << NS_WSA << "\""
       << " xmlns:wsdd=\"" << NS_WSD << "\""
       << " xmlns:tdn=\"" << NS_DN << "\""
       << " xmlns:tds=\"" << NS_TDS << "\">"
       << "<SOAP-ENV:Header>"
       << "<wsa:MessageID>" << newMessageId() << "</wsa:MessageID>"
       << "<wsa:To>" << TO_DISCOVERY << "</wsa:To>"
       << "<wsa:Action>" << ACT_HELLO << "</wsa:Action>"
       << "<wsdd:AppSequence InstanceId=\"" << instanceId_
       << "\" MessageNumber=\"" << (++messageNumber_) << "\"/>"
       << "</SOAP-ENV:Header>"
       << "<SOAP-ENV:Body>"
       << "<wsdd:Hello>"
       << "<wsa:EndpointReference><wsa:Address>" << endpointRef()
       << "</wsa:Address></wsa:EndpointReference>"
       << "<wsdd:Types>tdn:NetworkVideoTransmitter tds:Device</wsdd:Types>"
       << "<wsdd:Scopes>" << scopesLine() << "</wsdd:Scopes>"
       << "<wsdd:XAddrs>" << xaddr() << "</wsdd:XAddrs>"
       << "<wsdd:MetadataVersion>1</wsdd:MetadataVersion>"
       << "</wsdd:Hello>"
       << "</SOAP-ENV:Body></SOAP-ENV:Envelope>";
    return os.str();
}

std::string DiscoveryService::buildBye() const {
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
       << "<SOAP-ENV:Envelope"
       << " xmlns:SOAP-ENV=\"" << NS_SOAP << "\""
       << " xmlns:wsa=\"" << NS_WSA << "\""
       << " xmlns:wsdd=\"" << NS_WSD << "\""
       << " xmlns:tdn=\"" << NS_DN << "\""
       << " xmlns:tds=\"" << NS_TDS << "\">"
       << "<SOAP-ENV:Header>"
       << "<wsa:MessageID>" << newMessageId() << "</wsa:MessageID>"
       << "<wsa:To>" << TO_DISCOVERY << "</wsa:To>"
       << "<wsa:Action>" << ACT_BYE << "</wsa:Action>"
       << "<wsdd:AppSequence InstanceId=\"" << instanceId_
       << "\" MessageNumber=\"" << (++messageNumber_) << "\"/>"
       << "</SOAP-ENV:Header>"
       << "<SOAP-ENV:Body>"
       << "<wsdd:Bye>"
       << "<wsa:EndpointReference><wsa:Address>" << endpointRef()
       << "</wsa:Address></wsa:EndpointReference>"
       << "<wsdd:Types>tdn:NetworkVideoTransmitter tds:Device</wsdd:Types>"
       << "<wsdd:Scopes>" << scopesLine() << "</wsdd:Scopes>"
       << "<wsdd:XAddrs>" << xaddr() << "</wsdd:XAddrs>"
       << "<wsdd:MetadataVersion>1</wsdd:MetadataVersion>"
       << "</wsdd:Bye>"
       << "</SOAP-ENV:Body></SOAP-ENV:Envelope>";
    return os.str();
}

// ── Lifecycle ───────────────────────────────────────────────────────────────

bool DiscoveryService::start() {
    if (running_) return true;

    sock_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) {
        std::cerr << "[Discovery] socket() failed\n";
        return false;
    }

    int yes = 1;
    setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#ifdef SO_REUSEPORT
    setsockopt(sock_, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(MCAST_PORT);
    if (::bind(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[Discovery] bind(3702) failed — cần quyền/không bị chiếm port\n";
        ::close(sock_);
        sock_ = -1;
        return false;
    }

    // Bind multicast trên đúng interface có deviceIp (tránh kernel chọn nhầm
    // Docker bridge / WireGuard interface khi server có nhiều NIC).
    struct in_addr localIf{};
    localIf.s_addr = inet_addr(cfg_.deviceIp.c_str());
    if (localIf.s_addr == INADDR_NONE || localIf.s_addr == 0) {
        // Fallback: kernel tự chọn
        localIf.s_addr = htonl(INADDR_ANY);
    }

    // Join multicast group trên interface có deviceIp
    ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(MCAST_ADDR);
    mreq.imr_interface.s_addr = localIf.s_addr;
    if (setsockopt(sock_, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   &mreq, sizeof(mreq)) < 0) {
        std::cerr << "[Discovery] IP_ADD_MEMBERSHIP failed\n";
        ::close(sock_);
        sock_ = -1;
        return false;
    }

    // Outbound multicast: buộc dùng interface có deviceIp (không để kernel
    // tùy ý chọn docker0/wg0/br-*).
    if (setsockopt(sock_, IPPROTO_IP, IP_MULTICAST_IF,
                   &localIf, sizeof(localIf)) < 0) {
        std::cerr << "[Discovery] IP_MULTICAST_IF failed (non-fatal)\n";
    }

    // TTL cho gói multicast gửi đi
    unsigned char ttl = 1;
    setsockopt(sock_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl));

    // Bật loopback cho gói multicast (để probe.py cùng máy nhận được)
    unsigned char loop = 1;
    setsockopt(sock_, IPPROTO_IP, IP_MULTICAST_LOOP, &loop, sizeof(loop));

    // Recv timeout 1s để vòng lặp kiểm tra running_
    timeval tv{};
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    running_ = true;
    thread_ = std::thread(&DiscoveryService::recvLoop, this);

    // Đăng ký singleton để DeviceService::SystemReboot triệu hồi.
    g_discoveryInstance = this;

    // Startup Hello burst: gửi 3 lần cách nhau 300ms để tăng khả năng tool
    // catch được Hello của mình (thay vì Hello nhiễu từ WSDAPI/camera khác).
    for (int i = 0; i < 3; ++i) {
        sendMulticast(buildHello());
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    // Heartbeat: phát Hello định kỳ mỗi 3 giây. Windows WSDAPI phát Hello
    // chỉ ~15 phút/lần nên Hello của mình sẽ "override" cache last-seen của
    // tool trong hầu hết mọi thời điểm. Đây là hành vi phổ biến ở camera IP
    // thực (nhiều hãng gửi Hello 5-30s/lần).
    heartbeatThread_ = std::thread([this]() {
        using namespace std::chrono_literals;
        while (running_) {
            std::this_thread::sleep_for(3s);
            if (!running_) break;
            sendMulticast(buildHello());
        }
    });

    std::cout << "[Discovery] WS-Discovery listening on "
              << MCAST_ADDR << ":" << MCAST_PORT
              << " (Hello heartbeat 3s)\n";
    return true;
}

void DiscoveryService::sendMulticast(const std::string& xml) {
    if (sock_ < 0) return;
    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_addr.s_addr = inet_addr(MCAST_ADDR);
    dst.sin_port = htons(MCAST_PORT);
    ::sendto(sock_, xml.data(), xml.size(), 0,
             reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
}

void DiscoveryService::announceReboot() {
    // Test tool test SystemReboot/FactoryDefault sẽ chờ Bye rồi Hello.
    // Gửi Bye → tăng InstanceId (mô phỏng process mới sau reboot) → burst Hello.
    // Burst 8 lần trong 4 giây để "áp đảo" bất kỳ Hello nhiễu nào từ Windows
    // WSDAPI / camera khác trong LAN (tool 24.12 không filter theo Types/UUID).
    std::cout << "[Discovery] Announce reboot: Bye + Hello burst (8x/4s)\n";
    sendMulticast(buildBye());
    std::thread([this]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        // Update instance ID để tool nhận diện device đã "reboot"
        instanceId_ = static_cast<uint32_t>(::time(nullptr));
        messageNumber_ = 0;
        for (int i = 0; i < 8; ++i) {
            sendMulticast(buildHello());
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }).detach();
}

void DiscoveryService::stop() {
    if (!running_ && sock_ < 0) return;

    // Gửi Bye trước khi đóng
    if (sock_ >= 0) {
        sendMulticast(buildBye());
    }

    running_ = false;
    if (thread_.joinable()) thread_.join();
    if (heartbeatThread_.joinable()) heartbeatThread_.join();

    if (sock_ >= 0) {
        ::close(sock_);
        sock_ = -1;
    }
    g_discoveryInstance = nullptr;
}

void DiscoveryService::recvLoop() {
    std::vector<char> buf(16384);
    while (running_) {
        sockaddr_in src{};
        socklen_t slen = sizeof(src);
        ssize_t n = ::recvfrom(sock_, buf.data(), buf.size() - 1, 0,
                               reinterpret_cast<sockaddr*>(&src), &slen);
        if (n <= 0) continue;  // timeout hoặc lỗi tạm — lặp lại kiểm tra running_

        std::string msg(buf.data(), static_cast<size_t>(n));

        // Bỏ qua chính bản tin mình phát (ProbeMatches/ResolveMatches/Hello/Bye)
        if (msg.find("ProbeMatches") != std::string::npos ||
            msg.find("ResolveMatches") != std::string::npos) {
            continue;
        }

        const bool isProbe   = msg.find("Probe>")   != std::string::npos ||
                               msg.find("Probe ")   != std::string::npos ||
                               msg.find(":Probe")   != std::string::npos;
        const bool isResolve = msg.find("Resolve>") != std::string::npos ||
                               msg.find(":Resolve") != std::string::npos;

        // Loại nhầm ProbeMatch/ResolveMatch (đã lọc ở trên nhưng chắc chắn)
        if (!isProbe && !isResolve) continue;

        std::string relatesTo = extractTag(msg, "MessageID");

        std::string reply;
        if (isProbe) {
            if (!probeMatches(msg)) continue;   // Types/Scopes không khớp → im lặng
            reply = buildProbeMatch(relatesTo);
        } else {
            reply = buildResolveMatch(relatesTo);
        }

        // Trả unicast về đúng nơi gửi
        ::sendto(sock_, reply.data(), reply.size(), 0,
                 reinterpret_cast<sockaddr*>(&src), slen);
    }
}
