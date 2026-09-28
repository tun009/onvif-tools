#pragma once

enum class FocusStatus { IDLE = 0, MOVING = 1, UNKNOWN = 2 };

struct ImagingSettings {
    float brightness    = 50.0f;  // 0-100
    float contrast      = 50.0f;  // 0-100
    float saturation    = 50.0f;  // 0-100
    float sharpness     = 50.0f;  // 0-100
    bool  backlightComp = false;
    bool  wideDynRange  = false;

    // Exposure — khớp MGMT ImagingSettings.Exposure (xem onvif-module cho
    // context đầy đủ; mock chỉ cần field tồn tại để interface khớp nhau).
    bool  exposureAuto    = true;
    float exposureTime    = 0.0f;
    float exposureMinTime = 0.0f;
    float exposureMaxTime = 0.0f;
    float exposureGain    = 0.0f;
    float exposureMinGain = 0.0f;
    float exposureMaxGain = 0.0f;

    // WhiteBalance — khớp MGMT ImagingSettings.WhiteBalance.
    bool  whiteBalanceAuto   = true;
    float whiteBalanceCrGain = 0.0f;
    float whiteBalanceCbGain = 0.0f;

    // IrCutFilter — 0=ON, 1=OFF, 2=AUTO (khớp tt__IrCutFilterMode).
    int   irCutFilterMode = 2;
};

struct ImagingStatus {
    FocusStatus focusStatus   = FocusStatus::IDLE;
    float       focusPosition = 0.5f;
};
