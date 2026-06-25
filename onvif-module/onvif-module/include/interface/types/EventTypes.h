#pragma once
#include <functional>
#include <cstdint>
#include <string>

namespace ipc {
// Forward declare MsgType for EventCallback
enum class MsgType : uint8_t;
}

// Callback: (eventType, jsonPayload)
using EventCallback = std::function<void(ipc::MsgType, const std::string&)>;
