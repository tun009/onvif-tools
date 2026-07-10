#pragma once
// MediaLegacyService — adapter bọc MediaLegacyHandler (Media1/ver10, Profile S).
//
// Phase 3 migration (docs/10-refactor-plan-multi-profile.md).
// pathPrefix "/onvif/media" trùng với Media2Service (gSOAP). Registry thử service
// này TRƯỚC: nếu là op Media1 → trả XML; nếu là op Media2 → dispatch() trả ""
// → OnvifServer fallthrough xuống media2Svc.dispatch(). Đây là pattern
// co-existence sẽ dùng cho AnalyticsService (Profile M) sống chung với Media2.

#include "core/IOnvifService.h"
#include "services/MediaLegacyHandler.h"

class MediaLegacyService : public IOnvifService {
public:
    std::string pathPrefix() const override { return "/onvif/media"; }
    std::string name() const override { return "MediaLegacyService"; }

    std::string handle(const std::string& rawRequest) override {
        // "" nếu là op Media2 (ver20) → OnvifServer fallthrough sang Media2Service.
        return MediaLegacyHandler::dispatch(rawRequest);
    }
};
