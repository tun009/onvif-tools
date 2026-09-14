#pragma once
#include "services/DeviceService.h"
#include "services/DiscoveryService.h"
#include "core/ServiceRegistry.h"
#include "backend/IMgmtClient.h"
#include <memory>
#include <thread>
#include <atomic>

class OnvifServer {
public:
    OnvifServer(const ServiceConfig& cfg,
                std::shared_ptr<ICameraBackend> backend,
                bool discoveryEnabled = true,
                std::shared_ptr<IMgmtClient> authClient = nullptr);
    ~OnvifServer();

    bool start();
    void stop();

private:
    void listenLoop();

    ServiceConfig cfg_;
    std::shared_ptr<ICameraBackend> backend_;
    std::atomic<bool> running_{false};
    std::thread serverThread_;
    int masterSocket_ = -1;
    bool discoveryEnabled_ = true;
    std::shared_ptr<IMgmtClient> authClient_;

    std::unique_ptr<DiscoveryService> discovery_;

    // Service Registry — route service string-based (Phase 2+ refactor).
    // Service chưa migrate vẫn theo if-else trong listenLoop.
    ServiceRegistry registry_;
};
