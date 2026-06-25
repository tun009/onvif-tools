#pragma once

enum class FocusStatus { IDLE = 0, MOVING = 1, UNKNOWN = 2 };

struct ImagingSettings {
    float brightness    = 50.0f;  // 0-100
    float contrast      = 50.0f;  // 0-100
    float saturation    = 50.0f;  // 0-100
    float sharpness     = 50.0f;  // 0-100
    bool  backlightComp = false;
    bool  wideDynRange  = false;
};

struct ImagingStatus {
    FocusStatus focusStatus   = FocusStatus::IDLE;
    float       focusPosition = 0.5f;
};
