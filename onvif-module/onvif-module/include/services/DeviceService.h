#pragma once
// DeviceService.h
// Full implementation after: make gen (gSOAP code generation)
//
// This file is a placeholder. After running:
//   make gen
// gSOAP will generate:
//   generated/soapDeviceBindingService.h
// And this class will extend it.

#include "interface/ICameraBackend.h"
#include <memory>
#include <string>
#include <vector>
#include <mutex>

// Forward declare - will be defined after gSOAP gen
// #include "soapDeviceBindingService.h"

struct ServiceConfig {
    std::string deviceIp;
    int         httpPort   = 8080;
    int         rtspPort   = 8554;
    std::string deviceUuid;
    std::string username;
    std::string password;
};

// Placeholder until gSOAP is generated
class DeviceService {
public:
    DeviceService(const ServiceConfig& cfg,
                  std::shared_ptr<ICameraBackend> backend)
        : cfg_(cfg), backend_(std::move(backend)) {}

    // Will be implemented in DeviceService.cpp after gSOAP gen
    // int GetDeviceInformation(...);
    // int GetServices(...);
    // int GetCapabilities(...);
    // etc.

private:
    ServiceConfig                   cfg_;
    std::shared_ptr<ICameraBackend> backend_;
    std::mutex                      scopesMutex_;
    std::vector<std::string>        scopes_;
};
