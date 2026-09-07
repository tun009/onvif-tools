#pragma once

#include "interface/types/DeviceTypes.h"
#include <string>

struct MgmtClientConfig { std::string baseUrl; int connectTimeoutMs = 1000; int requestTimeoutMs = 3000; };

class IMgmtClient {
public:
    virtual ~IMgmtClient() = default;
    virtual DeviceInfo getDeviceInformation() = 0;
};
