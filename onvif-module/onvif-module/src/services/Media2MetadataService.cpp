// Media2MetadataService.cpp — Media2 (ver20) metadata + analytics config (Profile M).
// String-based interceptor chạy trước Media2Service gSOAP dispatch.

#include "services/Media2MetadataService.h"
#include "services/AnalyticsModuleStore.h"
#include <sstream>

namespace {
const char* NS_MEDIA2 = "http://www.onvif.org/ver20/media/wsdl";
const char* ACT = "http://www.onvif.org/ver20/media/wsdl/Media2/";

// Token cố định (mock).
const char* VAC_TOKEN  = "vac_main";        // VideoAnalyticsConfiguration
const char* META_TOKEN = "metadata_config"; // MetadataConfiguration
} // namespace

// ── Helpers ─────────────────────────────────────────────────────────────────
std::string Media2MetadataService::extractRelatesTo(const std::string& xml) {
    for (const char* tag : {"MessageID", "wsa:MessageID", "wsa5:MessageID"}) {
        auto p = xml.find(tag);
        if (p == std::string::npos) continue;
        auto gt = xml.find('>', p);
        if (gt == std::string::npos) continue;
        auto lt = xml.find('<', gt);
        if (lt == std::string::npos) continue;
        return xml.substr(gt + 1, lt - gt - 1);
    }
    return "";
}

std::string Media2MetadataService::wrap(const std::string& action,
                                        const std::string& relatesTo,
                                        const std::string& bodyXml) {
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
       << "<SOAP-ENV:Envelope"
       << " xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\""
       << " xmlns:wsa=\"http://www.w3.org/2005/08/addressing\""
       << " xmlns:tr2=\"" << NS_MEDIA2 << "\""
       << " xmlns:tt=\"http://www.onvif.org/ver10/schema\""
       << " xmlns:ter=\"http://www.onvif.org/ver10/error\">"
       << "<SOAP-ENV:Header><wsa:Action>" << action << "</wsa:Action>";
    if (!relatesTo.empty())
        os << "<wsa:RelatesTo>" << relatesTo << "</wsa:RelatesTo>";
    os << "</SOAP-ENV:Header>"
       << "<SOAP-ENV:Body>" << bodyXml << "</SOAP-ENV:Body>"
       << "</SOAP-ENV:Envelope>";
    return os.str();
}

bool Media2MetadataService::targetsMetadataOrAnalytics(const std::string& req) {
    // AddConfiguration/RemoveConfiguration có <Type>Metadata</Type> hoặc Analytics.
    // VideoSource/VideoEncoder → Media2Service gSOAP lo (không intercept).
    return req.find("Metadata") != std::string::npos ||
           req.find("Analytics") != std::string::npos;
}

// ── Dispatch ────────────────────────────────────────────────────────────────
std::string Media2MetadataService::handle(const std::string& req) {
    // Chỉ xử lý request Media2 (ver20). Media1 (ver10) → MediaLegacyService lo.
    if (req.find(NS_MEDIA2) == std::string::npos) return "";
    std::string rel = extractRelatesTo(req);
    auto has = [&](const char* op) { return req.find(op) != std::string::npos; };

    if (has("GetAnalyticsConfigurations"))
        return wrap(std::string(ACT) + "GetAnalyticsConfigurationsResponse", rel,
                    handleGetAnalyticsConfigurations(req));
    if (has("GetMetadataConfigurationOptions"))
        return wrap(std::string(ACT) + "GetMetadataConfigurationOptionsResponse", rel,
                    handleGetMetadataConfigurationOptions(req));
    if (has("GetMetadataConfigurations"))
        return wrap(std::string(ACT) + "GetMetadataConfigurationsResponse", rel,
                    handleGetMetadataConfigurations(req));
    if (has("SetMetadataConfiguration"))
        return wrap(std::string(ACT) + "SetMetadataConfigurationResponse", rel,
                    handleSetMetadataConfiguration(req));
    // AddConfiguration/RemoveConfiguration: chỉ intercept nếu type Metadata/Analytics
    if (has("AddConfiguration") && targetsMetadataOrAnalytics(req))
        return wrap(std::string(ACT) + "AddConfigurationResponse", rel,
                    handleAddConfiguration(req));
    if (has("RemoveConfiguration") && targetsMetadataOrAnalytics(req))
        return wrap(std::string(ACT) + "RemoveConfigurationResponse", rel,
                    handleRemoveConfiguration(req));

    // Không phải op Profile-M metadata/analytics → "" → fallthrough Media2 gSOAP.
    return "";
}

// ── §7.9 GetAnalyticsConfigurations ──────────────────────────────────────────
std::string Media2MetadataService::handleGetAnalyticsConfigurations(const std::string& req) {
    (void)req;
    std::ostringstream os;
    os << "<tr2:GetAnalyticsConfigurationsResponse>"
       << "<tr2:Configurations token=\"" << VAC_TOKEN << "\">"
         << "<tt:Name>VideoAnalyticsConfig</tt:Name>"
         << "<tt:UseCount>1</tt:UseCount>"
         << "<tt:AnalyticsEngineConfiguration>";
    // Module lấy từ shared store (đồng bộ với AnalyticsService Create/Delete).
    for (const auto& m : AnalyticsModuleStore::instance().list()) {
        os << "<tt:AnalyticsModule Name=\"" << m.name
           << "\" Type=\"" << m.type << "\">"
           << "<tt:Parameters>"
             << "<tt:SimpleItem Name=\"Sensitivity\" Value=\"50\"/>"
           << "</tt:Parameters>"
           << "</tt:AnalyticsModule>";
    }
    os << "</tt:AnalyticsEngineConfiguration>"
         << "<tt:RuleEngineConfiguration/>"
       << "</tr2:Configurations>"
       << "</tr2:GetAnalyticsConfigurationsResponse>";
    return os.str();
}

// ── §7.7/7.8 GetMetadataConfigurations ───────────────────────────────────────
std::string Media2MetadataService::handleGetMetadataConfigurations(const std::string& req) {
    (void)req;
    std::ostringstream os;
    os << "<tr2:GetMetadataConfigurationsResponse>"
       << "<tr2:Configurations token=\"" << META_TOKEN << "\">"
         << "<tt:Name>MetadataConfig</tt:Name>"
         << "<tt:UseCount>1</tt:UseCount>"
         << "<tt:Analytics>true</tt:Analytics>"
         << "<tt:Multicast>"
           << "<tt:Address><tt:Type>IPv4</tt:Type>"
             << "<tt:IPv4Address>239.0.0.1</tt:IPv4Address></tt:Address>"
           << "<tt:Port>32001</tt:Port><tt:TTL>1</tt:TTL><tt:AutoStart>false</tt:AutoStart>"
         << "</tt:Multicast>"
         << "<tt:SessionTimeout>PT60S</tt:SessionTimeout>"
         << "<tt:AnalyticsEngineConfiguration/>"
       << "</tr2:Configurations>"
       << "</tr2:GetMetadataConfigurationsResponse>";
    return os.str();
}

// ── §7.8 GetMetadataConfigurationOptions ─────────────────────────────────────
std::string Media2MetadataService::handleGetMetadataConfigurationOptions(const std::string& req) {
    (void)req;
    return
        "<tr2:GetMetadataConfigurationOptionsResponse>"
          "<tr2:Options>"
            "<tt:PTZStatusFilterOptions>"
              "<tt:PanTiltStatusSupported>false</tt:PanTiltStatusSupported>"
              "<tt:ZoomStatusSupported>false</tt:ZoomStatusSupported>"
            "</tt:PTZStatusFilterOptions>"
          "</tr2:Options>"
        "</tr2:GetMetadataConfigurationOptionsResponse>";
}

// ── §7.8 SetMetadataConfiguration ────────────────────────────────────────────
std::string Media2MetadataService::handleSetMetadataConfiguration(const std::string& req) {
    (void)req;
    return "<tr2:SetMetadataConfigurationResponse/>";
}

// ── §7.7/7.9 AddConfiguration (metadata/analytics) ───────────────────────────
std::string Media2MetadataService::handleAddConfiguration(const std::string& req) {
    (void)req;
    return "<tr2:AddConfigurationResponse/>";
}

std::string Media2MetadataService::handleRemoveConfiguration(const std::string& req) {
    (void)req;
    return "<tr2:RemoveConfigurationResponse/>";
}
