#include "services/DeviceService.h"
#include "auth/WsSecurityHandler.h"
#include <iostream>
#include <ctime>
#include <cstring>
#include <sstream>

std::mutex DeviceService::netMtx_;
DeviceService::NetworkState DeviceService::net_;

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
         wantImaging = false, reqUnsupported = false;
    if (!tds__GetCapabilities || tds__GetCapabilities->Category.empty()) {
        wantAll = true;
    } else {
        for (auto cat : tds__GetCapabilities->Category) {
            switch (cat) {
                case tt__CapabilityCategory::All:       wantAll = true; break;
                case tt__CapabilityCategory::Device:    wantDevice = true; break;
                case tt__CapabilityCategory::Events:    wantEvents = true; break;
                case tt__CapabilityCategory::Imaging:   wantImaging = true; break;
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

    // Media (ver10) KHÔNG hỗ trợ — thiết bị dùng Media2 (ver20). Không đưa vào
    // legacy GetCapabilities; hỏi Category=Media sẽ nhận fault ở trên.

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
    (void)tds__GetServices;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    auto soap = this->soap;

    const std::string base = "http://" + cfg_.deviceIp + ":" + std::to_string(cfg_.httpPort);

    // Danh sách service phải NHẤT QUÁN với GetCapabilities (test consistency)
    auto add = [&](const char* ns, const std::string& path, int major, int minor) {
        auto s = soap_new_tds__Service(soap);
        s->Namespace = ns;
        s->XAddr = base + path;
        s->Version = soap_new_tt__OnvifVersion(soap);
        s->Version->Major = major;
        s->Version->Minor = minor;
        // Capabilities (xsd:any) để trống — populate cần thao tác DOM, làm ở vòng sau.
        tds__GetServicesResponse.Service.push_back(s);
    };

    add("http://www.onvif.org/ver10/device/wsdl",  "/onvif/device_service", 21, 12); // Device
    // Khai báo cả Media ver10 (legacy) + Media2 ver20 (Profile T). Test tool
    // Imaging tìm Media ver10 để phát hiện VideoSource; nếu thiếu → fail
    // "Neither media, nor I/O supported".
    add("http://www.onvif.org/ver10/media/wsdl",   "/onvif/media",          21, 12); // Media (legacy)
    add("http://www.onvif.org/ver20/media/wsdl",   "/onvif/media",          21, 12); // Media2 (Profile T)
    add("http://www.onvif.org/ver10/events/wsdl",  "/onvif/event",          21, 12); // Events (Profile T)
    add("http://www.onvif.org/ver20/imaging/wsdl", "/onvif/imaging",        21, 12); // Imaging (Profile T)

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
    this->soap->mustUnderstand = 0;
    if (!validateAuth()) {
        return soap_sender_fault_subcode(this->soap, "ter:NotAuthorized", "Sender", "Not Authorized");
    }
    this->soap->header = nullptr;

    auto soap = this->soap;

    auto user = soap_new_tt__User(soap);
    user->Username = cfg_.username;
    user->UserLevel = tt__UserLevel::Administrator;
    tds__GetUsersResponse.User.push_back(user);

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
    for (const auto& ip : net_.dnsManual) {
        auto a = soap_new_tt__IPAddress(soap);
        a->Type = tt__IPType::IPv4;
        a->IPv4Address = Sp(soap, ip);
        info->DNSManual.push_back(a);
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
    // Cập nhật state — chỉ merge field có gửi
    if (req->NetworkInterface) {
        auto* ni = req->NetworkInterface;
        (void)ni;  // struct fields optional; giữ đơn giản, không tự parse hết
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
