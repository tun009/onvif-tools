// ImagingService.cpp — implement 4 op mandatory Profile T (Imaging).
// Map backend ImagingSettings (brightness/contrast/saturation/sharpness + BLC/WDR)
// tới các struct gSOAP tt__ImagingSettings20 / tt__ImagingOptions20.

#include "services/ImagingService.h"
#include <iostream>
#include <cstring>
#include <sstream>

std::mutex ImagingService::cacheMtx_;
std::map<std::string, ImagingService::ExtSettings> ImagingService::cache_;

ImagingService::ImagingService(struct soap* soap,
                               const ServiceConfig& cfg,
                               std::shared_ptr<ICameraBackend> backend)
    : ImagingBindingService(soap), cfg_(cfg), backend_(std::move(backend)) {}

// Token hợp lệ khớp với MediaLegacyHandler + Media2Service.
bool ImagingService::isValidToken(const std::string& tok) {
    // Chấp nhận cả token backend "src_main"/"src_sub*" và alias cũ.
    return tok == "src_main" || tok == "src_sub1" || tok == "src_sub2"
        || tok == "video_source_token";
}

bool ImagingService::isValidSettings(const ImagingSettings& s) {
    auto in = [](float v) { return v >= 0.0f && v <= 100.0f; };
    return in(s.brightness) && in(s.contrast)
        && in(s.saturation) && in(s.sharpness);
}

// Build fault XML thủ công. ONVIF fault dùng nested Subcode 2 lớp:
//   Code=env:Sender/Receiver -> Subcode=ter:<general> -> Subcode=ter:<specific>
// (vd env:Sender / ter:InvalidArgVal / ter:SettingsInvalid).
// gSOAP không sinh nested subcode chuẩn cũng không declare xmlns:ter — ta tự làm.
static int sendOnvifFault(struct soap* soap,
                          const char* code,           // "SOAP-ENV:Sender"|"SOAP-ENV:Receiver"
                          const char* subcode,        // ter:<general> (level 1)
                          const char* subSubcode,     // ter:<specific> (level 2, có thể null)
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

ImagingBindingService* ImagingService::copy() {
    return new ImagingService(this->soap, cfg_, backend_);
}

// ── Helper: cấp phát float* / bool* do soap quản lý ────────────────────────
namespace {
float* F(struct soap* s, float v) {
    float* p = (float*)soap_malloc(s, sizeof(float));
    *p = v;
    return p;
}
} // namespace

// ── GetServiceCapabilities ─────────────────────────────────────────────────
int ImagingService::GetServiceCapabilities(
    _timg__GetServiceCapabilities *req,
    _timg__GetServiceCapabilitiesResponse &resp)
{
    (void)req;
    (void)resp;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    auto soap = this->soap;

    // gSOAP tự thêm `xsi:type="timg:Capabilities"` (do class extends xsd:anyType)
    // khiến test tool báo "Error in deserializing body". Build response thủ công
    // — sạch, không xsi:type. (IMAGING-3-1-1)
    const char* xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<SOAP-ENV:Envelope"
        " xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\""
        " xmlns:timg=\"http://www.onvif.org/ver20/imaging/wsdl\">"
        "<SOAP-ENV:Body>"
        "<timg:GetServiceCapabilitiesResponse>"
        "<timg:Capabilities"
        " ImageStabilization=\"false\""
        " Presets=\"false\""
        " AdaptablePreset=\"false\"/>"
        "</timg:GetServiceCapabilitiesResponse>"
        "</SOAP-ENV:Body></SOAP-ENV:Envelope>";
    soap->http_content = "application/soap+xml; charset=utf-8";
    soap_response(soap, SOAP_FILE);
    soap_send_raw(soap, xml, strlen(xml));
    soap_end_send(soap);
    return SOAP_STOP;
}

// ── GetImagingSettings ─────────────────────────────────────────────────────
int ImagingService::GetImagingSettings(
    _timg__GetImagingSettings *req,
    _timg__GetImagingSettingsResponse &resp)
{
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    auto soap = this->soap;

    const std::string tok = req ? req->VideoSourceToken : "";
    if (!isValidToken(tok)) {
        return sendOnvifFault(soap, "SOAP-ENV:Sender", "ter:InvalidArgVal",
                              "ter:NoSource", "Invalid VideoSourceToken");
    }

    // Đọc từ cache; init từ backend + clamp nếu chưa có.
    ExtSettings ext;
    {
        std::lock_guard<std::mutex> lk(cacheMtx_);
        auto it = cache_.find(tok);
        if (it != cache_.end() && it->second.loaded) {
            ext = it->second;
        } else {
            try { ext.basic = backend_->getImagingSettings(tok); }
            catch (const std::exception& e) {
                std::cerr << "[ImagingService] backend error: " << e.what() << "\n";
            }
            auto clamp = [](float v) {
                return v < 0.0f ? 0.0f : (v > 100.0f ? 100.0f : v);
            };
            ext.basic.brightness = clamp(ext.basic.brightness);
            ext.basic.contrast   = clamp(ext.basic.contrast);
            ext.basic.saturation = clamp(ext.basic.saturation);
            ext.basic.sharpness  = clamp(ext.basic.sharpness);
            // Default modes: AUTO cho tất cả, IrCut = AUTO (2)
            ext.exposureMode = 0;
            ext.whiteBalanceMode = 0;
            ext.autoFocusMode = 0;
            ext.irCutFilter = 2;
            ext.loaded = true;
            cache_[tok] = ext;
        }
    }
    ImagingSettings& s = ext.basic;

    auto out = soap_new_tt__ImagingSettings20(soap);
    out->Brightness      = F(soap, s.brightness);
    out->ColorSaturation = F(soap, s.saturation);
    out->Contrast        = F(soap, s.contrast);
    out->Sharpness       = F(soap, s.sharpness);

    // BacklightCompensation
    out->BacklightCompensation = soap_new_tt__BacklightCompensation20(soap);
    out->BacklightCompensation->Mode =
        s.backlightComp ? tt__BacklightCompensationMode::ON
                        : tt__BacklightCompensationMode::OFF;

    // WideDynamicRange
    out->WideDynamicRange = soap_new_tt__WideDynamicRange20(soap);
    out->WideDynamicRange->Mode =
        s.wideDynRange ? tt__WideDynamicMode::ON
                       : tt__WideDynamicMode::OFF;

    // IrCutFilter (từ cache)
    auto* irc = (tt__IrCutFilterMode*)soap_malloc(soap, sizeof(tt__IrCutFilterMode));
    *irc = static_cast<tt__IrCutFilterMode>(ext.irCutFilter);
    out->IrCutFilter = irc;

    // WhiteBalance mode từ cache
    out->WhiteBalance = soap_new_tt__WhiteBalance20(soap);
    out->WhiteBalance->Mode = static_cast<tt__WhiteBalanceMode>(ext.whiteBalanceMode);

    // Exposure mode từ cache
    out->Exposure = soap_new_tt__Exposure20(soap);
    out->Exposure->Mode = static_cast<tt__ExposureMode>(ext.exposureMode);

    // Declare Focus mode (motorized lens mock) — Profile T conditional §7.16.
    // IMAGING-1-1-14 kiểm NearLimit/FarLimit persist qua Set→Get.
    out->Focus = soap_new_tt__FocusConfiguration20(soap);
    out->Focus->AutoFocusMode = static_cast<tt__AutoFocusMode>(ext.autoFocusMode);
    auto* nl = (float*)soap_malloc(soap, sizeof(float)); *nl = ext.focusNearLimit;
    auto* fl = (float*)soap_malloc(soap, sizeof(float)); *fl = ext.focusFarLimit;
    auto* ds = (float*)soap_malloc(soap, sizeof(float)); *ds = ext.focusDefaultSpeed;
    out->Focus->NearLimit = nl;
    out->Focus->FarLimit = fl;
    out->Focus->DefaultSpeed = ds;

    resp.ImagingSettings = out;
    return SOAP_OK;
}

// ── SetImagingSettings ─────────────────────────────────────────────────────
int ImagingService::SetImagingSettings(
    _timg__SetImagingSettings *req,
    _timg__SetImagingSettingsResponse &resp)
{
    (void)resp;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;

    if (!req || !req->ImagingSettings) {
        return sendOnvifFault(this->soap, "SOAP-ENV:Sender", "ter:InvalidArgVal",
                              nullptr, "Missing ImagingSettings");
    }
    if (!isValidToken(req->VideoSourceToken)) {
        return sendOnvifFault(this->soap, "SOAP-ENV:Sender", "ter:InvalidArgVal",
                              "ter:NoSource", "Invalid VideoSourceToken");
    }

    // Đọc cache/init từ backend làm baseline.
    ExtSettings ext;
    {
        std::lock_guard<std::mutex> lk(cacheMtx_);
        auto it = cache_.find(req->VideoSourceToken);
        if (it != cache_.end() && it->second.loaded) ext = it->second;
        else {
            try { ext.basic = backend_->getImagingSettings(req->VideoSourceToken); }
            catch (...) {}
            auto clamp = [](float v) {
                return v < 0.0f ? 0.0f : (v > 100.0f ? 100.0f : v);
            };
            ext.basic.brightness = clamp(ext.basic.brightness);
            ext.basic.contrast   = clamp(ext.basic.contrast);
            ext.basic.saturation = clamp(ext.basic.saturation);
            ext.basic.sharpness  = clamp(ext.basic.sharpness);
            ext.loaded = true;
        }
    }

    auto* in = req->ImagingSettings;
    if (in->Brightness)      ext.basic.brightness  = *in->Brightness;
    if (in->ColorSaturation) ext.basic.saturation  = *in->ColorSaturation;
    if (in->Contrast)        ext.basic.contrast    = *in->Contrast;
    if (in->Sharpness)       ext.basic.sharpness   = *in->Sharpness;
    if (in->BacklightCompensation)
        ext.basic.backlightComp =
            (in->BacklightCompensation->Mode == tt__BacklightCompensationMode::ON);
    if (in->WideDynamicRange)
        ext.basic.wideDynRange =
            (in->WideDynamicRange->Mode == tt__WideDynamicMode::ON);
    if (in->Exposure)     ext.exposureMode     = static_cast<int>(in->Exposure->Mode);
    if (in->WhiteBalance) ext.whiteBalanceMode = static_cast<int>(in->WhiteBalance->Mode);
    if (in->Focus) {
        ext.autoFocusMode = static_cast<int>(in->Focus->AutoFocusMode);
        if (in->Focus->NearLimit)    ext.focusNearLimit    = *in->Focus->NearLimit;
        if (in->Focus->FarLimit)     ext.focusFarLimit     = *in->Focus->FarLimit;
        if (in->Focus->DefaultSpeed) ext.focusDefaultSpeed = *in->Focus->DefaultSpeed;
    }
    if (in->IrCutFilter)  ext.irCutFilter      = static_cast<int>(*in->IrCutFilter);

    // Validate range — nested fault (IMAGING-1-1-8).
    if (!isValidSettings(ext.basic)) {
        return sendOnvifFault(this->soap, "SOAP-ENV:Sender",
                              "ter:InvalidArgVal", "ter:SettingsInvalid",
                              "Imaging settings out of range");
    }

    // Ghi cache + best-effort push xuống backend (backend chỉ nhận subset).
    {
        std::lock_guard<std::mutex> lk(cacheMtx_);
        cache_[req->VideoSourceToken] = ext;
    }
    try { backend_->setImagingSettings(req->VideoSourceToken, ext.basic); }
    catch (const std::exception& e) {
        std::cerr << "[ImagingService] setImagingSettings backend err: "
                  << e.what() << " (cache still updated)\n";
    }
    return SOAP_OK;
}

// ── GetOptions ─────────────────────────────────────────────────────────────
int ImagingService::GetOptions(
    _timg__GetOptions *req,
    _timg__GetOptionsResponse &resp)
{
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    auto soap = this->soap;

    if (!req || !isValidToken(req->VideoSourceToken)) {
        return sendOnvifFault(soap, "SOAP-ENV:Sender", "ter:InvalidArgVal",
                              "ter:NoSource", "Invalid VideoSourceToken");
    }

    auto opts = soap_new_tt__ImagingOptions20(soap);

    // Ranges 0..100 cho brightness/contrast/saturation/sharpness (đồng nhất backend)
    auto R = [&](float lo, float hi) {
        auto r = soap_new_tt__FloatRange(soap);
        r->Min = lo; r->Max = hi;
        return r;
    };
    opts->Brightness      = R(0.0f, 100.0f);
    opts->ColorSaturation = R(0.0f, 100.0f);
    opts->Contrast        = R(0.0f, 100.0f);
    opts->Sharpness       = R(0.0f, 100.0f);

    // BacklightCompensation options: ON/OFF, không Level
    opts->BacklightCompensation = soap_new_tt__BacklightCompensationOptions20(soap);
    opts->BacklightCompensation->Mode.push_back(tt__BacklightCompensationMode::OFF);
    opts->BacklightCompensation->Mode.push_back(tt__BacklightCompensationMode::ON);

    // WideDynamicRange options
    opts->WideDynamicRange = soap_new_tt__WideDynamicRangeOptions20(soap);
    opts->WideDynamicRange->Mode.push_back(tt__WideDynamicMode::OFF);
    opts->WideDynamicRange->Mode.push_back(tt__WideDynamicMode::ON);

    // IrCutFilterModes
    opts->IrCutFilterModes.push_back(tt__IrCutFilterMode::AUTO);
    opts->IrCutFilterModes.push_back(tt__IrCutFilterMode::ON);
    opts->IrCutFilterModes.push_back(tt__IrCutFilterMode::OFF);

    // WhiteBalance options: chỉ AUTO/MANUAL (không CrGain/CbGain range để đơn giản)
    opts->WhiteBalance = soap_new_tt__WhiteBalanceOptions20(soap);
    opts->WhiteBalance->Mode.push_back(tt__WhiteBalanceMode::AUTO);
    opts->WhiteBalance->Mode.push_back(tt__WhiteBalanceMode::MANUAL);

    // Exposure options: AUTO/MANUAL, không set các range chi tiết
    opts->Exposure = soap_new_tt__ExposureOptions20(soap);
    opts->Exposure->Mode.push_back(tt__ExposureMode::AUTO);
    opts->Exposure->Mode.push_back(tt__ExposureMode::MANUAL);

    // Declare Focus support (motorized lens mock) — Profile T §7.16 conditional.
    // Với declare, tool sẽ chạy 8 IMAGING-2-1-* Focus Move tests. Server có stub
    // ops Move/Stop/GetStatus/GetMoveOptions ở dưới.
    opts->Focus = soap_new_tt__FocusOptions20(soap);
    opts->Focus->AutoFocusModes.push_back(tt__AutoFocusMode::AUTO);
    opts->Focus->AutoFocusModes.push_back(tt__AutoFocusMode::MANUAL);
    opts->Focus->DefaultSpeed = soap_new_tt__FloatRange(soap);
    opts->Focus->DefaultSpeed->Min = 0.0f;
    opts->Focus->DefaultSpeed->Max = 1.0f;
    opts->Focus->NearLimit = soap_new_tt__FloatRange(soap);
    opts->Focus->NearLimit->Min = 0.0f;
    opts->Focus->NearLimit->Max = 100.0f;
    opts->Focus->FarLimit = soap_new_tt__FloatRange(soap);
    opts->Focus->FarLimit->Min = 0.0f;
    opts->Focus->FarLimit->Max = 100.0f;

    resp.ImagingOptions = opts;
    return SOAP_OK;
}

// ── Focus Move stubs (Profile T conditional §7.16) ─────────────────────────
// State đơn giản: 1 position float per VideoSource, moveStatus IDLE.
namespace {
struct FocusState {
    float position = 50.0f;
    std::string moveStatus = "IDLE";   // IDLE | MOVING | UNKNOWN
};
static std::mutex g_focusMtx;
static std::map<std::string, FocusState> g_focus;
}

int ImagingService::GetMoveOptions(_timg__GetMoveOptions* req,
                                   _timg__GetMoveOptionsResponse& resp) {
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    auto soap = this->soap;
    if (!req || !isValidToken(req->VideoSourceToken)) {
        return sendOnvifFault(soap, "SOAP-ENV:Sender",
                              "ter:InvalidArgVal", "ter:NoSource",
                              "Invalid VideoSourceToken");
    }
    auto mo = soap_new_tt__MoveOptions20(soap);
    // Absolute focus: hỗ trợ tuyệt đối với vị trí 0..100
    mo->Absolute = soap_new_tt__AbsoluteFocusOptions(soap);
    mo->Absolute->Position = soap_new_tt__FloatRange(soap);
    mo->Absolute->Position->Min = 0.0f;
    mo->Absolute->Position->Max = 100.0f;
    mo->Absolute->Speed = soap_new_tt__FloatRange(soap);
    mo->Absolute->Speed->Min = 0.0f;
    mo->Absolute->Speed->Max = 1.0f;
    // Relative + Continuous: cung cấp options tối thiểu
    mo->Relative = soap_new_tt__RelativeFocusOptions20(soap);
    mo->Relative->Distance = soap_new_tt__FloatRange(soap);
    mo->Relative->Distance->Min = -100.0f;
    mo->Relative->Distance->Max = 100.0f;
    mo->Relative->Speed = soap_new_tt__FloatRange(soap);
    mo->Relative->Speed->Min = 0.0f;
    mo->Relative->Speed->Max = 1.0f;
    mo->Continuous = soap_new_tt__ContinuousFocusOptions(soap);
    mo->Continuous->Speed = soap_new_tt__FloatRange(soap);
    mo->Continuous->Speed->Min = -1.0f;
    mo->Continuous->Speed->Max = 1.0f;
    resp.MoveOptions = mo;
    return SOAP_OK;
}

int ImagingService::Move(_timg__Move* req, _timg__MoveResponse& resp) {
    (void)resp;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!req || !isValidToken(req->VideoSourceToken)) {
        return sendOnvifFault(this->soap, "SOAP-ENV:Sender",
                              "ter:InvalidArgVal", "ter:NoSource",
                              "Invalid VideoSourceToken");
    }
    if (!req->Focus) {
        return sendOnvifFault(this->soap, "SOAP-ENV:Sender",
                              "ter:InvalidArgVal", nullptr, "Missing Focus");
    }
    // Negative tests IMAGING-2-1-4/6/8: tool gửi position/speed vượt range,
    // server PHẢI trả fault (không silently accept).
    if (req->Focus->Absolute) {
        float p = req->Focus->Absolute->Position;
        if (p < 0.0f || p > 100.0f) {
            return sendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                  "ter:InvalidArgVal", "ter:SettingsInvalid",
                                  "Position out of range");
        }
        if (req->Focus->Absolute->Speed) {
            float sp = *req->Focus->Absolute->Speed;
            if (sp < 0.0f || sp > 1.0f) {
                return sendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                      "ter:InvalidArgVal", "ter:SettingsInvalid",
                                      "Speed out of range");
            }
        }
    }
    if (req->Focus->Relative) {
        float d = req->Focus->Relative->Distance;
        if (d < -100.0f || d > 100.0f) {
            return sendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                  "ter:InvalidArgVal", "ter:SettingsInvalid",
                                  "Distance out of range");
        }
        if (req->Focus->Relative->Speed) {
            float sp = *req->Focus->Relative->Speed;
            if (sp < 0.0f || sp > 1.0f) {
                return sendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                      "ter:InvalidArgVal", "ter:SettingsInvalid",
                                      "Relative speed out of range");
            }
        }
    }
    if (req->Focus->Continuous) {
        float sp = req->Focus->Continuous->Speed;
        if (sp < -1.0f || sp > 1.0f) {
            return sendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                  "ter:InvalidArgVal", "ter:SettingsInvalid",
                                  "Continuous speed out of range");
        }
    }
    std::lock_guard<std::mutex> lk(g_focusMtx);
    auto& st = g_focus[req->VideoSourceToken];
    if (req->Focus->Absolute) {
        st.position = req->Focus->Absolute->Position;
        st.moveStatus = "IDLE";
    } else if (req->Focus->Relative) {
        st.position += req->Focus->Relative->Distance;
        if (st.position < 0.0f) st.position = 0.0f;
        if (st.position > 100.0f) st.position = 100.0f;
        st.moveStatus = "IDLE";
    } else if (req->Focus->Continuous) {
        st.moveStatus = "MOVING";
    }
    return SOAP_OK;
}

int ImagingService::Stop(_timg__Stop* req, _timg__StopResponse& resp) {
    (void)resp;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!req || !isValidToken(req->VideoSourceToken)) {
        return sendOnvifFault(this->soap, "SOAP-ENV:Sender",
                              "ter:InvalidArgVal", "ter:NoSource",
                              "Invalid VideoSourceToken");
    }
    std::lock_guard<std::mutex> lk(g_focusMtx);
    g_focus[req->VideoSourceToken].moveStatus = "IDLE";
    return SOAP_OK;
}

int ImagingService::GetStatus(_timg__GetStatus* req,
                              _timg__GetStatusResponse& resp) {
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    auto soap = this->soap;
    if (!req || !isValidToken(req->VideoSourceToken)) {
        return sendOnvifFault(soap, "SOAP-ENV:Sender",
                              "ter:InvalidArgVal", "ter:NoSource",
                              "Invalid VideoSourceToken");
    }
    float pos; std::string mv;
    {
        std::lock_guard<std::mutex> lk(g_focusMtx);
        auto& st = g_focus[req->VideoSourceToken];
        pos = st.position; mv = st.moveStatus;
    }
    auto sts = soap_new_tt__ImagingStatus20(soap);
    sts->FocusStatus20 = soap_new_tt__FocusStatus20(soap);
    sts->FocusStatus20->Position = pos;
    sts->FocusStatus20->MoveStatus =
        (mv == "MOVING") ? tt__MoveStatus::MOVING : tt__MoveStatus::IDLE;
    resp.Status = sts;
    return SOAP_OK;
}
