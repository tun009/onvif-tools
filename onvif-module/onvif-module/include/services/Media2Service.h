#pragma once
#include "soapMedia2BindingService.h"
#include "services/DeviceService.h"
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

    // ── Các method cốt lõi cần override ───────────────────────────────────
    virtual int GetProfiles(
        _ns1__GetProfiles *ns1__GetProfiles,
        _ns1__GetProfilesResponse &ns1__GetProfilesResponse) override;

    virtual int GetStreamUri(
        _ns1__GetStreamUri *ns1__GetStreamUri,
        _ns1__GetStreamUriResponse &ns1__GetStreamUriResponse) override;

    virtual int GetSnapshotUri(
        _ns1__GetSnapshotUri *ns1__GetSnapshotUri,
        _ns1__GetSnapshotUriResponse &ns1__GetSnapshotUriResponse) override;

    virtual int GetVideoSourceConfigurations(
        ns1__GetConfiguration *ns1__GetVideoSourceConfigurations,
        _ns1__GetVideoSourceConfigurationsResponse &ns1__GetVideoSourceConfigurationsResponse) override;

    virtual int GetVideoEncoderConfigurations(
        ns1__GetConfiguration *ns1__GetVideoEncoderConfigurations,
        _ns1__GetVideoEncoderConfigurationsResponse &ns1__GetVideoEncoderConfigurationsResponse) override;

private:
    bool validateAuth();
    ServiceConfig cfg_;
    std::shared_ptr<ICameraBackend> backend_;
};
