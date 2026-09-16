#pragma once

#include "interface/types/DeviceTypes.h"
#include <stdexcept>
#include <string>
#include <vector>

struct MgmtClientConfig { std::string baseUrl; int connectTimeoutMs = 1000; int requestTimeoutMs = 3000; };

// MGMT từ chối request vì input sai (result=-1, kèm field_error) — khác với
// lỗi vận hành (result=0, HTTP lỗi, mạng lỗi...) vốn nên là SOAP Receiver
// fault. Phân biệt hai loại lỗi này bằng exception type để caller (DeviceService)
// map đúng SOAP-ENV:Sender / SOAP-ENV:Receiver.
class MgmtValidationError : public std::runtime_error {
public:
    explicit MgmtValidationError(const std::string& message) : std::runtime_error(message) {}
};

struct WssePasswordDigest {
    std::string username;
    std::string nonce;
    std::string created;
    std::string passwordDigest;
};

struct HttpDigestCredential {
    std::string username;
    std::string realm;
    std::string method;
    std::string uri;
    std::string nonce;
    std::string qop;
    std::string nc;
    std::string cnonce;
    std::string algorithm;
    std::string response;
};

struct OnvifAuthenticationResult {
    bool authenticated = false;
    std::string userLevel;
};

class IMgmtClient {
public:
    virtual ~IMgmtClient() = default;
    virtual DeviceInfo getDeviceInformation() = 0;
    virtual SystemDateTime getSystemDateAndTime() = 0;
    // Ném MgmtValidationError khi MGMT từ chối do input sai (result=-1); ném
    // std::runtime_error (hoặc lớp con khác) khi lỗi vận hành/kết nối.
    virtual void setSystemDateAndTime(const SystemDateTime& request) = 0;

    // Network configuration (Profile T 7.4) — chỉ IPv4, xem DeviceTypes.h.
    virtual HostnameConfig getHostname() = 0;
    virtual void setHostname(const HostnameConfig& request) = 0;
    virtual DnsConfig getDns() = 0;
    virtual void setDns(const DnsConfig& request) = 0;
    virtual NetworkInterfaceConfig getNetworkInterface() = 0;
    virtual void setNetworkInterface(const NetworkInterfaceConfig& request) = 0;
    virtual NetworkGatewayConfig getNetworkGateway() = 0;
    virtual void setNetworkGateway(const NetworkGatewayConfig& request) = 0;
    virtual std::vector<NetworkProtocolEntry> getNetworkProtocols() = 0;
    virtual void setNetworkProtocols(const std::vector<NetworkProtocolEntry>& request) = 0;

    virtual OnvifAuthenticationResult verifyWssePasswordDigest(
        const WssePasswordDigest& credential) = 0;
    virtual OnvifAuthenticationResult verifyHttpDigest(
        const HttpDigestCredential& credential) = 0;
};
