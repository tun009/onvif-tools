#pragma once
// ReplayService — ONVIF Replay Control (trp, ver10/replay/wsdl) cho Profile G.
//
// String-based service (IOnvifService), pattern giống AnalyticsService.
// pathPrefix "/onvif/replay". Register 1 dòng trong OnvifServer.
//
// SKELETON (define-features): mới chỉ GetServiceCapabilities để DTT nhận diện
// Profile G và mở cây test case con. GetReplayUri làm ở bước sau.
// ponytail: skeleton — chỉ caps tĩnh; add GetReplayUri khi DTT lộ test case con.

#include "core/IOnvifService.h"
#include <string>

class ReplayService : public IOnvifService {
public:
    std::string pathPrefix() const override { return "/onvif/replay"; }
    std::string name() const override { return "ReplayService"; }
    std::string handle(const std::string& rawRequest) override;

private:
    std::string handleGetServiceCapabilities();
    static std::string extractRelatesTo(const std::string& xml);
    static std::string wrap(const std::string& action,
                            const std::string& relatesTo,
                            const std::string& bodyXml);
};
