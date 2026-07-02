#pragma once
// DiscoveryService.h
// WS-Discovery (ONVIF) — self-contained UDP multicast listener.
// Không phụ thuộc plugin wsddapi của gSOAP; tự dựng/parse bản tin SOAP-over-UDP.
//
// Giao thức: WS-Discovery 2005/04 + WS-Addressing 2004/08
//   Multicast: 239.255.255.250:3702 (UDP)
//   Xử lý: Probe -> ProbeMatch (unicast), Resolve -> ResolveMatch (unicast)
//   Phát:   Hello (khi start), Bye (khi stop) ra multicast

#include "services/DeviceService.h"   // ServiceConfig
#include <string>
#include <atomic>
#include <thread>
#include <cstdint>

class DiscoveryService {
public:
    explicit DiscoveryService(const ServiceConfig& cfg);
    ~DiscoveryService();

    // Mở UDP socket, join multicast, gửi Hello, chạy thread nhận.
    bool start();
    // Gửi Bye, đóng socket, dừng thread.
    void stop();

    // Giả lập reboot: gửi Bye ra multicast, chờ 1s, gửi Hello lại.
    // Dùng cho SystemReboot / SetSystemFactoryDefault để test tool detect
    // được lifecycle Bye→Hello sau reboot (DISCOVERY-1-1-2/1-1-8).
    void announceReboot();

    // Truy cập instance đang chạy (singleton nhẹ) — để DeviceService gọi.
    static DiscoveryService* current();

private:
    void recvLoop();
    void sendMulticast(const std::string& xml);   // gửi 1 gói UDP ra multicast

    // ── Dựng bản tin ──────────────────────────────────────────────
    std::string buildProbeMatch(const std::string& relatesTo) const;
    std::string buildResolveMatch(const std::string& relatesTo) const;
    std::string buildHello() const;
    std::string buildBye() const;

    // ── Helpers ───────────────────────────────────────────────────
    std::string scopesLine() const;              // danh sách scope, cách nhau bởi space
    std::string xaddr() const;                   // http://<ip>:<port>/onvif/device_service
    std::string endpointRef() const;             // urn:uuid:<deviceUuid>
    std::string newMessageId() const;            // urn:uuid:<random>
    static std::string extractTag(const std::string& xml, const std::string& localName);
    // Kiểm tra Probe có khớp Types/Scopes của thiết bị không (để quyết định trả lời)
    bool probeMatches(const std::string& probeXml) const;

    ServiceConfig cfg_;
    int sock_ = -1;
    std::atomic<bool> running_{false};
    std::thread thread_;

    // AppSequence: InstanceId cố định theo phiên chạy, MessageNumber tăng dần
    uint32_t instanceId_ = 0;
    mutable std::atomic<uint32_t> messageNumber_{0};
};
