#pragma once

#include "backend/IMgmtClient.h"

class HttpMgmtClient final : public IMgmtClient {
public:
    explicit HttpMgmtClient(MgmtClientConfig config);
    DeviceInfo getDeviceInformation() override;
    SystemDateTime getSystemDateAndTime() override;
    void setSystemDateAndTime(const SystemDateTime& request) override;
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
