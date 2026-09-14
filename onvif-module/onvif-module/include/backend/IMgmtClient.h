#pragma once

#include "interface/types/DeviceTypes.h"
#include <string>

struct MgmtClientConfig { std::string baseUrl; int connectTimeoutMs = 1000; int requestTimeoutMs = 3000; };

struct WssePasswordDigest {
    std::string username;
    std::string nonce;
    std::string created;
    std::string passwordDigest;
};

struct OnvifAuthenticationResult {
    bool authenticated = false;
    std::string userLevel;
};

class IMgmtClient {
public:
    virtual ~IMgmtClient() = default;
    virtual DeviceInfo getDeviceInformation() = 0;
    virtual OnvifAuthenticationResult verifyWssePasswordDigest(
        const WssePasswordDigest& credential) = 0;
};
