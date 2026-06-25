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

    virtual int GetProfiles(
        _ns1__GetProfiles *ns1__GetProfiles,
        _ns1__GetProfilesResponse &ns1__GetProfilesResponse) override;

    virtual int GetStreamUri(
        _ns1__GetStreamUri *ns1__GetStreamUri,
        _ns1__GetStreamUriResponse &ns1__GetStreamUriResponse) override;

    virtual int GetSnapshotUri(
        _ns1__GetSnapshotUri *ns1__GetSnapshotUri,
        _ns1__GetSnapshotUriResponse &ns1__GetSnapshotUriResponse) override;

    virtual int GetVideoEncoderConfigurations(
        _ns1__GetConfiguration *ns1__GetConfiguration,
        _ns1__GetVideoEncoderConfigurationsResponse &ns1__GetVideoEncoderConfigurationsResponse) override;

    virtual int SetVideoEncoderConfiguration(
        _ns1__SetVideoEncoderConfiguration *ns1__SetVideoEncoderConfiguration,
        _ns1__SetVideoEncoderConfigurationResponse &ns1__SetVideoEncoderConfigurationResponse) override;

private:
    bool validateAuth();
    ServiceConfig cfg_;
    std::shared_ptr<ICameraBackend> backend_;
};
