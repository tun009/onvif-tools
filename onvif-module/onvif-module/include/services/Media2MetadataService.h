#pragma once
// Media2MetadataService — intercept các op Media2 (ver20) thuộc Profile M
// mà Media2Service (gSOAP binding) chưa hỗ trợ: metadata config + analytics config.
//
// Chuẩn: docs/11-profile-m-plan.md (M2). String-based, pathPrefix "/onvif/media".
// Đăng ký SAU MediaLegacyService trong registry. Chain: MediaLegacy (Media1) →
// Media2Metadata (metadata/analytics) → "" → fallthrough media2Svc.dispatch()
// (Media2 ops còn lại: GetProfiles, VideoEncoder...).
//
// M2 scope (§7.7-7.9):
//   - GetAnalyticsConfigurations           (§7.9)  ← unblock ANALYTICS-4-*
//   - GetMetadataConfigurations            (§7.7, §7.8)
//   - GetMetadataConfigurationOptions      (§7.8)
//   - SetMetadataConfiguration             (§7.8)
//   - AddConfiguration/RemoveConfiguration type Metadata/Analytics (§7.7, §7.9)

#include "core/IOnvifService.h"
#include <string>

class Media2MetadataService : public IOnvifService {
public:
    std::string pathPrefix() const override { return "/onvif/media"; }
    std::string name() const override { return "Media2MetadataService"; }
    std::string handle(const std::string& rawRequest) override;

private:
    std::string handleGetAnalyticsConfigurations(const std::string& req);
    std::string handleGetMetadataConfigurations(const std::string& req);
    std::string handleGetMetadataConfigurationOptions(const std::string& req);
    std::string handleSetMetadataConfiguration(const std::string& req);
    std::string handleAddConfiguration(const std::string& req);
    std::string handleRemoveConfiguration(const std::string& req);

    static std::string extractRelatesTo(const std::string& xml);
    static std::string wrap(const std::string& action,
                            const std::string& relatesTo,
                            const std::string& bodyXml);
    // true nếu request AddConfiguration/RemoveConfiguration nhắm tới
    // Metadata/Analytics (không phải VideoSource/VideoEncoder của Media2Service).
    static bool targetsMetadataOrAnalytics(const std::string& req);
};
