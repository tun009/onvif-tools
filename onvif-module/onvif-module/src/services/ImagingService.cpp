// ImagingService.cpp — implement 4 op mandatory Profile T (Imaging).
// Map backend ImagingSettings (brightness/contrast/saturation/sharpness + BLC/WDR)
// tới các struct gSOAP tt__ImagingSettings20 / tt__ImagingOptions20.

#include "services/ImagingService.h"
#include <iostream>

std::mutex ImagingService::cacheMtx_;
std::map<std::string, ImagingSettings> ImagingService::cache_;

ImagingService::ImagingService(struct soap* soap,
                               const ServiceConfig& cfg,
                               std::shared_ptr<ICameraBackend> backend)
    : ImagingBindingService(soap), cfg_(cfg), backend_(std::move(backend)) {}

// Token hợp lệ khớp với MediaLegacyHandler + Media2Service.
bool ImagingService::isValidToken(const std::string& tok) {
    return tok == "video_source_token";
}

bool ImagingService::isValidSettings(const ImagingSettings& s) {
    auto in = [](float v) { return v >= 0.0f && v <= 100.0f; };
    return in(s.brightness) && in(s.contrast)
        && in(s.saturation) && in(s.sharpness);
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
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    auto soap = this->soap;

    auto caps = soap_new_timg__Capabilities(soap);
    // Set explicit false — nếu để null, XML sinh ra thiếu attribute, tool báo
    // "Error in deserializing body" (IMAGING-3-1-1).
    auto B = [&](bool v) { bool* p = (bool*)soap_malloc(soap, sizeof(bool)); *p = v; return p; };
    caps->ImageStabilization = B(false);
    caps->Presets            = B(false);
    caps->AdaptablePreset    = B(false);
    resp.Capabilities = caps;
    return SOAP_OK;
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
        return soap_sender_fault_subcode(soap, "ter:NoSource",
                                         "Sender", "Invalid VideoSourceToken");
    }

    // Đọc từ cache trước, fallback backend nếu chưa có.
    ImagingSettings s;
    {
        std::lock_guard<std::mutex> lk(cacheMtx_);
        auto it = cache_.find(tok);
        if (it != cache_.end()) s = it->second;
        else {
            try { s = backend_->getImagingSettings(tok); }
            catch (const std::exception& e) {
                std::cerr << "[ImagingService] backend error: " << e.what() << "\n";
            }
            cache_[tok] = s;
        }
    }

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

    // IrCutFilter (mock — camera màu ngày)
    auto* irc = (tt__IrCutFilterMode*)soap_malloc(soap, sizeof(tt__IrCutFilterMode));
    *irc = tt__IrCutFilterMode::AUTO;
    out->IrCutFilter = irc;

    // WhiteBalance mặc định AUTO
    out->WhiteBalance = soap_new_tt__WhiteBalance20(soap);
    out->WhiteBalance->Mode = tt__WhiteBalanceMode::AUTO;

    // Exposure mặc định AUTO
    out->Exposure = soap_new_tt__Exposure20(soap);
    out->Exposure->Mode = tt__ExposureMode::AUTO;

    // Focus mặc định AUTO
    out->Focus = soap_new_tt__FocusConfiguration20(soap);
    out->Focus->AutoFocusMode = tt__AutoFocusMode::AUTO;

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
        return soap_sender_fault_subcode(this->soap, "ter:InvalidArgVal",
                                         "Sender", "Missing ImagingSettings");
    }
    if (!isValidToken(req->VideoSourceToken)) {
        return soap_sender_fault_subcode(this->soap, "ter:NoSource",
                                         "Sender", "Invalid VideoSourceToken");
    }

    ImagingSettings s;
    {
        std::lock_guard<std::mutex> lk(cacheMtx_);
        auto it = cache_.find(req->VideoSourceToken);
        if (it != cache_.end()) s = it->second;
        else {
            try { s = backend_->getImagingSettings(req->VideoSourceToken); }
            catch (...) {}
        }
    }

    if (req->ImagingSettings->Brightness)      s.brightness  = *req->ImagingSettings->Brightness;
    if (req->ImagingSettings->ColorSaturation) s.saturation  = *req->ImagingSettings->ColorSaturation;
    if (req->ImagingSettings->Contrast)        s.contrast    = *req->ImagingSettings->Contrast;
    if (req->ImagingSettings->Sharpness)       s.sharpness   = *req->ImagingSettings->Sharpness;
    if (req->ImagingSettings->BacklightCompensation)
        s.backlightComp =
            (req->ImagingSettings->BacklightCompensation->Mode ==
             tt__BacklightCompensationMode::ON);
    if (req->ImagingSettings->WideDynamicRange)
        s.wideDynRange =
            (req->ImagingSettings->WideDynamicRange->Mode ==
             tt__WideDynamicMode::ON);

    // Validate range — settings ngoài [0,100] → fault (IMAGING-1-1-8).
    if (!isValidSettings(s)) {
        return soap_sender_fault_subcode(this->soap, "ter:SettingsInvalid",
                                         "Sender",
                                         "Imaging settings out of range");
    }

    // Ghi cache + best-effort push xuống backend.
    {
        std::lock_guard<std::mutex> lk(cacheMtx_);
        cache_[req->VideoSourceToken] = s;
    }
    try { backend_->setImagingSettings(req->VideoSourceToken, s); }
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
        return soap_sender_fault_subcode(soap, "ter:NoSource",
                                         "Sender", "Invalid VideoSourceToken");
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

    // Focus options: AUTO/MANUAL
    opts->Focus = soap_new_tt__FocusOptions20(soap);
    opts->Focus->AutoFocusModes.push_back(tt__AutoFocusMode::AUTO);
    opts->Focus->AutoFocusModes.push_back(tt__AutoFocusMode::MANUAL);

    resp.ImagingOptions = opts;
    return SOAP_OK;
}
