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
                std::shared_ptr<IMgmtClient> authClient = nullptr,
                // Service Recording Control thật (capability recording=real). nullptr = không đăng ký.
                std::unique_ptr<IOnvifService> recordingService = nullptr,
                // Service Recording Search thật (capability search=real). nullptr = không đăng ký.
                std::unique_ptr<IOnvifService> searchService = nullptr);
    ~OnvifServer();

    bool start();
    void stop();

private:
    void listenLoop();
    // Hỏi MGMT chế độ Discovery định kỳ và áp dụng cho DiscoveryService (web đổi chế độ thì onvif-module theo).
    void pollDiscoveryMode();

    ServiceConfig cfg_;
    std::shared_ptr<ICameraBackend> backend_;
    std::atomic<bool> running_{false};
    std::thread serverThread_;
    int masterSocket_ = -1;
    bool discoveryEnabled_ = true;
    std::shared_ptr<IMgmtClient> authClient_;

    std::unique_ptr<DiscoveryService> discovery_;
    std::thread discoveryModeThread_;
    std::atomic<bool> discoveryModePolling_{false};

    // Service Registry — route service string-based (Phase 2+ refactor).
    // Service chưa migrate vẫn theo if-else trong listenLoop.
    ServiceRegistry registry_;
};
