#pragma once
// DeviceIOService — adapter bọc DeviceIOHandler vào interface IOnvifService.
//
// Pilot migration (Phase 2, xem docs/10-refactor-plan-multi-profile.md).
// KHÔNG viết lại logic — chỉ delegate sang DeviceIOHandler::dispatch() sẵn có.
// Chứng minh Service Registry pattern hoạt động trước khi migrate service khác.

#include "core/IOnvifService.h"
#include "services/DeviceIOHandler.h"

class DeviceIOService : public IOnvifService {
public:
    std::string pathPrefix() const override { return "/onvif/deviceIO"; }
    std::string name() const override { return "DeviceIOService"; }

    std::string handle(const std::string& rawRequest) override {
        return DeviceIOHandler::dispatch(rawRequest);
    }
};
