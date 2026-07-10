#pragma once
// AnalyticsService — ONVIF Analytics service (ver20) cho Profile M.
//
// Chuẩn kiến trúc: docs/10-refactor-plan-multi-profile.md PHẦN II + doc 11 (Profile M).
// String-based service (implement IOnvifService) — pattern giống MediaLegacyHandler.
// pathPrefix "/onvif/analytics". Register 1 dòng trong OnvifServer.
//
// M1 scope (mandatory Profile M §7.6, §7.10):
//   - GetServiceCapabilities
//   - GetSupportedMetadata            (§7.6)
//   - GetSupportedAnalyticsModules    (§7.10)
//   - GetAnalyticsModules
//   - CreateAnalyticsModules / DeleteAnalyticsModules
//   - GetAnalyticsModuleOptions / ModifyAnalyticsModules
// (Rule config §8.6 + metadata stream §7.5 làm ở M3/M5.)

#include "core/IOnvifService.h"
#include <string>
#include <vector>
#include <utility>

class AnalyticsService : public IOnvifService {
public:
    std::string pathPrefix() const override { return "/onvif/analytics"; }
    std::string name() const override { return "AnalyticsService"; }
    std::string handle(const std::string& rawRequest) override;

private:
    // Handlers — trả body XML (chưa wrap envelope).
    std::string handleGetServiceCapabilities();
    std::string handleGetSupportedMetadata(const std::string& req);
    std::string handleGetSupportedAnalyticsModules(const std::string& req);
    std::string handleGetAnalyticsModules(const std::string& req);
    std::string handleCreateAnalyticsModules(const std::string& req);
    std::string handleDeleteAnalyticsModules(const std::string& req);
    std::string handleGetAnalyticsModuleOptions(const std::string& req);
    std::string handleModifyAnalyticsModules(const std::string& req);

    // Helpers
    static std::string extractRelatesTo(const std::string& xml);
    static std::string extractInnerTag(const std::string& xml, const std::string& localName);
    // Lấy attribute từ opening tag: extractAttr(xml,"AnalyticsModule","Name")
    static std::string extractAttr(const std::string& xml,
                                   const std::string& tagName,
                                   const std::string& attr);
    // Parse tất cả <SimpleItem Name=".." Value=".."/> trong request.
    static std::vector<std::pair<std::string, std::string>>
        parseSimpleItems(const std::string& xml);
    static std::string wrap(const std::string& action,
                            const std::string& relatesTo,
                            const std::string& bodyXml);
};
