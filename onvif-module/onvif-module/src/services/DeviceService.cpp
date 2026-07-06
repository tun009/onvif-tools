#include "services/DeviceService.h"
#include "services/DiscoveryService.h"
#include "auth/WsSecurityHandler.h"
#include <iostream>
#include <ctime>
#include <cstring>
#include <sstream>
#include <algorithm>

std::mutex DeviceService::netMtx_;
DeviceService::NetworkState DeviceService::net_;
std::mutex DeviceService::sysMtx_;
DeviceService::SystemState DeviceService::sys_;

// ONVIF fault XML thủ công — gSOAP không tự declare xmlns:ter cho prefix trong
// subcode Value. Copy pattern từ ImagingService (đã pass ở IMAGING-1-1-8).
static int devSendOnvifFault(struct soap* soap,
                             const char* code,       // "SOAP-ENV:Sender"
                             const char* subcode,    // "ter:InvalidArgVal"
                             const char* subSubcode, // "ter:InvalidHostname" hoặc nullptr
                             const char* reason) {
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
       << "<SOAP-ENV:Envelope"
       << " xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\""
       << " xmlns:ter=\"http://www.onvif.org/ver10/error\">"
       << "<SOAP-ENV:Body><SOAP-ENV:Fault>"
       << "<SOAP-ENV:Code><SOAP-ENV:Value>" << code << "</SOAP-ENV:Value>"
       << "<SOAP-ENV:Subcode><SOAP-ENV:Value>" << subcode << "</SOAP-ENV:Value>";
    if (subSubcode && *subSubcode) {
        os << "<SOAP-ENV:Subcode><SOAP-ENV:Value>" << subSubcode
           << "</SOAP-ENV:Value></SOAP-ENV:Subcode>";
    }
    os << "</SOAP-ENV:Subcode></SOAP-ENV:Code>"
       << "<SOAP-ENV:Reason><SOAP-ENV:Text xml:lang=\"en\">"
       << reason << "</SOAP-ENV:Text></SOAP-ENV:Reason>"
       << "</SOAP-ENV:Fault></SOAP-ENV:Body></SOAP-ENV:Envelope>";
    std::string xml = os.str();
    soap->http_content = "application/soap+xml; charset=utf-8";
    soap_response(soap, SOAP_FILE);
    soap_send_raw(soap, xml.data(), xml.size());
    soap_end_send(soap);
    return SOAP_STOP;
}

// Kiểm tra hostname theo RFC 952/1123 tối giản: chỉ letter/digit/hyphen,
// không được rỗng, không được bắt đầu/kết thúc bằng '-'.
static bool isValidHostname(const std::string& h) {
    if (h.empty() || h.size() > 63) return false;
    if (h.front() == '-' || h.back() == '-') return false;
    for (char c : h) {
        if (!(std::isalnum((unsigned char)c) || c == '-' || c == '.'))
            return false;
    }
    return true;
}

DeviceService::DeviceService(struct soap* soap, const ServiceConfig& cfg, std::shared_ptr<ICameraBackend> backend)
    : DeviceBindingService(soap), cfg_(cfg), backend_(std::move(backend)) {}

DeviceBindingService* DeviceService::copy() {
    return new DeviceService(this->soap, cfg_, backend_);
}

extern thread_local bool g_http_digest_authenticated;

// ── Authentication check ─────────────────────────────────────────────────────
bool DeviceService::validateAuth() {
    if (g_http_digest_authenticated) {
        return true;
    }
    WsSecurityHandler handler(cfg_.username, cfg_.password);
    return handler.validate(this->soap);
}

// ── GetSystemDateAndTime ─────────────────────────────────────────────────────
int DeviceService::GetSystemDateAndTime(
    _tds__GetSystemDateAndTime *tds__GetSystemDateAndTime, 
    _tds__GetSystemDateAndTimeResponse &tds__GetSystemDateAndTimeResponse) 
{
    (void)tds__GetSystemDateAndTime;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
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

    // TimeZone — POSIX TZ format: STD offset [DST[offset][,rule]]
    // "UTC" alone không hợp lệ ("standard offset part format is incorrect"),
    // dùng "UTC0" (STD=UTC, offset=0). Test DEVICE-3-1-1.
    sdt->TimeZone = soap_new_tt__TimeZone(soap);
    sdt->TimeZone->TZ = "UTC0";

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
// NOTE: Per ONVIF spec, GetDeviceInformation does NOT require authentication.
// It is a public endpoint accessible to any client for device discovery.
int DeviceService::GetDeviceInformation(
    _tds__GetDeviceInformation *tds__GetDeviceInformation, 
    _tds__GetDeviceInformationResponse &tds__GetDeviceInformationResponse) 
{
    (void)tds__GetDeviceInformation;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;

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
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    auto soap = this->soap;

    // Helper: cấp phát bool* do soap quản lý (tự giải phóng khi soap_end)
    auto B = [&](bool v) { bool* p = (bool*)soap_malloc(soap, sizeof(bool)); *p = v; return p; };
    const std::string base = "http://" + cfg_.deviceIp + ":" + std::to_string(cfg_.httpPort);

    // Lọc theo Category yêu cầu. Khi client hỏi 1 category cụ thể, PHẢI chỉ trả
    // đúng category đó (test DEVICE-1-1-3/4/5/10). Rỗng hoặc All → trả tất cả.
    // Được hỗ trợ (legacy GetCapabilities): Device, Events, Imaging.
    // KHÔNG hỗ trợ: Media (chỉ có Media2/ver20), PTZ, Analytics → phải trả fault.
    bool wantAll = false, wantDevice = false, wantEvents = false,
         wantMedia = false, wantImaging = false, reqUnsupported = false;
    if (!tds__GetCapabilities || tds__GetCapabilities->Category.empty()) {
        wantAll = true;
    } else {
        for (auto cat : tds__GetCapabilities->Category) {
            switch (cat) {
                case tt__CapabilityCategory::All:       wantAll = true; break;
                case tt__CapabilityCategory::Device:    wantDevice = true; break;
                case tt__CapabilityCategory::Events:    wantEvents = true; break;
                case tt__CapabilityCategory::Imaging:   wantImaging = true; break;
                // Media1 (legacy) đã bỏ declare — Profile T dùng Media2.
                // DEVICE-1-1-4: Category=Media phải trả fault NoSuchService.
                case tt__CapabilityCategory::Media:     reqUnsupported = true; break;
                case tt__CapabilityCategory::PTZ:       reqUnsupported = true; break;
                case tt__CapabilityCategory::Analytics: reqUnsupported = true; break;
            }
        }
    }

    // Category không hỗ trợ → SOAP 1.2 fault env:Receiver /
    // ter:ActionNotSupported / ter:NoSuchService (DEVICE-1-1-4, 1-1-6, 1-1-11).
    // Dùng QName dạng "URI":local để gSOAP tự khai báo namespace (tránh lỗi
    // "ter is an undeclared prefix").
    if (!wantAll && reqUnsupported) {
        soap_receiver_fault_subcode(
            soap, "\"http://www.onvif.org/ver10/error\":ActionNotSupported",
            "No such service", nullptr);
        if (soap->fault && soap->fault->SOAP_ENV__Code &&
            soap->fault->SOAP_ENV__Code->SOAP_ENV__Subcode) {
            auto sub2 = soap_new_SOAP_ENV__Code(soap);
            sub2->SOAP_ENV__Value =
                soap_strdup(soap, "\"http://www.onvif.org/ver10/error\":NoSuchService");
            soap->fault->SOAP_ENV__Code->SOAP_ENV__Subcode->SOAP_ENV__Subcode = sub2;
        }
        return SOAP_FAULT;
    }

    auto caps = soap_new_tt__Capabilities(soap);

    // ── Device ────────────────────────────────────────────────────────────
    if (wantAll || wantDevice) {
    caps->Device = soap_new_tt__DeviceCapabilities(soap);
    caps->Device->XAddr = base + "/onvif/device_service";

    caps->Device->Network = soap_new_tt__NetworkCapabilities(soap);
    caps->Device->Network->IPFilter          = B(false);
    caps->Device->Network->ZeroConfiguration  = B(false);
    caps->Device->Network->IPVersion6         = B(false);
    caps->Device->Network->DynDNS             = B(false);

    caps->Device->System = soap_new_tt__SystemCapabilities(soap);
    caps->Device->System->DiscoveryResolve = true;
    caps->Device->System->DiscoveryBye     = true;
    caps->Device->System->RemoteDiscovery  = false;
    caps->Device->System->SystemBackup     = false;
    caps->Device->System->SystemLogging    = false;
    caps->Device->System->FirmwareUpgrade  = false;
    // SupportedVersions BẮT BUỘC (schema minOccurs=1)
    {
        auto ver = soap_new_tt__OnvifVersion(soap);
        ver->Major = 21;
        ver->Minor = 12;
        caps->Device->System->SupportedVersions.push_back(ver);
    }

    caps->Device->Security = soap_new_tt__SecurityCapabilities(soap);
    caps->Device->Security->TLS1_x002e1        = false;
    caps->Device->Security->TLS1_x002e2        = false;
    caps->Device->Security->OnboardKeyGeneration = false;
    caps->Device->Security->AccessPolicyConfig = false;
    caps->Device->Security->X_x002e509Token    = false;
    caps->Device->Security->SAMLToken          = false;
    caps->Device->Security->KerberosToken      = false;
    caps->Device->Security->RELToken           = false;

    // IO capabilities (tool yêu cầu ở DEVICE-1-1-3 STEP 13)
    caps->Device->IO = soap_new_tt__IOCapabilities(soap);
    {
        int* ic = (int*)soap_malloc(soap, sizeof(int)); *ic = 0;
        int* ro = (int*)soap_malloc(soap, sizeof(int)); *ro = 0;
        caps->Device->IO->InputConnectors = ic;
        caps->Device->IO->RelayOutputs    = ro;
    }
    } // end Device

    // DeviceIO capabilities trong Extension (DEVICE-1-1-2 check step 6).
    // Profile T §7.10.3 mandate DeviceIO service với GetVideoSources.
    if (wantAll || wantDevice) {
        caps->Extension = soap_new_tt__CapabilitiesExtension(soap);
        caps->Extension->DeviceIO = soap_new_tt__DeviceIOCapabilities(soap);
        caps->Extension->DeviceIO->XAddr = base + "/onvif/deviceIO";
        caps->Extension->DeviceIO->VideoSources = 1;
        caps->Extension->DeviceIO->VideoOutputs = 0;
        caps->Extension->DeviceIO->AudioSources = 0;
        caps->Extension->DeviceIO->AudioOutputs = 0;
        caps->Extension->DeviceIO->RelayOutputs = 0;
    }

    // ── Media (Profile T: streaming RTP/RTSP/TCP) ─────────────────────────
    // Test tool check "Media capabilities not found" khi Category=All hoặc =Media
    // (DEVICE-1-1-2, 1-1-4). Trỏ về /onvif/media (Media2 sẽ xử lý các op ver20).
    if (wantAll || wantMedia) {
        caps->Media = soap_new_tt__MediaCapabilities(soap);
        caps->Media->XAddr = base + "/onvif/media";
        caps->Media->StreamingCapabilities = soap_new_tt__RealTimeStreamingCapabilities(soap);
        caps->Media->StreamingCapabilities->RTPMulticast              = B(false);
        caps->Media->StreamingCapabilities->RTP_USCORETCP             = B(true);
        caps->Media->StreamingCapabilities->RTP_USCORERTSP_USCORETCP  = B(true);
    }

    // ── Events (Profile T mandatory: PullPoint) ───────────────────────────
    if (wantAll || wantEvents) {
        caps->Events = soap_new_tt__EventCapabilities(soap);
        caps->Events->XAddr = base + "/onvif/event";
        caps->Events->WSSubscriptionPolicySupport                    = true;
        caps->Events->WSPullPointSupport                             = true;
        caps->Events->WSPausableSubscriptionManagerInterfaceSupport  = false;
    }

    // ── Imaging (Profile T mandatory) ─────────────────────────────────────
    if (wantAll || wantImaging) {
        caps->Imaging = soap_new_tt__ImagingCapabilities(soap);
        caps->Imaging->XAddr = base + "/onvif/imaging";
    }

    // PTZ KHÔNG quảng bá: đây là Fixed Camera, không hỗ trợ PTZ trong Profile T.

    tds__GetCapabilitiesResponse.Capabilities = caps;

    return SOAP_OK;
}

// ── GetServices ──────────────────────────────────────────────────────────────
int DeviceService::GetServices(
    _tds__GetServices *tds__GetServices,
    _tds__GetServicesResponse &tds__GetServicesResponse)
{
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    auto soap = this->soap;

    const bool includeCaps = tds__GetServices && tds__GetServices->IncludeCapability;
    const std::string base = "http://" + cfg_.deviceIp + ":" + std::to_string(cfg_.httpPort);

    // Khi IncludeCapability=true, tool đòi Capabilities inline trong tds:Service
    // (DEVICE-1-1-13/14/16/17/19/30). tds:Service.Capabilities là xsd:any nên
    // gSOAP class rỗng — build response XML thủ công.
    if (includeCaps) {
        std::ostringstream os;
        os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
           << "<SOAP-ENV:Envelope"
           << " xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\""
           << " xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\""
           << " xmlns:tt=\"http://www.onvif.org/ver10/schema\""
           << " xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\""
           << " xmlns:tr2=\"http://www.onvif.org/ver20/media/wsdl\""
           << " xmlns:tev=\"http://www.onvif.org/ver10/events/wsdl\""
           << " xmlns:timg=\"http://www.onvif.org/ver20/imaging/wsdl\""
           << " xmlns:tmd=\"http://www.onvif.org/ver10/deviceIO/wsdl\""
           << ">"
           << "<SOAP-ENV:Body><tds:GetServicesResponse>";

        auto svc = [&](const char* ns, const std::string& path,
                       int maj, int min, const std::string& capsXml) {
            os << "<tds:Service>"
               << "<tds:Namespace>" << ns << "</tds:Namespace>"
               << "<tds:XAddr>" << base << path << "</tds:XAddr>"
               << "<tds:Capabilities>" << capsXml << "</tds:Capabilities>"
               << "<tds:Version><tt:Major>" << maj << "</tt:Major>"
               << "<tt:Minor>" << min << "</tt:Minor></tds:Version>"
               << "</tds:Service>";
        };

        // Device — khớp GetServiceCapabilities response
        std::string devCaps =
            "<tds:Capabilities>"
              "<tds:Network IPFilter=\"false\" ZeroConfiguration=\"false\" "
                "IPVersion6=\"false\" DynDNS=\"false\"/>"
              "<tds:Security TLS1.1=\"false\" TLS1.2=\"false\" "
                "OnboardKeyGeneration=\"false\" AccessPolicyConfig=\"false\" "
                "X.509Token=\"false\" SAMLToken=\"false\" KerberosToken=\"false\" "
                "RELToken=\"false\" HttpDigest=\"true\" UsernameToken=\"true\"/>"
              "<tds:System DiscoveryResolve=\"true\" DiscoveryBye=\"true\" "
                "RemoteDiscovery=\"false\" SystemBackup=\"false\" "
                "SystemLogging=\"false\" FirmwareUpgrade=\"false\"/>"
            "</tds:Capabilities>";
        svc("http://www.onvif.org/ver10/device/wsdl", "/onvif/device_service",
            21, 12, devCaps);

        // Media1 (ver10 legacy) — declared cho Profile S support.
        std::string mediaCaps =
            "<trt:Capabilities SnapshotUri=\"true\" Rotation=\"false\" "
             "VideoSourceMode=\"false\" OSD=\"true\">"
              "<trt:ProfileCapabilities MaximumNumberOfProfiles=\"3\"/>"
              "<trt:StreamingCapabilities RTPMulticast=\"false\" "
               "RTP_TCP=\"true\" RTP_RTSP_TCP=\"true\" NonAggregateControl=\"false\"/>"
            "</trt:Capabilities>";
        svc("http://www.onvif.org/ver10/media/wsdl", "/onvif/media",
            21, 12, mediaCaps);

        // Media2 (ver20 - Profile T mandatory)
        std::string media2Caps =
            "<tr2:Capabilities SnapshotUri=\"true\" Rotation=\"false\" "
             "VideoSourceMode=\"false\" OSD=\"true\">"
              "<tr2:ProfileCapabilities MaximumNumberOfProfiles=\"3\" "
               "ConfigurationsSupported=\"VideoSource VideoEncoder\"/>"
              "<tr2:StreamingCapabilities RTPMulticast=\"false\" "
               "RTP_TCP=\"true\" RTP_RTSP_TCP=\"true\" NonAggregateControl=\"false\"/>"
            "</tr2:Capabilities>";
        svc("http://www.onvif.org/ver20/media/wsdl", "/onvif/media",
            21, 12, media2Caps);

        // Events (Profile T mandatory)
        std::string evtCaps =
            "<tev:Capabilities WSSubscriptionPolicySupport=\"true\" "
             "WSPullPointSupport=\"true\" "
             "WSPausableSubscriptionManagerInterfaceSupport=\"false\" "
             "MaxNotificationProducers=\"10\" MaxPullPoints=\"2\" "
             "PersistentNotificationStorage=\"false\"/>";
        svc("http://www.onvif.org/ver10/events/wsdl", "/onvif/event",
            21, 12, evtCaps);

        // Imaging (Profile T mandatory)
        std::string imgCaps =
            "<timg:Capabilities ImageStabilization=\"false\" "
             "Presets=\"false\" AdaptablePreset=\"false\"/>";
        svc("http://www.onvif.org/ver20/imaging/wsdl", "/onvif/imaging",
            21, 12, imgCaps);

        // DeviceIO (Profile T §7.10.3 mandate GetVideoSources).
        // Fix MEDIA2-2-2-1: tool helper HelperConfigureMediaProfileWithVideoSource
        // gọi DeviceIO.GetVideoSources — nếu không có service crash NullRef.
        std::string ioCaps =
            "<tmd:Capabilities VideoSources=\"1\" VideoOutputs=\"0\" "
             "AudioSources=\"0\" AudioOutputs=\"0\" RelayOutputs=\"0\" "
             "DigitalInputs=\"0\" SerialPorts=\"0\"/>";
        svc("http://www.onvif.org/ver10/deviceIO/wsdl", "/onvif/deviceIO",
            21, 12, ioCaps);

        os << "</tds:GetServicesResponse></SOAP-ENV:Body></SOAP-ENV:Envelope>";
        std::string xml = os.str();
        soap->http_content = "application/soap+xml; charset=utf-8";
        soap_response(soap, SOAP_FILE);
        soap_send_raw(soap, xml.data(), xml.size());
        soap_end_send(soap);
        return SOAP_STOP;
    }

    // IncludeCapability=false → dùng gSOAP struct sinh XML bình thường
    // (Capabilities để trống theo spec).
    auto add = [&](const char* ns, const std::string& path, int major, int minor) {
        auto s = soap_new_tds__Service(soap);
        s->Namespace = ns;
        s->XAddr = base + path;
        s->Version = soap_new_tt__OnvifVersion(soap);
        s->Version->Major = major;
        s->Version->Minor = minor;
        tds__GetServicesResponse.Service.push_back(s);
    };

    add("http://www.onvif.org/ver10/device/wsdl",  "/onvif/device_service", 21, 12); // Device
    add("http://www.onvif.org/ver10/media/wsdl",   "/onvif/media",          21, 12); // Media1 (Profile S)
    add("http://www.onvif.org/ver20/media/wsdl",   "/onvif/media",          21, 12); // Media2 (Profile T)
    add("http://www.onvif.org/ver10/events/wsdl",  "/onvif/event",          21, 12); // Events (Profile T)
    add("http://www.onvif.org/ver20/imaging/wsdl", "/onvif/imaging",        21, 12); // Imaging (Profile T)
    add("http://www.onvif.org/ver10/deviceIO/wsdl", "/onvif/deviceIO",       21, 12); // DeviceIO (Profile T §7.10.3)

    return SOAP_OK;
}

// ── GetScopes ────────────────────────────────────────────────────────────────
int DeviceService::GetScopes(
    _tds__GetScopes *tds__GetScopes, 
    _tds__GetScopesResponse &tds__GetScopesResponse) 
{
    (void)tds__GetScopes;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    auto soap = this->soap;

    std::lock_guard<std::mutex> lk(sysMtx_);
    for (const auto& uri : sys_.scopes) {
        auto scope = soap_new_tt__Scope(soap);
        // Fixed scopes: các scope hệ thống (bắt đầu bằng onvif://www.onvif.org/type|hardware).
        // Configurable: các scope user set via SetScopes/AddScopes.
        bool isFixed = uri.find("/type/") != std::string::npos ||
                       uri.find("/hardware/") != std::string::npos ||
                       uri.find("/Profile/") != std::string::npos;
        scope->ScopeDef = isFixed ? tt__ScopeDefinition::Fixed
                                  : tt__ScopeDefinition::Configurable;
        scope->ScopeItem = uri;
        tds__GetScopesResponse.Scopes.push_back(scope);
    }
    return SOAP_OK;
}

// ── Discovery Mode & Scopes CRUD (Profile T §7.3) ────────────────────────────
int DeviceService::GetDiscoveryMode(_tds__GetDiscoveryMode* req,
                                    _tds__GetDiscoveryModeResponse& resp) {
    (void)req;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    std::lock_guard<std::mutex> lk(sysMtx_);
    resp.DiscoveryMode = static_cast<tt__DiscoveryMode>(sys_.discoveryMode);
    return SOAP_OK;
}

int DeviceService::SetDiscoveryMode(_tds__SetDiscoveryMode* req,
                                    _tds__SetDiscoveryModeResponse& resp) {
    (void)resp;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!req) return SOAP_OK;
    int mode;
    {
        std::lock_guard<std::mutex> lk(sysMtx_);
        sys_.discoveryMode = static_cast<int>(req->DiscoveryMode);
        mode = sys_.discoveryMode;
    }
    // 0 = Discoverable, 1 = NonDiscoverable (tt__DiscoveryMode enum)
    if (auto* d = DiscoveryService::current()) d->setDiscoverable(mode == 0);
    return SOAP_OK;
}

static void syncScopesToDiscovery(const std::vector<std::string>& scopes) {
    if (auto* d = DiscoveryService::current()) d->setScopes(scopes);
}

int DeviceService::SetScopes(_tds__SetScopes* req,
                             _tds__SetScopesResponse& resp) {
    (void)resp;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!req) return SOAP_OK;
    std::vector<std::string> newScopes;
    {
        std::lock_guard<std::mutex> lk(sysMtx_);
        for (const auto& uri : sys_.scopes) {
            if (uri.find("/type/") != std::string::npos ||
                uri.find("/hardware/") != std::string::npos ||
                uri.find("/Profile/") != std::string::npos) {
                newScopes.push_back(uri);
            }
        }
        for (const auto& u : req->Scopes) newScopes.push_back(u);
        sys_.scopes = newScopes;
    }
    syncScopesToDiscovery(newScopes);
    if (auto* d = DiscoveryService::current()) d->announceHelloNow();
    return SOAP_OK;
}

int DeviceService::AddScopes(_tds__AddScopes* req,
                             _tds__AddScopesResponse& resp) {
    (void)resp;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!req) return SOAP_OK;
    std::cout << "[DeviceService] AddScopes: " << req->ScopeItem.size() << " items";
    for (const auto& u : req->ScopeItem) std::cout << " [" << u << "]";
    std::cout << std::endl;
    std::vector<std::string> snap;
    {
        std::lock_guard<std::mutex> lk(sysMtx_);
        for (const auto& u : req->ScopeItem) {
            if (std::find(sys_.scopes.begin(), sys_.scopes.end(), u) == sys_.scopes.end()) {
                sys_.scopes.push_back(u);
            }
        }
        snap = sys_.scopes;
    }
    syncScopesToDiscovery(snap);
    if (auto* d = DiscoveryService::current()) d->announceHelloNow();
    return SOAP_OK;
}

int DeviceService::RemoveScopes(_tds__RemoveScopes* req,
                                _tds__RemoveScopesResponse& resp) {
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!req) return SOAP_OK;
    std::vector<std::string> snap;
    {
        std::lock_guard<std::mutex> lk(sysMtx_);
        for (const auto& u : req->ScopeItem) {
            bool isFixed = u.find("/type/") != std::string::npos ||
                           u.find("/hardware/") != std::string::npos ||
                           u.find("/Profile/") != std::string::npos;
            if (isFixed) continue;
            auto it = std::find(sys_.scopes.begin(), sys_.scopes.end(), u);
            if (it != sys_.scopes.end()) {
                sys_.scopes.erase(it);
                resp.ScopeItem.push_back(u);
            }
        }
        snap = sys_.scopes;
    }
    syncScopesToDiscovery(snap);
    if (auto* d = DiscoveryService::current()) d->announceHelloNow();
    return SOAP_OK;
}

// ── GetUsers ─────────────────────────────────────────────────────────────────
int DeviceService::GetUsers(
    _tds__GetUsers *tds__GetUsers, 
    _tds__GetUsersResponse &tds__GetUsersResponse) 
{
    (void)tds__GetUsers;
    this->soap->mustUnderstand = 0;
    if (!validateAuth()) {
        return soap_sender_fault_subcode(this->soap, "ter:NotAuthorized", "Sender", "Not Authorized");
    }
    this->soap->header = nullptr;

    auto soap = this->soap;

    // Đọc từ user cache (Set/Delete/Create update state này).
    std::lock_guard<std::mutex> lk(sysMtx_);
    for (const auto& mu : sys_.users) {
        auto user = soap_new_tt__User(soap);
        user->Username = mu.username;
        // Không trả Password (schema — Password là secret, không expose).
        user->UserLevel = static_cast<tt__UserLevel>(mu.level);
        tds__GetUsersResponse.User.push_back(user);
    }
    return SOAP_OK;
}

// ── GetServiceCapabilities ───────────────────────────────────────────────────
// Profile T MANDATORY: Tool reads this (without credentials first) to determine
// what security methods the device supports. HTTPDigest=true is required for
// Profile T. If this returns NotAuthorized, the tool retries with credentials.
int DeviceService::GetServiceCapabilities(
    _tds__GetServiceCapabilities *tds__GetServiceCapabilities,
    _tds__GetServiceCapabilitiesResponse &tds__GetServiceCapabilitiesResponse)
{
    (void)tds__GetServiceCapabilities;
    this->soap->mustUnderstand = 0;
    if (!validateAuth()) {
        return soap_sender_fault_subcode(this->soap, "ter:NotAuthorized", "Sender", "Not Authorized");
    }
    this->soap->header = nullptr;
    auto soap = this->soap;

    auto caps = soap_new_tds__DeviceServiceCapabilities(soap);
    if (!caps) return soap_receiver_fault(soap, "Memory allocation failed", nullptr);

    // ── Network capabilities ─────────────────────────────────────────────
    caps->Network = soap_new_tds__NetworkCapabilities(soap);
    if (caps->Network) {
        auto dynDNS = new bool(false);
        caps->Network->DynDNS = dynDNS;
        auto ipVersion6 = new bool(false);
        caps->Network->IPVersion6 = ipVersion6;
        auto zeroConf = new bool(false);
        caps->Network->ZeroConfiguration = zeroConf;
        auto ipFilter = new bool(false);
        caps->Network->IPFilter = ipFilter;
    }

    // ── Security capabilities ────────────────────────────────────────────
    // Profile T REQUIRES HttpDigest = true
    caps->Security = soap_new_tds__SecurityCapabilities(soap);
    if (caps->Security) {
        // HTTP Digest authentication - MANDATORY for Profile T
        auto httpDigest = new bool(true);
        caps->Security->HttpDigest = httpDigest;

        // WS-UsernameToken - our current auth mechanism
        auto usernameToken = new bool(true);
        caps->Security->UsernameToken = usernameToken;
    }

    // ── System capabilities ──────────────────────────────────────────────
    caps->System = soap_new_tds__SystemCapabilities(soap);
    if (caps->System) {
        auto discoveryBye = new bool(true);
        caps->System->DiscoveryBye = discoveryBye;
        auto discoveryResolve = new bool(true);
        caps->System->DiscoveryResolve = discoveryResolve;
        auto remoteDiscovery = new bool(false);
        caps->System->RemoteDiscovery = remoteDiscovery;
        auto systemBackup = new bool(false);
        caps->System->SystemBackup = systemBackup;
        auto systemLogging = new bool(false);
        caps->System->SystemLogging = systemLogging;
        auto firmwareUpgrade = new bool(false);
        caps->System->FirmwareUpgrade = firmwareUpgrade;
    }

    tds__GetServiceCapabilitiesResponse.Capabilities = caps;
    std::cout << "[DeviceService] GetServiceCapabilities → HTTPDigest=true, UsernameToken=true" << std::endl;
    return SOAP_OK;
}

// ══════════════════════════════════════════════════════════════════════════
// Network Configuration (Profile T mục 7.4 mandatory — 10 op)
// ══════════════════════════════════════════════════════════════════════════

// Helper: cấp phát std::string* do soap quản lý
static std::string* Sp(struct soap* s, const std::string& v) {
    auto p = soap_new_std__string(s);
    *p = v;
    return p;
}

// ── GetHostname / SetHostname ───────────────────────────────────────────────
int DeviceService::GetHostname(_tds__GetHostname* req,
                               _tds__GetHostnameResponse& resp) {
    (void)req;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    auto soap = this->soap;

    auto info = soap_new_tt__HostnameInformation(soap);
    std::lock_guard<std::mutex> lk(netMtx_);
    info->FromDHCP = net_.hostnameFromDHCP;
    info->Name = Sp(soap, net_.hostname);
    resp.HostnameInformation = info;
    return SOAP_OK;
}

int DeviceService::SetHostname(_tds__SetHostname* req,
                               _tds__SetHostnameResponse& resp) {
    (void)resp;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!req || !isValidHostname(req->Name)) {
        // DEVICE-2-1-3 yêu cầu nested subcode: Sender/InvalidArgVal/InvalidHostname
        return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                 "ter:InvalidArgVal", "ter:InvalidHostname",
                                 "Invalid hostname");
    }
    std::lock_guard<std::mutex> lk(netMtx_);
    net_.hostname = req->Name;
    net_.hostnameFromDHCP = false;
    return SOAP_OK;
}

// ── GetDNS / SetDNS ─────────────────────────────────────────────────────────
int DeviceService::GetDNS(_tds__GetDNS* req, _tds__GetDNSResponse& resp) {
    (void)req;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    auto soap = this->soap;

    auto info = soap_new_tt__DNSInformation(soap);
    std::lock_guard<std::mutex> lk(netMtx_);
    info->FromDHCP = net_.dnsFromDHCP;
    info->SearchDomain = net_.searchDomain;
    // Khi FromDHCP=true, DNS phải nằm ở DNSFromDHCP (không phải DNSManual).
    // Test DEVICE-2-1-8 xác định điều này (check current DNS configuration).
    if (net_.dnsFromDHCP) {
        // Giả lập DHCP-provided DNS (mock)
        for (const auto& ip : std::vector<std::string>{"192.168.8.1", "8.8.8.8"}) {
            auto a = soap_new_tt__IPAddress(soap);
            a->Type = tt__IPType::IPv4;
            a->IPv4Address = Sp(soap, ip);
            info->DNSFromDHCP.push_back(a);
        }
    } else {
        for (const auto& ip : net_.dnsManual) {
            auto a = soap_new_tt__IPAddress(soap);
            a->Type = tt__IPType::IPv4;
            a->IPv4Address = Sp(soap, ip);
            info->DNSManual.push_back(a);
        }
    }
    resp.DNSInformation = info;
    return SOAP_OK;
}

int DeviceService::SetDNS(_tds__SetDNS* req, _tds__SetDNSResponse& resp) {
    (void)resp;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!req) {
        return soap_sender_fault_subcode(this->soap, "ter:InvalidArgVal",
                                         "Sender", "Missing SetDNS request");
    }
    std::lock_guard<std::mutex> lk(netMtx_);
    net_.dnsFromDHCP = req->FromDHCP;
    net_.searchDomain = req->SearchDomain;
    net_.dnsManual.clear();
    for (auto* a : req->DNSManual) {
        if (a && a->IPv4Address) net_.dnsManual.push_back(*a->IPv4Address);
    }
    return SOAP_OK;
}

// ── GetNetworkInterfaces / SetNetworkInterfaces ─────────────────────────────
int DeviceService::GetNetworkInterfaces(_tds__GetNetworkInterfaces* req,
                                        _tds__GetNetworkInterfacesResponse& resp) {
    (void)req;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    auto soap = this->soap;

    std::lock_guard<std::mutex> lk(netMtx_);
    auto iface = soap_new_tt__NetworkInterface(soap);
    iface->token = net_.ifaceToken;
    iface->Enabled = net_.ifaceEnabled;

    iface->Info = soap_new_tt__NetworkInterfaceInfo(soap);
    iface->Info->Name = Sp(soap, net_.ifaceName);
    iface->Info->HwAddress = net_.hwAddress;
    auto* mtu = (int*)soap_malloc(soap, sizeof(int));
    *mtu = net_.mtu;
    iface->Info->MTU = mtu;

    iface->IPv4 = soap_new_tt__IPv4NetworkInterface(soap);
    iface->IPv4->Enabled = true;
    iface->IPv4->Config = soap_new_tt__IPv4Configuration(soap);
    iface->IPv4->Config->DHCP = net_.ipv4DhcpEnabled;
    auto* manual = soap_new_tt__PrefixedIPv4Address(soap);
    manual->Address = net_.ipv4Manual;
    manual->PrefixLength = net_.prefixLength;
    iface->IPv4->Config->Manual.push_back(manual);
    // IPCONFIG-1-1-3: khi DHCP=true, tool expect FromDHCP field có địa chỉ.
    // Fake DHCP lease = same IP.
    if (net_.ipv4DhcpEnabled) {
        auto* fromDhcp = soap_new_tt__PrefixedIPv4Address(soap);
        fromDhcp->Address = net_.ipv4Manual;
        fromDhcp->PrefixLength = net_.prefixLength;
        iface->IPv4->Config->FromDHCP = fromDhcp;
    }

    resp.NetworkInterfaces.push_back(iface);
    return SOAP_OK;
}

int DeviceService::SetNetworkInterfaces(_tds__SetNetworkInterfaces* req,
                                        _tds__SetNetworkInterfacesResponse& resp) {
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!req) {
        return soap_sender_fault_subcode(this->soap, "ter:InvalidArgVal",
                                         "Sender", "Missing request");
    }
    std::lock_guard<std::mutex> lk(netMtx_);
    if (req->InterfaceToken != net_.ifaceToken) {
        return soap_sender_fault_subcode(this->soap, "ter:InvalidArgVal",
                                         "Sender", "Unknown InterfaceToken");
    }
    // Cập nhật state — merge field có gửi (DEVICE-2-1-18 verify appliance).
    if (req->NetworkInterface) {
        auto* ni = req->NetworkInterface;
        if (ni->Enabled) net_.ifaceEnabled = *ni->Enabled;
        if (ni->MTU)     net_.mtu = *ni->MTU;
        if (ni->IPv4) {
            auto* v4 = ni->IPv4;
            if (v4->Enabled) { /* keep enabled */ }
            if (v4->DHCP)    net_.ipv4DhcpEnabled = *v4->DHCP;
            if (!v4->Manual.empty() && v4->Manual[0]) {
                net_.ipv4Manual   = v4->Manual[0]->Address;
                net_.prefixLength = v4->Manual[0]->PrefixLength;
            }
        }
    }
    resp.RebootNeeded = false;
    return SOAP_OK;
}

// ── GetNetworkDefaultGateway / SetNetworkDefaultGateway ─────────────────────
int DeviceService::GetNetworkDefaultGateway(_tds__GetNetworkDefaultGateway* req,
                                            _tds__GetNetworkDefaultGatewayResponse& resp) {
    (void)req;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    auto soap = this->soap;
    std::lock_guard<std::mutex> lk(netMtx_);
    auto gw = soap_new_tt__NetworkGateway(soap);
    gw->IPv4Address = net_.gatewayIPv4;
    resp.NetworkGateway = gw;
    return SOAP_OK;
}

int DeviceService::SetNetworkDefaultGateway(_tds__SetNetworkDefaultGateway* req,
                                            _tds__SetNetworkDefaultGatewayResponse& resp) {
    (void)resp;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!req) {
        return soap_sender_fault_subcode(this->soap, "ter:InvalidArgVal",
                                         "Sender", "Missing request");
    }
    std::lock_guard<std::mutex> lk(netMtx_);
    net_.gatewayIPv4 = req->IPv4Address;
    return SOAP_OK;
}

// ── GetNetworkProtocols / SetNetworkProtocols ───────────────────────────────
int DeviceService::GetNetworkProtocols(_tds__GetNetworkProtocols* req,
                                       _tds__GetNetworkProtocolsResponse& resp) {
    (void)req;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    auto soap = this->soap;
    std::lock_guard<std::mutex> lk(netMtx_);
    for (const auto& p : net_.protocols) {
        auto np = soap_new_tt__NetworkProtocol(soap);
        np->Name = static_cast<tt__NetworkProtocolType>(p.type);
        np->Enabled = p.enabled;
        np->Port.push_back(p.port);
        resp.NetworkProtocols.push_back(np);
    }
    return SOAP_OK;
}

int DeviceService::SetNetworkProtocols(_tds__SetNetworkProtocols* req,
                                       _tds__SetNetworkProtocolsResponse& resp) {
    (void)resp;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!req) {
        return soap_sender_fault_subcode(this->soap, "ter:InvalidArgVal",
                                         "Sender", "Missing request");
    }
    std::lock_guard<std::mutex> lk(netMtx_);
    for (auto* p : req->NetworkProtocols) {
        if (!p) continue;
        int type = static_cast<int>(p->Name);
        // Reject enum values ngoài HTTP/HTTPS/RTSP (0..2) — SetNetworkProtocols
        // - UNSUPPORTED PROTOCOLS test yêu cầu fault ActionNotSupported.
        // Nhưng gSOAP enum đã filter — nếu tool gửi type khác, gSOAP deserialize fail trước.
        for (auto& cur : net_.protocols) {
            if (cur.type == type) {
                cur.enabled = p->Enabled;
                if (!p->Port.empty()) cur.port = p->Port[0];
            }
        }
    }
    return SOAP_OK;
}

// ══════════════════════════════════════════════════════════════════════════
// System (Profile T mục 7.5)
// ══════════════════════════════════════════════════════════════════════════

int DeviceService::SetSystemDateAndTime(_tds__SetSystemDateAndTime* req,
                                        _tds__SetSystemDateAndTimeResponse& resp) {
    (void)resp;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!req) {
        return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                 "ter:InvalidArgVal", nullptr,
                                 "Missing request");
    }
    // Validate: nếu Manual, UTCDateTime bắt buộc.
    if (req->DateTimeType == tt__SetDateTimeType::Manual && !req->UTCDateTime) {
        return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                 "ter:InvalidArgVal", "ter:MissingAttribute",
                                 "UTCDateTime required for Manual");
    }
    // Validate timezone (nếu có): POSIX TZ tối thiểu cần có digit (offset) hoặc
    // dấu phẩy (rule). "INVALIDTIMEZONE" (toàn chữ) không hợp lệ.
    if (req->TimeZone) {
        const std::string& tz = req->TimeZone->TZ;
        bool hasDigit = false, hasComma = false;
        for (char c : tz) {
            if (std::isdigit((unsigned char)c)) hasDigit = true;
            if (c == ',') hasComma = true;
            if (!(std::isalnum((unsigned char)c) || c=='+'||c=='-'||c==':'||c=='/'||c=='.'||c==','))
                return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                         "ter:InvalidArgVal", "ter:InvalidTimeZone",
                                         "Invalid timezone format");
        }
        if (tz.empty() || (!hasDigit && !hasComma)) {
            return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                     "ter:InvalidArgVal", "ter:InvalidTimeZone",
                                     "Invalid POSIX TZ format");
        }
    }
    // Validate date + time (nếu Manual)
    if (req->DateTimeType == tt__SetDateTimeType::Manual && req->UTCDateTime) {
        if (req->UTCDateTime->Date) {
            auto* d = req->UTCDateTime->Date;
            if (d->Month < 1 || d->Month > 12 || d->Day < 1 || d->Day > 31
                || d->Year < 1970 || d->Year > 2099) {
                return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                         "ter:InvalidArgVal", "ter:InvalidDateTime",
                                         "Invalid date");
            }
        }
        if (req->UTCDateTime->Time) {
            auto* t = req->UTCDateTime->Time;
            if (t->Hour < 0 || t->Hour > 23 || t->Minute < 0 || t->Minute > 59
                || t->Second < 0 || t->Second > 60) {
                return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                         "ter:InvalidArgVal", "ter:InvalidDateTime",
                                         "Invalid time");
            }
        }
    }
    std::lock_guard<std::mutex> lk(sysMtx_);
    sys_.ntpEnabled = (req->DateTimeType == tt__SetDateTimeType::NTP);
    // Không thực sự set system clock (mock).
    return SOAP_OK;
}

int DeviceService::SetSystemFactoryDefault(_tds__SetSystemFactoryDefault* req,
                                           _tds__SetSystemFactoryDefaultResponse& resp) {
    (void)req; (void)resp;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    std::cout << "[DeviceService] SetSystemFactoryDefault ack (mock)\n";
    // Giả lập reboot: phát Bye + Hello ra multicast để test tool nhận diện
    // (DISCOVERY-1-1-2/1-1-8, DEVICE-3-1-6/3-1-7).
    if (auto* d = DiscoveryService::current()) d->announceReboot();
    return SOAP_OK;
}

int DeviceService::SystemReboot(_tds__SystemReboot* req,
                                _tds__SystemRebootResponse& resp) {
    (void)req;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    resp.Message = "Rebooting in 5 seconds";
    std::cout << "[DeviceService] SystemReboot ack (mock)\n";
    // Giả lập reboot: phát Bye + Hello (test tool chờ Hello sau SystemReboot).
    if (auto* d = DiscoveryService::current()) d->announceReboot();
    return SOAP_OK;
}

// ══════════════════════════════════════════════════════════════════════════
// User handling (Profile T mục 7.6)
// ══════════════════════════════════════════════════════════════════════════

int DeviceService::CreateUsers(_tds__CreateUsers* req,
                               _tds__CreateUsersResponse& resp) {
    (void)resp;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!req || req->User.empty()) {
        return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                 "ter:InvalidArgVal", nullptr,
                                 "No users provided");
    }
    std::lock_guard<std::mutex> lk(sysMtx_);
    for (auto* u : req->User) {
        if (!u || u->Username.empty()) {
            return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                     "ter:InvalidArgVal", "ter:UsernameMissing",
                                     "Empty username");
        }
        // Password bắt buộc trong ONVIF (trừ Anonymous)
        if (u->UserLevel != tt__UserLevel::Anonymous && !u->Password) {
            return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                     "ter:InvalidArgVal", "ter:PasswordMissing",
                                     "Password required");
        }
        // Kiểm tra trùng
        for (const auto& ex : sys_.users) {
            if (ex.username == u->Username) {
                return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                         "ter:OperationProhibited",
                                         "ter:UsernameClash",
                                         "Username already exists");
            }
        }
        MockUser mu;
        mu.username = u->Username;
        mu.password = u->Password ? *u->Password : "";
        mu.level = static_cast<int>(u->UserLevel);
        sys_.users.push_back(mu);
    }
    return SOAP_OK;
}

int DeviceService::DeleteUsers(_tds__DeleteUsers* req,
                               _tds__DeleteUsersResponse& resp) {
    (void)resp;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!req || req->Username.empty()) {
        return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                 "ter:InvalidArgVal", nullptr,
                                 "No username provided");
    }
    std::lock_guard<std::mutex> lk(sysMtx_);
    // Atomic: validate TẤT CẢ trước, delete sau. Nếu bất kỳ user nào sai,
    // reject cả lệnh — không delete gì (DEVICE-4-1-5 error case).
    int adminCount = 0;
    for (const auto& u : sys_.users)
        if (u.level == 0) ++adminCount;

    std::vector<size_t> toDelete;
    for (const auto& name : req->Username) {
        auto it = std::find_if(sys_.users.begin(), sys_.users.end(),
                               [&](const MockUser& u){ return u.username == name; });
        if (it == sys_.users.end()) {
            std::string msg = "User not found: " + name;
            return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                     "ter:InvalidArgVal", "ter:UsernameMissing",
                                     msg.c_str());
        }
        if (it->level == 0) {
            if (--adminCount < 1) {
                return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                         "ter:OperationProhibited",
                                         "ter:FixedUser",
                                         "Cannot delete last admin");
            }
        }
        toDelete.push_back(std::distance(sys_.users.begin(), it));
    }
    // Delete descending để index không thay đổi
    std::sort(toDelete.rbegin(), toDelete.rend());
    for (size_t idx : toDelete) sys_.users.erase(sys_.users.begin() + idx);
    return SOAP_OK;
}

int DeviceService::SetUser(_tds__SetUser* req,
                           _tds__SetUserResponse& resp) {
    (void)resp;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!req || req->User.empty()) {
        return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                 "ter:InvalidArgVal", nullptr,
                                 "No users provided");
    }
    std::lock_guard<std::mutex> lk(sysMtx_);
    // Atomic: validate all trước, apply sau (DEVICE-4-1-8 error case).
    std::vector<size_t> targets;
    for (auto* u : req->User) {
        if (!u || u->Username.empty()) {
            return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                     "ter:InvalidArgVal", "ter:UsernameMissing",
                                     "Empty username");
        }
        auto it = std::find_if(sys_.users.begin(), sys_.users.end(),
                               [&](const MockUser& mu){ return mu.username == u->Username; });
        if (it == sys_.users.end()) {
            std::string msg = "User not found: " + u->Username;
            return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                     "ter:InvalidArgVal", "ter:UsernameMissing",
                                     msg.c_str());
        }
        targets.push_back(std::distance(sys_.users.begin(), it));
    }
    // Apply
    for (size_t i = 0; i < req->User.size(); ++i) {
        auto* u = req->User[i];
        auto& mu = sys_.users[targets[i]];
        if (u->Password) mu.password = *u->Password;
        mu.level = static_cast<int>(u->UserLevel);
    }
    return SOAP_OK;
}

// ── GetWsdlUrl (Profile T §7.2 mandatory) ─────────────────────────────────
int DeviceService::GetWsdlUrl(_tds__GetWsdlUrl* req, _tds__GetWsdlUrlResponse& resp) {
    (void)req;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    resp.WsdlUrl = "http://" + cfg_.deviceIp + ":" + std::to_string(cfg_.httpPort)
                 + "/wsdl/";
    return SOAP_OK;
}
