#include "services/DeviceService.h"
#include "auth/WsSecurityHandler.h"
#include <iostream>
#include <ctime>

DeviceService::DeviceService(struct soap* soap, const ServiceConfig& cfg, std::shared_ptr<ICameraBackend> backend)
    : DeviceBindingService(soap), cfg_(cfg), backend_(std::move(backend)) {}

DeviceBindingService* DeviceService::copy() {
    return new DeviceService(this->soap, cfg_, backend_);
}

// ── Authentication check ─────────────────────────────────────────────────────
bool DeviceService::validateAuth() {
    WsSecurityHandler handler(cfg_.username, cfg_.password);
    return handler.validate(this->soap);
}

// ── GetSystemDateAndTime ─────────────────────────────────────────────────────
int DeviceService::GetSystemDateAndTime(
    _tds__GetSystemDateAndTime *tds__GetSystemDateAndTime, 
    _tds__GetSystemDateAndTimeResponse &tds__GetSystemDateAndTimeResponse) 
{
    (void)tds__GetSystemDateAndTime;
    auto soap = this->soap;

    // Get time from backend (IPC)
    SystemDateTime dt;
    try {
        dt = backend_->getSystemDateAndTime();
    } catch (const std::exception& e) {
        std::cerr << "[DeviceService] Error getting system time from backend: " << e.what() << std::endl;
        // Fallback to system time
        std::time_t now = std::time(nullptr);
        std::tm* tm_utc = std::gmtime(&now);
        dt.year = tm_utc->tm_year + 1900;
        dt.month = tm_utc->tm_mon + 1;
        dt.day = tm_utc->tm_mday;
        dt.hour = tm_utc->tm_hour;
        dt.minute = tm_utc->tm_min;
        dt.second = tm_utc->tm_sec;
    }

    // Allocate response struct using soap memory manager
    auto sdt = soap_new_tt__SystemDateTime(soap);
    sdt->DateTimeType = tt__SetDateTimeType::Manual;
    sdt->DaylightSavings = dt.daylightSaving;

    // TimeZone
    sdt->TimeZone = soap_new_tt__TimeZone(soap);
    sdt->TimeZone->TZ = "UTC";

    // UTCDateTime
    sdt->UTCDateTime = soap_new_tt__DateTime(soap);
    sdt->UTCDateTime->Date = soap_new_tt__Date(soap);
    sdt->UTCDateTime->Date->Year = dt.year;
    sdt->UTCDateTime->Date->Month = dt.month;
    sdt->UTCDateTime->Date->Day = dt.day;

    sdt->UTCDateTime->Time = soap_new_tt__Time(soap);
    sdt->UTCDateTime->Time->Hour = dt.hour;
    sdt->UTCDateTime->Time->Minute = dt.minute;
    sdt->UTCDateTime->Time->Second = dt.second;

    tds__GetSystemDateAndTimeResponse.SystemDateAndTime = sdt;

    return SOAP_OK;
}

// ── GetDeviceInformation ─────────────────────────────────────────────────────
int DeviceService::GetDeviceInformation(
    _tds__GetDeviceInformation *tds__GetDeviceInformation, 
    _tds__GetDeviceInformationResponse &tds__GetDeviceInformationResponse) 
{
    (void)tds__GetDeviceInformation;
    if (!validateAuth()) {
        return soap_sender_fault_subcode(this->soap, "ter:NotAuthorized", "Sender", "Not Authorized");
    }

    DeviceInfo info;
    try {
        info = backend_->getDeviceInfo();
    } catch (const std::exception& e) {
        std::cerr << "[DeviceService] Error getting device info from backend: " << e.what() << std::endl;
        info.manufacturer = "MockCam Inc.";
        info.model = "MockCam-4K";
        info.firmwareVersion = "1.0.0";
        info.serialNumber = "MOCK-001-2026";
        info.hardwareId = "JetsonOrinNX-8GB";
    }

    tds__GetDeviceInformationResponse.Manufacturer = info.manufacturer;
    tds__GetDeviceInformationResponse.Model = info.model;
    tds__GetDeviceInformationResponse.FirmwareVersion = info.firmwareVersion;
    tds__GetDeviceInformationResponse.SerialNumber = info.serialNumber;
    tds__GetDeviceInformationResponse.HardwareId = info.hardwareId;

    return SOAP_OK;
}

// ── GetCapabilities ──────────────────────────────────────────────────────────
int DeviceService::GetCapabilities(
    _tds__GetCapabilities *tds__GetCapabilities, 
    _tds__GetCapabilitiesResponse &tds__GetCapabilitiesResponse) 
{
    (void)tds__GetCapabilities;
    auto soap = this->soap;

    auto caps = soap_new_tt__Capabilities(soap);

    // Device capabilities
    caps->Device = soap_new_tt__DeviceCapabilities(soap);
    caps->Device->XAddr = "http://" + cfg_.deviceIp + ":" + std::to_string(cfg_.httpPort) + "/onvif/device";
    
    // Media capabilities (VMS expects media capability even if implemented partially/postponed)
    caps->Media = soap_new_tt__MediaCapabilities(soap);
    caps->Media->XAddr = "http://" + cfg_.deviceIp + ":" + std::to_string(cfg_.httpPort) + "/onvif/media";
    
    // PTZ capabilities
    caps->PTZ = soap_new_tt__PTZCapabilities(soap);
    caps->PTZ->XAddr = "http://" + cfg_.deviceIp + ":" + std::to_string(cfg_.httpPort) + "/onvif/ptz";

    tds__GetCapabilitiesResponse.Capabilities = caps;

    return SOAP_OK;
}

// ── GetServices ──────────────────────────────────────────────────────────────
int DeviceService::GetServices(
    _tds__GetServices *tds__GetServices, 
    _tds__GetServicesResponse &tds__GetServicesResponse) 
{
    (void)tds__GetServices;
    auto soap = this->soap;

    // 1. Device Service
    auto deviceSvc = soap_new_tds__Service(soap);
    deviceSvc->Namespace = "http://www.onvif.org/ver10/device/wsdl";
    deviceSvc->XAddr = "http://" + cfg_.deviceIp + ":" + std::to_string(cfg_.httpPort) + "/onvif/device";
    deviceSvc->Version = soap_new_tt__OnvifVersion(soap);
    deviceSvc->Version->Major = 21;
    deviceSvc->Version->Minor = 12;
    tds__GetServicesResponse.Service.push_back(deviceSvc);

    // 2. Media Service (VMS might query this)
    auto mediaSvc = soap_new_tds__Service(soap);
    mediaSvc->Namespace = "http://www.onvif.org/ver20/media/wsdl";
    mediaSvc->XAddr = "http://" + cfg_.deviceIp + ":" + std::to_string(cfg_.httpPort) + "/onvif/media";
    mediaSvc->Version = soap_new_tt__OnvifVersion(soap);
    mediaSvc->Version->Major = 20;
    mediaSvc->Version->Minor = 12;
    tds__GetServicesResponse.Service.push_back(mediaSvc);

    return SOAP_OK;
}

// ── GetScopes ────────────────────────────────────────────────────────────────
int DeviceService::GetScopes(
    _tds__GetScopes *tds__GetScopes, 
    _tds__GetScopesResponse &tds__GetScopesResponse) 
{
    (void)tds__GetScopes;
    auto soap = this->soap;

    std::vector<std::string> scopeUris = {
        "onvif://www.onvif.org/type/NetworkVideoTransmitter",
        "onvif://www.onvif.org/type/video_encoder",
        "onvif://www.onvif.org/name/MockCam-4K",
        "onvif://www.onvif.org/hardware/JetsonOrinNX-8GB",
        "onvif://www.onvif.org/Profile/Streaming",
        "onvif://www.onvif.org/Profile/T"
    };

    for (const auto& uri : scopeUris) {
        auto scope = soap_new_tt__Scope(soap);
        scope->ScopeDef = tt__ScopeDefinition::Fixed;
        scope->ScopeItem = uri;
        tds__GetScopesResponse.Scopes.push_back(scope);
    }

    return SOAP_OK;
}

// ── GetUsers ─────────────────────────────────────────────────────────────────
int DeviceService::GetUsers(
    _tds__GetUsers *tds__GetUsers, 
    _tds__GetUsersResponse &tds__GetUsersResponse) 
{
    (void)tds__GetUsers;
    if (!validateAuth()) {
        return soap_sender_fault_subcode(this->soap, "ter:NotAuthorized", "Sender", "Not Authorized");
    }

    auto soap = this->soap;

    auto user = soap_new_tt__User(soap);
    user->Username = cfg_.username;
    user->UserLevel = tt__UserLevel::Administrator;
    tds__GetUsersResponse.User.push_back(user);

    return SOAP_OK;
}
