// ImagingService.cpp — implement 4 op mandatory Profile T (Imaging).
// Map backend ImagingSettings (brightness/contrast/saturation/sharpness + BLC/WDR)
// tới các struct gSOAP tt__ImagingSettings20 / tt__ImagingOptions20.

#include "services/ImagingService.h"
#include <iostream>

ImagingService::ImagingService(struct soap* soap,
                               const ServiceConfig& cfg,
                               std::shared_ptr<ICameraBackend> backend)
    : ImagingBindingService(soap), cfg_(cfg), backend_(std::move(backend)) {}

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
    // Không hỗ trợ ImageStabilization / Presets — không set (mặc định null = false).
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

    ImagingSettings s;
    try {
        s = backend_->getImagingSettings(req ? req->VideoSourceToken : "");
    } catch (const std::exception& e) {
        std::cerr << "[ImagingService] backend error: " << e.what() << "\n";
        // fallback dùng giá trị mặc định của struct
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

    ImagingSettings s;
    // Đọc giá trị hiện tại làm baseline, chỉ ghi đè trường được gửi
    try { s = backend_->getImagingSettings(req->VideoSourceToken); }
    catch (...) { /* dùng default */ }

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

    try {
        if (!backend_->setImagingSettings(req->VideoSourceToken, s)) {
            return soap_receiver_fault_subcode(
                this->soap, "ter:Failed", "Receiver",
                "Backend rejected imaging settings");
        }
    } catch (const std::exception& e) {
        std::cerr << "[ImagingService] setImagingSettings error: " << e.what() << "\n";
        return soap_receiver_fault(this->soap, "Backend error", nullptr);
    }
    return SOAP_OK;
}

// ── GetOptions ─────────────────────────────────────────────────────────────
int ImagingService::GetOptions(
    _timg__GetOptions *req,
    _timg__GetOptionsResponse &resp)
{
    (void)req;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    auto soap = this->soap;

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
