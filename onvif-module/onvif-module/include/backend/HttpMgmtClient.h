#pragma once

#include "backend/IMgmtClient.h"

class HttpMgmtClient final : public IMgmtClient {
public:
    explicit HttpMgmtClient(MgmtClientConfig config);
    DeviceInfo getDeviceInformation() override;
private:
    std::string get(const std::string& path) const;
    MgmtClientConfig config_;
};
