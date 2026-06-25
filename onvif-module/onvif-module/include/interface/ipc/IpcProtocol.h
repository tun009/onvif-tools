#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ipc {

constexpr uint32_t MAGIC   = 0x4F4E5646; // "ONVF"
constexpr uint8_t  VERSION = 0x01;

enum class MsgType : uint8_t {
    // Device
    REQ_GET_DEVICE_INFO         = 0x01,
    REQ_GET_NETWORK_CONFIG      = 0x02,
    REQ_SET_NETWORK_CONFIG      = 0x03,
    REQ_GET_DATETIME            = 0x04,
    REQ_SET_DATETIME            = 0x05,
    REQ_REBOOT                  = 0x06,
    REQ_FACTORY_RESET           = 0x07,

    // Media2
    REQ_GET_PROFILES            = 0x10,
    REQ_GET_STREAM_URI          = 0x11,
    REQ_SET_VIDEO_CONFIG        = 0x12,
    REQ_GET_SNAPSHOT_URI        = 0x13,

    // PTZ
    REQ_PTZ_ABSOLUTE_MOVE       = 0x20,
    REQ_PTZ_RELATIVE_MOVE       = 0x21,
    REQ_PTZ_CONTINUOUS_MOVE     = 0x22,
    REQ_PTZ_STOP                = 0x23,
    REQ_PTZ_GET_STATUS          = 0x24,
    REQ_PTZ_GOTO_HOME           = 0x25,
    REQ_PTZ_SET_HOME            = 0x26,

    // Imaging
    REQ_GET_IMAGING_SETTINGS    = 0x30,
    REQ_SET_IMAGING_SETTINGS    = 0x31,
    REQ_GET_IMAGING_STATUS      = 0x32,

    // Analytics
    REQ_GET_ANALYTICS_MODULES   = 0x40,
    REQ_GET_ANALYTICS_RULES     = 0x41,
    REQ_ADD_ANALYTICS_RULE      = 0x42,
    REQ_DEL_ANALYTICS_RULE      = 0x43,

    // Events
    REQ_SUBSCRIBE_EVENTS        = 0x50,
    REQ_UNSUBSCRIBE_EVENTS      = 0x51,
    REQ_RENEW_SUBSCRIPTION      = 0x52,

    // Responses
    RESP_OK                     = 0xA0,
    RESP_ERROR                  = 0xA1,

    // Server push events
    EVT_MOTION_DETECTED         = 0xB0,
    EVT_TAMPER_DETECTED         = 0xB1,
    EVT_OBJECT_DETECTED         = 0xB2,
    EVT_PTZ_MOVE_STOP           = 0xB3,
    EVT_STREAM_STATUS_CHANGED   = 0xB4,
    EVT_DEVICE_STATUS_CHANGED   = 0xB5,
};

#pragma pack(push, 1)
struct MsgHeader {
    uint32_t magic;
    uint8_t  version;
    uint8_t  msgType;
    uint16_t flags;
    uint32_t requestId;
    uint32_t payloadLen;
};
#pragma pack(pop)

static_assert(sizeof(MsgHeader) == 16, "MsgHeader must be 16 bytes");

struct Message {
    MsgHeader            header;
    std::vector<uint8_t> payload;
};

} // namespace ipc
