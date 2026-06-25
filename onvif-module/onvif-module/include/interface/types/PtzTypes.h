#pragma once

struct PTZVector {
    float pan  = 0.0f;
    float tilt = 0.0f;
    float zoom = 0.0f;
};

enum class MoveStatus  { IDLE = 0, MOVING = 1, UNKNOWN = 2 };
enum class PTZMoveMode { IDLE = 0, ABSOLUTE = 1, RELATIVE = 2, CONTINUOUS = 3 };

struct PTZStatus {
    PTZVector  position;
    MoveStatus moveStatus    = MoveStatus::IDLE;
    MoveStatus panTiltStatus = MoveStatus::IDLE;
    MoveStatus zoomStatus    = MoveStatus::IDLE;
};
