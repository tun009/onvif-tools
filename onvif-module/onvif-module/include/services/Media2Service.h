#pragma once
#include "soapH.h"
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

    virtual int AddConfiguration(
        _ns1__AddConfiguration *ns1__AddConfiguration,
        _ns1__AddConfigurationResponse &ns1__AddConfigurationResponse) override;

    virtual int RemoveConfiguration(
        _ns1__RemoveConfiguration *ns1__RemoveConfiguration,
        _ns1__RemoveConfigurationResponse &ns1__RemoveConfigurationResponse) override;

    virtual int GetVideoSourceConfigurationOptions(
        ns1__GetConfiguration *ns1__GetVideoSourceConfigurationOptions,
        _ns1__GetVideoSourceConfigurationOptionsResponse &ns1__GetVideoSourceConfigurationOptionsResponse) override;

    virtual int GetVideoEncoderConfigurationOptions(
        ns1__GetConfiguration *ns1__GetVideoEncoderConfigurationOptions,
        _ns1__GetVideoEncoderConfigurationOptionsResponse &ns1__GetVideoEncoderConfigurationOptionsResponse) override;

    virtual int SetVideoSourceConfiguration(
        _ns1__SetVideoSourceConfiguration *ns1__SetVideoSourceConfiguration,
        ns1__SetConfigurationResponse &ns1__SetVideoSourceConfigurationResponse) override;

    virtual int SetVideoEncoderConfiguration(
        _ns1__SetVideoEncoderConfiguration *ns1__SetVideoEncoderConfiguration,
        ns1__SetConfigurationResponse &ns1__SetVideoEncoderConfigurationResponse) override;

    virtual int SetSynchronizationPoint(
        _ns1__SetSynchronizationPoint *ns1__SetSynchronizationPoint,
        _ns1__SetSynchronizationPointResponse &ns1__SetSynchronizationPointResponse) override;

    virtual int GetServiceCapabilities(
        _ns1__GetServiceCapabilities *ns1__GetServiceCapabilities,
        _ns1__GetServiceCapabilitiesResponse &ns1__GetServiceCapabilitiesResponse) override;

    // Profile T Media Profile Management (mục 7.8)
    virtual int CreateProfile(
        _ns1__CreateProfile *req,
        _ns1__CreateProfileResponse &resp) override;

    virtual int DeleteProfile(
        _ns1__DeleteProfile *req,
        _ns1__DeleteProfileResponse &resp) override;

    // Profile T §7.8: MaxNumberOfConcurrentStreams per config
    virtual int GetVideoEncoderInstances(
        _ns1__GetVideoEncoderInstances *req,
        _ns1__GetVideoEncoderInstancesResponse &resp) override;

    // Profile T §7.15: Metadata configuration options
    virtual int GetMetadataConfigurationOptions(
        ns1__GetConfiguration *req,
        _ns1__GetMetadataConfigurationOptionsResponse &resp) override;

    // Profile T §7.18: OSD (On-Screen Display) 5 op mandatory
    virtual int CreateOSD(
        _ns1__CreateOSD *req,
        _ns1__CreateOSDResponse &resp) override;
    virtual int DeleteOSD(
        _ns1__DeleteOSD *req,
        ns1__SetConfigurationResponse &resp) override;
    virtual int GetOSDs(
        _ns1__GetOSDs *req,
        _ns1__GetOSDsResponse &resp) override;
    virtual int GetOSDOptions(
        _ns1__GetOSDOptions *req,
        _ns1__GetOSDOptionsResponse &resp) override;
    virtual int SetOSD(
        _ns1__SetOSD *req,
        ns1__SetConfigurationResponse &resp) override;

private:
    bool validateAuth();
    ServiceConfig cfg_;
    std::shared_ptr<ICameraBackend> backend_;
};
