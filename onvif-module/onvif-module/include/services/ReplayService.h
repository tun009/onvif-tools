#pragma once
// ReplayService — ONVIF Replay Control (trp, ver10/replay/wsdl) cho Profile G.
//
// String-based service (IOnvifService), pattern giống AnalyticsService.
// pathPrefix "/onvif/replay". Register 1 dòng trong OnvifServer.
//
// STUB-FEATURES: GetServiceCapabilities + GetReplayUri (URI RTSP tĩnh) +
// Get/SetReplayConfiguration. Đủ để DTT Feature Definition đánh dấu Profile G.
// ReversePlayback=false (scope bỏ reverse). Streaming thật (RTP header extension
// 0xABAC, Range/onvif-replay) là Phase sau — xem memory project_profile_g_replay.
// ponytail: stub — URI tĩnh; RTSP replay pipeline làm ở Phase streaming.

#include "core/IOnvifService.h"
#include <string>

class ReplayService : public IOnvifService {
public:
    std::string pathPrefix() const override { return "/onvif/replay"; }
    std::string name() const override { return "ReplayService"; }
    std::string handle(const std::string& rawRequest) override;

private:
    static std::string extractRelatesTo(const std::string& xml);
    static std::string wrap(const std::string& action,
                            const std::string& relatesTo,
                            const std::string& bodyXml);
};
