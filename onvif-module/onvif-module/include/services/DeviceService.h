#pragma once
#include "soapDeviceBindingService.h"
#include "interface/ICameraBackend.h"
#include <memory>
#include <string>

struct ServiceConfig {
    std::string deviceIp;
    int         httpPort   = 8080;
    int         rtspPort   = 8554;
    std::string deviceUuid;
    std::string username;
    std::string password;
};

class DeviceService : public DeviceBindingService {
public:
    DeviceService(struct soap* soap, const ServiceConfig& cfg, std::shared_ptr<ICameraBackend> backend);
    ~DeviceService() = default;

    virtual DeviceBindingService* copy() override;

    // ONVIF Device service operations
    virtual int GetSystemDateAndTime(_tds__GetSystemDateAndTime *tds__GetSystemDateAndTime, _tds__GetSystemDateAndTimeResponse &tds__GetSystemDateAndTimeResponse) override;
    virtual int GetDeviceInformation(_tds__GetDeviceInformation *tds__GetDeviceInformation, _tds__GetDeviceInformationResponse &tds__GetDeviceInformationResponse) override;
    virtual int GetCapabilities(_tds__GetCapabilities *tds__GetCapabilities, _tds__GetCapabilitiesResponse &tds__GetCapabilitiesResponse) override;
    virtual int GetServices(_tds__GetServices *tds__GetServices, _tds__GetServicesResponse &tds__GetServicesResponse) override;
    virtual int GetScopes(_tds__GetScopes *tds__GetScopes, _tds__GetScopesResponse &tds__GetScopesResponse) override;
    virtual int GetUsers(_tds__GetUsers *tds__GetUsers, _tds__GetUsersResponse &tds__GetUsersResponse) override;
    virtual int GetServiceCapabilities(_tds__GetServiceCapabilities *tds__GetServiceCapabilities, _tds__GetServiceCapabilitiesResponse &tds__GetServiceCapabilitiesResponse) override;

private:
    bool validateAuth();

    ServiceConfig cfg_;
    std::shared_ptr<ICameraBackend> backend_;
};
