#pragma once

enum class FocusStatus { IDLE = 0, MOVING = 1, UNKNOWN = 2 };

struct ImagingSettings {
    float brightness    = 50.0f;  // 0-100
    float contrast      = 50.0f;  // 0-100
    float saturation    = 50.0f;  // 0-100
    float sharpness     = 50.0f;  // 0-100
    bool  backlightComp = false;
    bool  wideDynRange  = false;

    // Exposure — khớp MGMT ImagingSettings.Exposure. Đồng bộ dù MGMT có đẩy
    // xuống HAL thật hay chỉ lưu DB (MinGain không có HAL register phía
    // MGMT nhưng vẫn đồng bộ round-trip Get/Set).
    bool  exposureAuto    = true;   // Mode: AUTO=true / MANUAL=false
    float exposureTime    = 0.0f;   // microseconds, áp dụng khi MANUAL
    float exposureMinTime = 0.0f;
    float exposureMaxTime = 0.0f;
    float exposureGain    = 0.0f;   // dB, áp dụng khi MANUAL
    float exposureMinGain = 0.0f;
    float exposureMaxGain = 0.0f;

    // WhiteBalance — khớp MGMT ImagingSettings.WhiteBalance.
    bool  whiteBalanceAuto   = true;  // Mode: AUTO=true / MANUAL=false
    float whiteBalanceCrGain = 0.0f;
    float whiteBalanceCbGain = 0.0f;

    // IrCutFilter — khớp MGMT ImagingSettings.DayNight
    // (IrCutFilterMode AUTO/MANUAL + IrCutOn bool, 2 trục gộp thành 1 enum
    // ONVIF). 0=ON, 1=OFF, 2=AUTO — khớp tt__IrCutFilterMode phía onvif-module.
    int   irCutFilterMode = 2;
};

struct ImagingStatus {
    FocusStatus focusStatus   = FocusStatus::IDLE;
    float       focusPosition = 0.5f;
};
