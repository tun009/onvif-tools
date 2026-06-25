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

    // NOTE: GetVideoEncoderConfigurations & SetVideoEncoderConfiguration
    // are intentionally NOT overridden here. gSOAP (-DWITH_DEFAULT_VIRTUAL)
    // provides default stubs returning SOAP_NO_METHOD for unimplemented methods.

private:
    bool validateAuth();
    ServiceConfig cfg_;
    std::shared_ptr<ICameraBackend> backend_;
};
