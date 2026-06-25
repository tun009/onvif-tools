#pragma once
#include "soapMedia2BindingService.h"
#include "services/DeviceService.h"   // tái dụng ServiceConfig
#include "interface/ICameraBackend.h"
#include <memory>
#include <string>

class Media2Service : public Media2BindingService {
public:
    Media2Service(struct soap* soap,
                  const ServiceConfig& cfg,
                  std::shared_ptr<ICameraBackend> backend);
    ~Media2Service() = default;

    virtual Media2BindingService* copy() override;

    // ── Profile queries ────────────────────────────────────────────────────
    virtual int GetProfiles(
        _tr2__GetProfiles *tr2__GetProfiles,
        _tr2__GetProfilesResponse &tr2__GetProfilesResponse) override;

    // ── Stream URI ─────────────────────────────────────────────────────────
    virtual int GetStreamUri(
        _tr2__GetStreamUri *tr2__GetStreamUri,
        _tr2__GetStreamUriResponse &tr2__GetStreamUriResponse) override;

    // ── Snapshot URI ───────────────────────────────────────────────────────
    virtual int GetSnapshotUri(
        _tr2__GetSnapshotUri *tr2__GetSnapshotUri,
        _tr2__GetSnapshotUriResponse &tr2__GetSnapshotUriResponse) override;

    // ── Video encoder config ───────────────────────────────────────────────
    virtual int GetVideoEncoderConfigurations(
        _tr2__GetConfiguration *tr2__GetConfiguration,
        _tr2__GetVideoEncoderConfigurationsResponse &tr2__GetVideoEncoderConfigurationsResponse) override;

    virtual int SetVideoEncoderConfiguration(
        _tr2__SetVideoEncoderConfiguration *tr2__SetVideoEncoderConfiguration,
        _tr2__SetVideoEncoderConfigurationResponse &tr2__SetVideoEncoderConfigurationResponse) override;

private:
    bool validateAuth();

    ServiceConfig cfg_;
    std::shared_ptr<ICameraBackend> backend_;
};
