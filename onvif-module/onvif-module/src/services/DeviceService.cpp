#include "services/DeviceService.h"
#include "services/DiscoveryService.h"
#include "auth/WsSecurityHandler.h"
#include "backend/IMgmtClient.h"
#include <iostream>
#include <ctime>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iomanip>

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

// Kiểm tra dạng IPv4 dotted-quad đơn giản, dùng để chọn Type cho tt__NetworkHost
// (NTP server có thể là IP hoặc hostname; MGMT lưu chung một string, không
// phân biệt loại).
static bool isIPv4Address(const std::string& s) {
    int groups = 0, value = 0, digits = 0;
    for (std::size_t i = 0; i <= s.size(); ++i) {
        if (i == s.size() || s[i] == '.') {
            if (digits == 0 || digits > 3 || value > 255) return false;
            ++groups; value = 0; digits = 0;
        } else if (std::isdigit((unsigned char)s[i])) {
            value = value * 10 + (s[i] - '0');
            ++digits;
        } else {
            return false;
        }
    }
    return groups == 4;
}

// Cùng luật với DateTimeService::validHost() phía MGMT (chỉ alnum/._-:),
// chặn input rõ ràng sai trước khi gọi MGMT thay vì để MGMT trả result=-1
// mập mờ.
static bool isValidNtpHost(const std::string& h) {
    if (h.empty() || h.size() > 253) return false;
    if (!std::isalnum((unsigned char)h.front())) return false;
    for (char c : h) {
        if (!(std::isalnum((unsigned char)c) || c == '_' || c == '.' ||
              c == ':' || c == '-'))
            return false;
    }
    return true;
}

static bool isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

// Số ngày thật của tháng — ONVIF/MGMT chỉ check range 1-31 thô, không đủ để
// chặn ngày không tồn tại (VD 30/02). Nếu lọt xuống MGMT, lệnh `timedatectl
// set-time` sẽ fail và MGMT trả {"result":0} không kèm lý do, không thể phân
// biệt với lỗi hệ thống thật — nên phải chặn ở đây trước khi gọi backend.
static int daysInMonth(int year, int month) {
    static const int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    if (month == 2 && isLeapYear(year)) return 29;
    return kDays[month - 1];
}

// Ngược lại với công thức compose TimeZone ở GetSystemDateAndTime (chuỗi
// "UTC" + dấu + giờ[:phút], dấu '-' ứng với offset dương). Đây là quy ước tự
// định nghĩa của riêng service này (không phải mọi client POSIX TZ đều theo
// đúng hình thức này), nhưng Get/Set trong cùng service phải nhất quán với
// nhau để round-trip Get→Set hoạt động đúng.
static bool parsePosixOffsetMinutes(const std::string& tz, int& outMinutes) {
    if (tz.rfind("UTC", 0) != 0) return false;
    const std::string rest = tz.substr(3);
    if (rest.empty()) return false;
    if (rest == "0") { outMinutes = 0; return true; }
    const char sign = rest[0];
    if (sign != '+' && sign != '-') return false;
    const std::string numPart = rest.substr(1);
    if (numPart.empty()) return false;
    for (char c : numPart) {
        if (c != ':' && !std::isdigit((unsigned char)c)) return false;
    }
    const auto colon = numPart.find(':');
    int hours = 0, minutes = 0;
    try {
        if (colon == std::string::npos) {
            hours = std::stoi(numPart);
        } else {
            hours = std::stoi(numPart.substr(0, colon));
            minutes = std::stoi(numPart.substr(colon + 1));
        }
    } catch (...) {
        return false;
    }
    if (hours < 0 || hours > 14 || minutes < 0 || minutes > 59) return false;
    const int total = hours * 60 + minutes;
    outMinutes = (sign == '-') ? total : -total;
    return true;
}

static std::int64_t civilMinutes(int year, unsigned month, unsigned day,
                                 int hour, int minute) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yearOfEra = static_cast<unsigned>(year - era * 400);
    const unsigned adjustedMonth = month > 2 ? month - 3 : month + 9;
    const unsigned dayOfYear = (153 * adjustedMonth + 2) / 5 + day - 1;
    const unsigned dayOfEra = yearOfEra * 365 + yearOfEra / 4 -
                              yearOfEra / 100 + dayOfYear;
    const std::int64_t days = static_cast<std::int64_t>(era) * 146097 +
                              static_cast<std::int64_t>(dayOfEra);
    return days * 24 * 60 + hour * 60 + minute;
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

    const SystemDateTime dt = backend_->getSystemDateAndTime();

    // Allocate response struct using soap memory manager
    auto sdt = soap_new_tt__SystemDateTime(soap);
    sdt->DateTimeType = dt.dateTimeType == "NTP"
        ? tt__SetDateTimeType::NTP : tt__SetDateTimeType::Manual;
    sdt->DaylightSavings = dt.daylightSaving;

    // ONVIF expects POSIX TZ syntax. Derive the current UTC offset from the
    // MGMT-provided UTC/local values instead of hardcoding UTC0.
    const auto utcMinutes = civilMinutes(dt.year, dt.month, dt.day,
                                         dt.hour, dt.minute);
    const auto localMinutes = civilMinutes(dt.localYear, dt.localMonth,
                                           dt.localDay, dt.localHour,
                                           dt.localMinute);
    const int offsetMinutes = static_cast<int>(localMinutes - utcMinutes);
    if (offsetMinutes < -14 * 60 || offsetMinutes > 14 * 60)
        throw std::runtime_error("MGMT returned invalid UTC/local offset");
    std::ostringstream posixTz;
    posixTz << "UTC";
    if (offsetMinutes != 0) {
        posixTz << (offsetMinutes > 0 ? '-' : '+');
        const int absolute = std::abs(offsetMinutes);
        posixTz << absolute / 60;
        if (absolute % 60)
            posixTz << ':' << std::setw(2) << std::setfill('0') << absolute % 60;
    } else {
        posixTz << '0';
    }
    sdt->TimeZone = soap_new_tt__TimeZone(soap);
    sdt->TimeZone->TZ = posixTz.str();

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

    // LocalDateTime
    sdt->LocalDateTime = soap_new_tt__DateTime(soap);
    sdt->LocalDateTime->Date = soap_new_tt__Date(soap);
    sdt->LocalDateTime->Date->Year = dt.localYear;
    sdt->LocalDateTime->Date->Month = dt.localMonth;
    sdt->LocalDateTime->Date->Day = dt.localDay;
    sdt->LocalDateTime->Time = soap_new_tt__Time(soap);
    sdt->LocalDateTime->Time->Hour = dt.localHour;
    sdt->LocalDateTime->Time->Minute = dt.localMinute;
    sdt->LocalDateTime->Time->Second = dt.localSecond;

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
        return soap_receiver_fault_subcode(
            this->soap,
            "\"http://www.onvif.org/ver10/error\":Action",
            "Device information backend unavailable",
            nullptr);
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
         wantMedia = false, wantImaging = false, wantAnalytics = false,
         reqUnsupported = false;
    if (!tds__GetCapabilities || tds__GetCapabilities->Category.empty()) {
        wantAll = true;
    } else {
        for (auto cat : tds__GetCapabilities->Category) {
            switch (cat) {
                case tt__CapabilityCategory::All:       wantAll = true; break;
                case tt__CapabilityCategory::Device:    wantDevice = true; break;
                case tt__CapabilityCategory::Events:    wantEvents = true; break;
                case tt__CapabilityCategory::Imaging:   wantImaging = true; break;
                // Media1 declared cho Profile S — Category=Media trả caps.
                case tt__CapabilityCategory::Media:     wantMedia = true; break;
                case tt__CapabilityCategory::PTZ:       reqUnsupported = true; break;
                // Analytics declared cho Profile M (§7.10 Analytics Module).
                case tt__CapabilityCategory::Analytics: wantAnalytics = true; break;
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

    // ── Analytics (Profile M §7.10 Analytics Module) ─────────────────────
    if (wantAll || wantAnalytics) {
        caps->Analytics = soap_new_tt__AnalyticsCapabilities(soap);
        caps->Analytics->XAddr = base + "/onvif/analytics";
        // RuleSupport=false: KHÔNG implement Rule engine (§8.6 conditional) →
        // tool skip Rules/recognition tests. AnalyticsModuleSupport=true (§7.10).
        caps->Analytics->RuleSupport            = false;
        caps->Analytics->AnalyticsModuleSupport = true;
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
           << " xmlns:tan=\"http://www.onvif.org/ver20/analytics/wsdl\""
           << " xmlns:trc=\"http://www.onvif.org/ver10/recording/wsdl\""
           << " xmlns:tse=\"http://www.onvif.org/ver10/search/wsdl\""
           << " xmlns:trp=\"http://www.onvif.org/ver10/replay/wsdl\""
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
               "ConfigurationsSupported=\"VideoSource VideoEncoder Metadata Analytics\"/>"
              "<tr2:StreamingCapabilities RTSPStreaming=\"true\" RTPMulticast=\"true\" "
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

        // Analytics (Profile M mandatory §7.6/7.10 — GetSupportedMetadata,
        // GetSupportedAnalyticsModules...). RuleSupport/AnalyticsModuleSupport.
        std::string anCaps =
            "<tan:Capabilities RuleSupport=\"false\" AnalyticsModuleSupport=\"true\" "
             "CellBasedSceneDescriptionSupported=\"false\" "
             "AnalyticsModuleOptionsSupported=\"true\" SupportedMetadata=\"true\"/>";
        svc("http://www.onvif.org/ver20/analytics/wsdl", "/onvif/analytics",
            21, 12, anCaps);

        // Recording Control (Profile G) — non-dynamic, 1 recording dựng sẵn, H264.
        std::string recCaps =
            "<trc:Capabilities DynamicRecordings=\"false\" DynamicTracks=\"false\" "
             "DeleteData=\"false\" Encoding=\"H264\" MaxRate=\"20000\" "
             "MaxTotalRate=\"20000\" MaxRecordings=\"1\" MaxRecordingJobs=\"1\" "
             "Options=\"true\"/>";
        svc("http://www.onvif.org/ver10/recording/wsdl", "/onvif/recording",
            21, 12, recCaps);

        // Recording Search (Profile G).
        std::string seaCaps =
            "<tse:Capabilities MetadataSearch=\"false\" GeneralStartEvents=\"true\"/>";
        svc("http://www.onvif.org/ver10/search/wsdl", "/onvif/search",
            21, 12, seaCaps);

        // Replay Control (Profile G) — bỏ reverse playback.
        std::string repCaps =
            "<trp:Capabilities ReversePlayback=\"false\" "
             "SessionTimeoutRange=\"0 4294967295\" RTP_RTSP_TCP=\"true\"/>";
        svc("http://www.onvif.org/ver10/replay/wsdl", "/onvif/replay",
            21, 12, repCaps);

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
    add("http://www.onvif.org/ver20/analytics/wsdl", "/onvif/analytics",     21, 12); // Analytics (Profile M)
    add("http://www.onvif.org/ver10/recording/wsdl", "/onvif/recording",     21, 12); // Recording (Profile G)
    add("http://www.onvif.org/ver10/search/wsdl",    "/onvif/search",        21, 12); // Search (Profile G)
    add("http://www.onvif.org/ver10/replay/wsdl",    "/onvif/replay",        21, 12); // Replay (Profile G)

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
        // `tds__SystemCapabilities` in current gSOAP/ONVIF schemas names
        // this optional capability HttpFirmwareUpgrade (not FirmwareUpgrade).
        caps->System->HttpFirmwareUpgrade = firmwareUpgrade;
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

    HostnameConfig cfg;
    try {
        cfg = backend_->getHostname();
    } catch (const std::exception& e) {
        std::cerr << "[DeviceService] GetHostname backend error: " << e.what() << std::endl;
        return soap_receiver_fault_subcode(
            this->soap, "\"http://www.onvif.org/ver10/error\":Action",
            "Hostname backend unavailable", nullptr);
    }

    auto info = soap_new_tt__HostnameInformation(soap);
    info->FromDHCP = cfg.fromDhcp;
    info->Name = Sp(soap, cfg.name);
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
    HostnameConfig cfg;
    cfg.name = req->Name;
    cfg.fromDhcp = false;
    try {
        backend_->setHostname(cfg);
    } catch (const MgmtValidationError& e) {
        return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                 "ter:InvalidArgVal", "ter:InvalidHostname", e.what());
    } catch (const std::exception& e) {
        std::cerr << "[DeviceService] SetHostname backend error: " << e.what() << std::endl;
        return soap_receiver_fault_subcode(
            this->soap, "\"http://www.onvif.org/ver10/error\":Action",
            "SetHostname backend unavailable", nullptr);
    }
    return SOAP_OK;
}

// ── GetDNS / SetDNS ─────────────────────────────────────────────────────────
// Chỉ IPv4 (xem 01-IMPLEMENTATION_PLAN.md mục 2.1 — Profile T không bắt buộc
// IPv6). MGMT có hỗ trợ IPv6 thật nhưng cố tình bỏ qua ở đây.
int DeviceService::GetDNS(_tds__GetDNS* req, _tds__GetDNSResponse& resp) {
    (void)req;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    auto soap = this->soap;

    DnsConfig cfg;
    try {
        cfg = backend_->getDns();
    } catch (const std::exception& e) {
        std::cerr << "[DeviceService] GetDNS backend error: " << e.what() << std::endl;
        return soap_receiver_fault_subcode(
            this->soap, "\"http://www.onvif.org/ver10/error\":Action",
            "DNS backend unavailable", nullptr);
    }

    auto info = soap_new_tt__DNSInformation(soap);
    info->FromDHCP = cfg.fromDhcp;
    info->SearchDomain = cfg.searchDomain;
    auto addIp = [&](const std::string& ip) {
        if (ip.empty()) return;
        auto a = soap_new_tt__IPAddress(soap);
        a->Type = tt__IPType::IPv4;
        a->IPv4Address = Sp(soap, ip);
        if (cfg.fromDhcp) info->DNSFromDHCP.push_back(a);
        else info->DNSManual.push_back(a);
    };
    addIp(cfg.primaryDns);
    addIp(cfg.secondaryDns);
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
    DnsConfig cfg;
    cfg.fromDhcp = req->FromDHCP;
    cfg.searchDomain = req->SearchDomain;
    for (auto* a : req->DNSManual) {
        if (!a || !a->IPv4Address) continue;
        if (cfg.primaryDns.empty()) cfg.primaryDns = *a->IPv4Address;
        else if (cfg.secondaryDns.empty()) cfg.secondaryDns = *a->IPv4Address;
    }
    try {
        backend_->setDns(cfg);
    } catch (const MgmtValidationError& e) {
        return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                 "ter:InvalidArgVal", nullptr, e.what());
    } catch (const std::exception& e) {
        std::cerr << "[DeviceService] SetDNS backend error: " << e.what() << std::endl;
        return soap_receiver_fault_subcode(
            this->soap, "\"http://www.onvif.org/ver10/error\":Action",
            "SetDNS backend unavailable", nullptr);
    }
    return SOAP_OK;
}

// ── GetNetworkInterfaces / SetNetworkInterfaces ─────────────────────────────
// Chỉ IPv4 (mục 2.1). SetNetworkInterfaces: MGMT trả result=1 ngay rồi apply
// thật ở background thread — không có cách xác nhận apply thành công đồng bộ
// qua chính response này (giới hạn đã ghi trong 01-IMPLEMENTATION_PLAN.md).
int DeviceService::GetNetworkInterfaces(_tds__GetNetworkInterfaces* req,
                                        _tds__GetNetworkInterfacesResponse& resp) {
    (void)req;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    auto soap = this->soap;

    NetworkInterfaceConfig cfg;
    try {
        cfg = backend_->getNetworkInterface();
    } catch (const std::exception& e) {
        std::cerr << "[DeviceService] GetNetworkInterfaces backend error: " << e.what() << std::endl;
        return soap_receiver_fault_subcode(
            this->soap, "\"http://www.onvif.org/ver10/error\":Action",
            "Network interface backend unavailable", nullptr);
    }

    auto iface = soap_new_tt__NetworkInterface(soap);
    iface->token = cfg.token;
    iface->Enabled = cfg.enabled;

    iface->Info = soap_new_tt__NetworkInterfaceInfo(soap);
    iface->Info->Name = Sp(soap, cfg.name);
    iface->Info->HwAddress = cfg.hwAddress;
    auto* mtu = (int*)soap_malloc(soap, sizeof(int));
    *mtu = cfg.mtu;
    iface->Info->MTU = mtu;

    iface->IPv4 = soap_new_tt__IPv4NetworkInterface(soap);
    iface->IPv4->Enabled = cfg.ipv4Enabled;
    iface->IPv4->Config = soap_new_tt__IPv4Configuration(soap);
    iface->IPv4->Config->DHCP = cfg.dhcp;
    auto* manual = soap_new_tt__PrefixedIPv4Address(soap);
    manual->Address = cfg.address;
    manual->PrefixLength = cfg.prefixLength;
    iface->IPv4->Config->Manual.push_back(manual);
    // IPCONFIG-1-1-3: khi DHCP=true, tool expect FromDHCP field có địa chỉ.
    if (cfg.dhcp) {
        auto* fromDhcp = soap_new_tt__PrefixedIPv4Address(soap);
        fromDhcp->Address = cfg.address;
        fromDhcp->PrefixLength = cfg.prefixLength;
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
    NetworkInterfaceConfig cfg;
    try {
        cfg = backend_->getNetworkInterface();
    } catch (const std::exception& e) {
        std::cerr << "[DeviceService] SetNetworkInterfaces: cannot read current state: "
                  << e.what() << std::endl;
        return soap_receiver_fault_subcode(
            this->soap, "\"http://www.onvif.org/ver10/error\":Action",
            "Network interface backend unavailable", nullptr);
    }
    if (req->InterfaceToken != cfg.token) {
        return soap_sender_fault_subcode(this->soap, "ter:InvalidArgVal",
                                         "Sender", "Unknown InterfaceToken");
    }
    // Merge field có gửi (DEVICE-2-1-18 verify appliance) lên state hiện tại.
    if (req->NetworkInterface) {
        auto* ni = req->NetworkInterface;
        if (ni->Enabled) cfg.enabled = *ni->Enabled;
        if (ni->MTU)     cfg.mtu = *ni->MTU;
        if (ni->IPv4) {
            auto* v4 = ni->IPv4;
            if (v4->DHCP) cfg.dhcp = *v4->DHCP;
            if (!v4->Manual.empty() && v4->Manual[0]) {
                cfg.address = v4->Manual[0]->Address;
                cfg.prefixLength = v4->Manual[0]->PrefixLength;
            }
        }
    }
    try {
        backend_->setNetworkInterface(cfg);
    } catch (const MgmtValidationError& e) {
        return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                 "ter:InvalidArgVal", nullptr, e.what());
    } catch (const std::exception& e) {
        std::cerr << "[DeviceService] SetNetworkInterfaces backend error: " << e.what() << std::endl;
        return soap_receiver_fault_subcode(
            this->soap, "\"http://www.onvif.org/ver10/error\":Action",
            "SetNetworkInterfaces backend unavailable", nullptr);
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
    NetworkGatewayConfig cfg;
    try {
        cfg = backend_->getNetworkGateway();
    } catch (const std::exception& e) {
        std::cerr << "[DeviceService] GetNetworkDefaultGateway backend error: " << e.what() << std::endl;
        return soap_receiver_fault_subcode(
            this->soap, "\"http://www.onvif.org/ver10/error\":Action",
            "Network gateway backend unavailable", nullptr);
    }
    auto gw = soap_new_tt__NetworkGateway(soap);
    if (!cfg.ipv4Address.empty()) gw->IPv4Address.push_back(cfg.ipv4Address);
    resp.NetworkGateway = gw;
    return SOAP_OK;
}

int DeviceService::SetNetworkDefaultGateway(_tds__SetNetworkDefaultGateway* req,
                                            _tds__SetNetworkDefaultGatewayResponse& resp) {
    (void)resp;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!req || req->IPv4Address.empty()) {
        return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                 "ter:InvalidArgVal", nullptr,
                                 "IPv4Address is required");
    }
    NetworkGatewayConfig cfg;
    cfg.ipv4Address = req->IPv4Address.front();
    try {
        backend_->setNetworkGateway(cfg);
    } catch (const MgmtValidationError& e) {
        return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                 "ter:InvalidArgVal", nullptr, e.what());
    } catch (const std::exception& e) {
        std::cerr << "[DeviceService] SetNetworkDefaultGateway backend error: " << e.what() << std::endl;
        return soap_receiver_fault_subcode(
            this->soap, "\"http://www.onvif.org/ver10/error\":Action",
            "SetNetworkDefaultGateway backend unavailable", nullptr);
    }
    return SOAP_OK;
}

// ── GetNetworkProtocols / SetNetworkProtocols ───────────────────────────────
// MGMT quản lý port SOAP ONVIF dưới tên protocol "ONVIF" (khác hẳn MGMT
// "HTTP", vốn là port web UI MGMT 8086) — map "ONVIF" của MGMT thành "HTTP"
// của SOAP, vì đó chính là transport thật SOAP đang chạy. "HTTPS" luôn công
// bố disabled vì onvif-module chưa hỗ trợ TLS (README nguyên tắc #5: không
// quảng cáo capability backend thật không làm được).
//
// Giới hạn đã biết: đổi port "ONVIF" qua SetNetworkProtocols chỉ ghi xuống
// MGMT (persist), KHÔNG tự rebind listener đang chạy của onvif-module —
// cần restart thủ công để port mới có hiệu lực thật (xem
// 01-IMPLEMENTATION_PLAN.md mục Network configuration).
int DeviceService::GetNetworkProtocols(_tds__GetNetworkProtocols* req,
                                       _tds__GetNetworkProtocolsResponse& resp) {
    (void)req;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    auto soap = this->soap;

    std::vector<NetworkProtocolEntry> protocols;
    try {
        protocols = backend_->getNetworkProtocols();
    } catch (const std::exception& e) {
        std::cerr << "[DeviceService] GetNetworkProtocols backend error: " << e.what() << std::endl;
        return soap_receiver_fault_subcode(
            this->soap, "\"http://www.onvif.org/ver10/error\":Action",
            "Network protocols backend unavailable", nullptr);
    }

    bool foundHttp = false, foundRtsp = false;
    int httpsPort = 0; // lấy từ MGMT nếu có, không tự bịa số
    for (const auto& p : protocols) {
        if (p.name == "ONVIF") {
            auto np = soap_new_tt__NetworkProtocol(soap);
            np->Name = tt__NetworkProtocolType::HTTP;
            // Luôn true: SOAP server này đang thực sự phục vụ request này.
            np->Enabled = true;
            np->Port.push_back(p.port);
            resp.NetworkProtocols.push_back(np);
            foundHttp = true;
        } else if (p.name == "RTSP") {
            auto np = soap_new_tt__NetworkProtocol(soap);
            np->Name = tt__NetworkProtocolType::RTSP;
            np->Enabled = p.enabled;
            np->Port.push_back(p.port);
            resp.NetworkProtocols.push_back(np);
            foundRtsp = true;
        } else if (p.name == "HTTPS" && p.port > 0) {
            // MGMT trả port HTTPS thật (port web UI MGMT, không phải port
            // SOAP) — dùng đúng giá trị này thay vì đoán/hardcode.
            httpsPort = p.port;
        }
    }
    // MGMT chưa có/thiếu entry tương ứng -> fallback đúng port runtime thật
    // của process này thay vì bỏ trống (bắt buộc phải công bố HTTP/RTSP).
    if (!foundHttp) {
        auto np = soap_new_tt__NetworkProtocol(soap);
        np->Name = tt__NetworkProtocolType::HTTP;
        np->Enabled = true;
        np->Port.push_back(cfg_.httpPort);
        resp.NetworkProtocols.push_back(np);
    }
    if (!foundRtsp) {
        auto np = soap_new_tt__NetworkProtocol(soap);
        np->Name = tt__NetworkProtocolType::RTSP;
        np->Enabled = true;
        np->Port.push_back(cfg_.rtspPort);
        resp.NetworkProtocols.push_back(np);
    }
    auto https = soap_new_tt__NetworkProtocol(soap);
    https->Name = tt__NetworkProtocolType::HTTPS;
    // Luôn false: onvif-module chưa hỗ trợ TLS thật cho SOAP, bất kể MGMT
    // báo port HTTPS của web UI đang bật hay không (2 khái niệm khác nhau).
    https->Enabled = false;
    // Schema ONVIF bắt buộc Port dù Enabled=false (DEVICE-2-1-33 báo
    // "incomplete content... expected Port" khi thiếu). Dùng port HTTPS
    // thật MGMT trả (nếu có); 443 chỉ là fallback cuối cùng khi MGMT không
    // trả entry HTTPS nào.
    https->Port.push_back(httpsPort > 0 ? httpsPort : 443);
    resp.NetworkProtocols.push_back(https);
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
    std::vector<NetworkProtocolEntry> updates;
    for (auto* p : req->NetworkProtocols) {
        if (!p) continue;
        if (p->Name == tt__NetworkProtocolType::HTTPS) {
            if (p->Enabled) {
                // DEVICE-2-1-35 style: không hỗ trợ thật -> ActionNotSupported.
                return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                         "ter:ActionNotSupported", nullptr,
                                         "HTTPS is not supported by this device");
            }
            continue; // đã disabled sẵn, không cần gửi gì xuống MGMT
        }
        NetworkProtocolEntry entry;
        entry.enabled = p->Enabled;
        entry.port = p->Port.empty() ? 0 : p->Port[0];
        if (p->Name == tt__NetworkProtocolType::HTTP) entry.name = "ONVIF";
        else if (p->Name == tt__NetworkProtocolType::RTSP) entry.name = "RTSP";
        else continue;
        updates.push_back(entry);
    }
    if (updates.empty()) return SOAP_OK;
    try {
        backend_->setNetworkProtocols(updates);
    } catch (const MgmtValidationError& e) {
        return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                 "ter:InvalidArgVal", nullptr, e.what());
    } catch (const std::exception& e) {
        std::cerr << "[DeviceService] SetNetworkProtocols backend error: " << e.what() << std::endl;
        return soap_receiver_fault_subcode(
            this->soap, "\"http://www.onvif.org/ver10/error\":Action",
            "SetNetworkProtocols backend unavailable", nullptr);
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
    const bool isManual = req->DateTimeType == tt__SetDateTimeType::Manual;
    // Validate: nếu Manual, UTCDateTime (đủ cả Date lẫn Time) bắt buộc — thiếu
    // một trong hai thì không đủ dữ liệu để gọi timedatectl set-time.
    if (isManual && (!req->UTCDateTime || !req->UTCDateTime->Date || !req->UTCDateTime->Time)) {
        return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                 "ter:InvalidArgVal", "ter:MissingAttribute",
                                 "UTCDateTime (Date and Time) required for Manual");
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
    // Validate date + time (nếu Manual): range thô trước, rồi số ngày thật
    // của tháng/năm (chặn 30/02 v.v. trước khi tới MGMT).
    if (isManual) {
        auto* d = req->UTCDateTime->Date;
        auto* t = req->UTCDateTime->Time;
        if (d->Month < 1 || d->Month > 12 || d->Day < 1 || d->Day > 31
            || d->Year < 1970 || d->Year > 2099) {
            return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                     "ter:InvalidArgVal", "ter:InvalidDateTime",
                                     "Invalid date");
        }
        if (d->Day > daysInMonth(d->Year, d->Month)) {
            return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                     "ter:InvalidArgVal", "ter:InvalidDateTime",
                                     "Day does not exist in given month");
        }
        if (t->Hour < 0 || t->Hour > 23 || t->Minute < 0 || t->Minute > 59
            || t->Second < 0 || t->Second > 60) {
            return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                     "ter:InvalidArgVal", "ter:InvalidDateTime",
                                     "Invalid time");
        }
    }

    // Đọc state hiện tại từ MGMT: (1) ONVIF SetSystemDateAndTime chỉ mang
    // TimeZone dạng offset POSIX, không đủ để suy ra đúng tên IANA khi cần đổi
    // sang một offset khác — nên chỉ chấp nhận khi offset khớp zone hiện có,
    // còn lại giữ nguyên zone cũ; (2) khi DateTimeType=NTP, operation này của
    // ONVIF không mang theo NTP host (đó là việc của SetNTP, hiện chưa làm) —
    // phải lấy lại NTPServer đang cấu hình để không gửi thiếu xuống MGMT.
    SystemDateTime current;
    try {
        current = backend_->getSystemDateAndTime();
    } catch (const std::exception& e) {
        std::cerr << "[DeviceService] SetSystemDateAndTime: cannot read current state: "
                  << e.what() << std::endl;
        return soap_receiver_fault_subcode(
            this->soap, "\"http://www.onvif.org/ver10/error\":Action",
            "DateTime backend unavailable", nullptr);
    }

    SystemDateTime setReq;
    setReq.dateTimeType = isManual ? "MANUAL" : "NTP";
    setReq.timezone = current.timezone;

    if (req->TimeZone) {
        int requestedOffset = 0;
        if (!parsePosixOffsetMinutes(req->TimeZone->TZ, requestedOffset)) {
            return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                     "ter:InvalidArgVal", "ter:InvalidTimeZone",
                                     "Unsupported TimeZone format");
        }
        const auto utcMinutes = civilMinutes(current.year, current.month, current.day,
                                             current.hour, current.minute);
        const auto localMinutes = civilMinutes(current.localYear, current.localMonth,
                                               current.localDay, current.localHour,
                                               current.localMinute);
        const int currentOffset = static_cast<int>(localMinutes - utcMinutes);
        if (requestedOffset != currentOffset) {
            // Một offset POSIX có thể khớp nhiều IANA zone khác nhau — không
            // có cách suy ngược an toàn ra đúng zone. Từ chối thay vì đoán.
            return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                     "ter:InvalidArgVal", "ter:InvalidTimeZone",
                                     "Device cannot map the requested UTC offset to a "
                                     "specific timezone; keep the currently advertised "
                                     "TimeZone or omit it");
        }
        // Offset khớp zone hiện có → không đổi gì, dùng nguyên current.timezone.
    }

    if (isManual) {
        auto* d = req->UTCDateTime->Date;
        auto* t = req->UTCDateTime->Time;
        setReq.year = d->Year; setReq.month = d->Month; setReq.day = d->Day;
        setReq.hour = t->Hour; setReq.minute = t->Minute; setReq.second = t->Second;
    } else {
        if (current.ntpMode.empty() ||
            (current.ntpMode == "MANUAL" && current.ntpHost.empty())) {
            return soap_receiver_fault_subcode(
                this->soap, "\"http://www.onvif.org/ver10/error\":Action",
                "NTP server is not configured", nullptr);
        }
        setReq.ntpMode = current.ntpMode;
        setReq.ntpHost = current.ntpHost;
    }

    try {
        backend_->setSystemDateAndTime(setReq);
    } catch (const MgmtValidationError& e) {
        std::cerr << "[DeviceService] SetSystemDateAndTime rejected by MGMT: "
                  << e.what() << std::endl;
        return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                 "ter:InvalidArgVal", nullptr, e.what());
    } catch (const std::exception& e) {
        std::cerr << "[DeviceService] SetSystemDateAndTime backend error: "
                  << e.what() << std::endl;
        return soap_receiver_fault_subcode(
            this->soap, "\"http://www.onvif.org/ver10/error\":Action",
            "SetSystemDateAndTime backend unavailable", nullptr);
    }

    return SOAP_OK;
}

// ── GetNTP / SetNTP ──────────────────────────────────────────────────────────
int DeviceService::GetNTP(_tds__GetNTP* req, _tds__GetNTPResponse& resp) {
    (void)req;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    auto soap = this->soap;

    SystemDateTime current;
    try {
        current = backend_->getSystemDateAndTime();
    } catch (const std::exception& e) {
        std::cerr << "[DeviceService] GetNTP: cannot read current state: "
                  << e.what() << std::endl;
        return soap_receiver_fault_subcode(
            this->soap, "\"http://www.onvif.org/ver10/error\":Action",
            "DateTime backend unavailable", nullptr);
    }

    auto info = soap_new_tt__NTPInformation(soap);
    info->FromDHCP = (current.ntpMode == "DHCP");
    if (!current.ntpHost.empty()) {
        auto host = soap_new_tt__NetworkHost(soap);
        if (isIPv4Address(current.ntpHost)) {
            host->Type = tt__NetworkHostType::IPv4;
            host->IPv4Address = Sp(soap, current.ntpHost);
        } else {
            host->Type = tt__NetworkHostType::DNS;
            host->DNSname = Sp(soap, current.ntpHost);
        }
        if (info->FromDHCP) info->NTPFromDHCP.push_back(host);
        else info->NTPManual.push_back(host);
    }
    resp.NTPInformation = info;
    return SOAP_OK;
}

int DeviceService::SetNTP(_tds__SetNTP* req, _tds__SetNTPResponse& resp) {
    (void)resp;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!req) {
        return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                 "ter:InvalidArgVal", nullptr, "Missing request");
    }

    std::string host;
    if (!req->FromDHCP) {
        if (req->NTPManual.empty()) {
            return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                     "ter:InvalidArgVal", "ter:MissingAttribute",
                                     "NTPManual required when FromDHCP is false");
        }
        auto* h = req->NTPManual.front();
        if (h) {
            if (h->IPv4Address) host = *h->IPv4Address;
            else if (h->DNSname) host = *h->DNSname;
            else if (h->IPv6Address) host = *h->IPv6Address;
        }
        if (!isValidNtpHost(host)) {
            return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                     "ter:InvalidArgVal", nullptr,
                                     "Invalid NTP host");
        }
    }

    // Đọc state hiện tại chỉ để giữ nguyên TimeZone — SetNTP không được đổi
    // ngày/giờ/múi giờ, chỉ đổi cấu hình NTP server.
    SystemDateTime current;
    try {
        current = backend_->getSystemDateAndTime();
    } catch (const std::exception& e) {
        std::cerr << "[DeviceService] SetNTP: cannot read current state: "
                  << e.what() << std::endl;
        return soap_receiver_fault_subcode(
            this->soap, "\"http://www.onvif.org/ver10/error\":Action",
            "DateTime backend unavailable", nullptr);
    }

    SystemDateTime setReq;
    // Giới hạn đã biết: MGMT (datetime_service.cpp) chỉ thực sự ghi NTPServer
    // khi DateTimeType=NTP — nhánh MANUAL bỏ qua NTPServer trong request và
    // giữ nguyên giá trị cũ trong DB. ONVIF coi SetNTP là operation độc lập
    // với DateTimeType, nhưng để cấu hình NTP thực sự được lưu qua MGMT,
    // SetNTP ở đây buộc phải gửi kèm DateTimeType=NTP — tức gọi SetNTP có
    // side effect chuyển đồng hồ sang chế độ NTP. Không có cách nào khác để
    // ghi NTPServer khi vẫn giữ DateTimeType=MANUAL với contract MGMT hiện tại.
    setReq.dateTimeType = "NTP";
    setReq.timezone = current.timezone;
    setReq.ntpMode = req->FromDHCP ? "DHCP" : "MANUAL";
    setReq.ntpHost = host;

    try {
        backend_->setSystemDateAndTime(setReq);
    } catch (const MgmtValidationError& e) {
        std::cerr << "[DeviceService] SetNTP rejected by MGMT: " << e.what() << std::endl;
        return devSendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                 "ter:InvalidArgVal", nullptr, e.what());
    } catch (const std::exception& e) {
        std::cerr << "[DeviceService] SetNTP backend error: " << e.what() << std::endl;
        return soap_receiver_fault_subcode(
            this->soap, "\"http://www.onvif.org/ver10/error\":Action",
            "SetNTP backend unavailable", nullptr);
    }
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
