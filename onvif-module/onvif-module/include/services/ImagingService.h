#pragma once
// ImagingService — ONVIF Imaging Service (ver20).
// Profile T MANDATORY: GetImagingSettings, SetImagingSettings, GetOptions,
// GetServiceCapabilities.

#include "soapImagingBindingService.h"
#include "services/DeviceService.h"     // ServiceConfig
#include "interface/ICameraBackend.h"
#include <memory>
#include <map>
#include <mutex>

class ImagingService : public ImagingBindingService {
public:
    ImagingService(struct soap* soap,
                   const ServiceConfig& cfg,
                   std::shared_ptr<ICameraBackend> backend);
    ~ImagingService() = default;

    ImagingBindingService* copy() override;

    int GetServiceCapabilities(
        _timg__GetServiceCapabilities *req,
        _timg__GetServiceCapabilitiesResponse &resp) override;

    int GetImagingSettings(
        _timg__GetImagingSettings *req,
        _timg__GetImagingSettingsResponse &resp) override;

    int SetImagingSettings(
        _timg__SetImagingSettings *req,
        _timg__SetImagingSettingsResponse &resp) override;

    int GetOptions(
        _timg__GetOptions *req,
        _timg__GetOptionsResponse &resp) override;

private:
    ServiceConfig cfg_;
    std::shared_ptr<ICameraBackend> backend_;

    // Cache riêng vì backend mock không persist một số field (vd BLC mode).
    // Static để chia sẻ giữa các instance ImagingService (mỗi request tạo 1 instance).
    static std::mutex cacheMtx_;
    static std::map<std::string, ImagingSettings> cache_;

    static bool isValidToken(const std::string& tok);
    static bool isValidSettings(const ImagingSettings& s);
};
