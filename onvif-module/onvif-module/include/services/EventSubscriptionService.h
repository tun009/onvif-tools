#pragma once
// EventSubscriptionService — adapter bọc MockSubscriptionManager (Event service).
//
// Phase 3 migration (docs/10-refactor-plan-multi-profile.md).
// MockSubscriptionManager là singleton, dispatch() cần deviceIp + port + subId.
// subId nằm trong URL path "/onvif/event/<subId>" — parse từ request line.

#include "core/IOnvifService.h"
#include "services/MockSubscriptionManager.h"
#include <string>

class EventSubscriptionService : public IOnvifService {
public:
    EventSubscriptionService(std::string deviceIp, int port)
        : deviceIp_(std::move(deviceIp)), port_(port) {}

    std::string pathPrefix() const override { return "/onvif/event"; }
    std::string name() const override { return "EventSubscriptionService"; }

    std::string handle(const std::string& rawRequest) override {
        std::string subId = parseSubId(rawRequest);
        return MockSubscriptionManager::getInstance()
                   .dispatch(deviceIp_, port_, subId, rawRequest);
    }

private:
    std::string deviceIp_;
    int port_;

    // Lấy subId từ request line: "POST /onvif/event/<subId> HTTP/1.1"
    static std::string parseSubId(const std::string& req) {
        auto lineEnd = req.find("\r\n");
        std::string firstLine = req.substr(0, lineEnd == std::string::npos ? req.size() : lineEnd);
        auto p = firstLine.find("/onvif/event/");
        if (p == std::string::npos) return "";
        p += 13;  // len("/onvif/event/")
        auto e = firstLine.find_first_of(" \t", p);
        return firstLine.substr(p, (e == std::string::npos ? firstLine.size() : e) - p);
    }
};
