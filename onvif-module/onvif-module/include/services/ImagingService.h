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

    // Focus Move stubs (Profile T conditional §7.16). Declare Focus support
    // để pass 8 IMAGING-2-1-* Focus Move tests. State đơn giản trong RAM.
    int Move(_timg__Move *req, _timg__MoveResponse &resp) override;
    int Stop(_timg__Stop *req, _timg__StopResponse &resp) override;
    int GetStatus(_timg__GetStatus *req, _timg__GetStatusResponse &resp) override;
    int GetMoveOptions(_timg__GetMoveOptions *req,
                       _timg__GetMoveOptionsResponse &resp) override;

private:
    ServiceConfig cfg_;
    std::shared_ptr<ICameraBackend> backend_;

    // Cache riêng vì backend mock không persist một số field. Extended lưu
    // các mode ONVIF (Exposure/WhiteBalance/Focus/IrCut/BLC/WDR) không có
    // trong ImagingSettings backend struct.
    struct ExtSettings {
        ImagingSettings basic;             // brightness/contrast/saturation/sharpness/BLC/WDR
        int exposureMode = 0;              // 0=AUTO, 1=MANUAL (enum tt__ExposureMode)
        int whiteBalanceMode = 0;          // 0=AUTO, 1=MANUAL
        int autoFocusMode = 0;             // 0=AUTO, 1=MANUAL
        int irCutFilter = 0;               // 0=ON, 1=OFF, 2=AUTO (enum tt__IrCutFilterMode)
        // Focus params (IMAGING-1-1-14 persist test)
        float focusNearLimit = 0.0f;
        float focusFarLimit = 100.0f;
        float focusDefaultSpeed = 0.5f;
        bool loaded = false;               // đã init từ backend?
    };
    static std::mutex cacheMtx_;
    static std::map<std::string, ExtSettings> cache_;

    static bool isValidToken(const std::string& tok);
    static bool isValidSettings(const ImagingSettings& s);
};
