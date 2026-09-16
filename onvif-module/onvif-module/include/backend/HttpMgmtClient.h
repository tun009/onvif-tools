#pragma once

#include "backend/IMgmtClient.h"

class HttpMgmtClient final : public IMgmtClient {
public:
    explicit HttpMgmtClient(MgmtClientConfig config);
    DeviceInfo getDeviceInformation() override;
    SystemDateTime getSystemDateAndTime() override;
    void setSystemDateAndTime(const SystemDateTime& request) override;

    HostnameConfig getHostname() override;
    void setHostname(const HostnameConfig& request) override;
    DnsConfig getDns() override;
    void setDns(const DnsConfig& request) override;
    NetworkInterfaceConfig getNetworkInterface() override;
    void setNetworkInterface(const NetworkInterfaceConfig& request) override;
    NetworkGatewayConfig getNetworkGateway() override;
    void setNetworkGateway(const NetworkGatewayConfig& request) override;
    std::vector<NetworkProtocolEntry> getNetworkProtocols() override;
    void setNetworkProtocols(const std::vector<NetworkProtocolEntry>& request) override;

    OnvifAuthenticationResult verifyWssePasswordDigest(
        const WssePasswordDigest& credential) override;
    OnvifAuthenticationResult verifyHttpDigest(
        const HttpDigestCredential& credential) override;
private:
    struct HttpResponse { int status = 0; std::string body; };
    HttpResponse request(const std::string& method, const std::string& path,
                         const std::string& body = "") const;
    MgmtClientConfig config_;
};
