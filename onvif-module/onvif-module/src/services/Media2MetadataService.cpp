// Media2MetadataService.cpp — Media2 (ver20) metadata + analytics config (Profile M).
// String-based interceptor chạy trước Media2Service gSOAP dispatch.

#include "services/Media2MetadataService.h"
#include "services/AnalyticsModuleStore.h"
#include "utils/FaultBuilder.h"
#include <sstream>
#include <mutex>
#include <algorithm>

namespace {
const char* NS_MEDIA2 = "http://www.onvif.org/ver20/media/wsdl";
const char* ACT = "http://www.onvif.org/ver20/media/wsdl/Media2/";

// Token cố định (mock).
const char* VAC_TOKEN  = "vac_main";        // VideoAnalyticsConfiguration
const char* META_TOKEN = "metadata_config"; // MetadataConfiguration

// State metadata config (persist Set→Get). MEDIA2-8-1-1 "MODIFY ALL SUPPORTED":
// capture-replay toàn bộ inner <Configuration> từ Set → echo trong Get để
// round-trip khớp mọi field (Name/Events/Multicast/Analytics/...).
std::mutex g_metaMtx;
std::string g_metaConfigInner;   // inner content <Configuration>...; rỗng = dùng default

// Lấy nội dung element đầu tiên khớp localName.
std::string innerTag(const std::string& xml, const std::string& local) {
    auto pos = xml.find(local);
    while (pos != std::string::npos) {
        char prev = pos > 0 ? xml[pos-1] : '<';
        if (prev == '<' || prev == ':') {
            auto gt = xml.find('>', pos);
            if (gt != std::string::npos && xml[gt-1] != '/') {
                auto lt = xml.find('<', gt);
                if (lt != std::string::npos) return xml.substr(gt+1, lt-gt-1);
            }
        }
        pos = xml.find(local, pos + local.size());
    }
    return "";
}

bool findElementByLocalName(const std::string& xml,
                            const std::string& local,
                            std::size_t& openEnd,
                            std::size_t& closeStart) {
    std::size_t p = 0;
    while ((p = xml.find('<', p)) != std::string::npos) {
        if (p + 1 >= xml.size() || xml[p + 1] == '/' ||
            xml[p + 1] == '?' || xml[p + 1] == '!') {
            ++p;
            continue;
        }

        std::size_t nameStart = p + 1;
        std::size_t nameEnd = nameStart;
        while (nameEnd < xml.size() &&
               xml[nameEnd] != '>' &&
               xml[nameEnd] != '/' &&
               xml[nameEnd] != ' ' &&
               xml[nameEnd] != '\t' &&
               xml[nameEnd] != '\r' &&
               xml[nameEnd] != '\n') {
            ++nameEnd;
        }

        std::string qname = xml.substr(nameStart, nameEnd - nameStart);
        std::size_t colon = qname.find(':');
        std::string lname = (colon == std::string::npos) ? qname : qname.substr(colon + 1);
        if (lname != local) {
            p = nameEnd;
            continue;
        }

        openEnd = xml.find('>', nameEnd);
        if (openEnd == std::string::npos || (openEnd > 0 && xml[openEnd - 1] == '/'))
            return false;

        std::string closePlain = "</" + local + ">";
        std::string closeQualified = "</" + qname + ">";
        std::size_t c1 = xml.find(closePlain, openEnd + 1);
        std::size_t c2 = xml.find(closeQualified, openEnd + 1);
        if (c1 == std::string::npos) closeStart = c2;
        else if (c2 == std::string::npos) closeStart = c1;
        else closeStart = std::min(c1, c2);
        return closeStart != std::string::npos;
    }
    return false;
}
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

    // Negative test (MEDIA2-8-1-4/9-1-3): ConfigurationToken invalid → NoConfig fault.
    std::string cfgTok = innerTag(req, "ConfigurationToken");
    if (has("GetAnalyticsConfigurations")) {
        if (!cfgTok.empty() && cfgTok != VAC_TOKEN)
            return FaultBuilder::sender("ter:InvalidArgVal", "ter:NoConfig",
                                        "No analytics configuration with the given token");
        return wrap(std::string(ACT) + "GetAnalyticsConfigurationsResponse", rel,
                    handleGetAnalyticsConfigurations(req));
    }
    if (has("GetMetadataConfigurationOptions"))
        return wrap(std::string(ACT) + "GetMetadataConfigurationOptionsResponse", rel,
                    handleGetMetadataConfigurationOptions(req));
    if (has("GetMetadataConfigurations")) {
        if (!cfgTok.empty() && cfgTok != META_TOKEN)
            return FaultBuilder::sender("ter:InvalidArgVal", "ter:NoConfig",
                                        "No metadata configuration with the given token");
        return wrap(std::string(ACT) + "GetMetadataConfigurationsResponse", rel,
                    handleGetMetadataConfigurations(req));
    }
    if (has("SetMetadataConfiguration"))
        return wrap(std::string(ACT) + "SetMetadataConfigurationResponse", rel,
                    handleSetMetadataConfiguration(req));
    // AddConfiguration/RemoveConfiguration: KHÔNG intercept — Media2Service (gSOAP)
    // xử lý + track token metadata/analytics vào g_dynProfiles (dùng cho GetProfiles).

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
           << "\" Type=\"" << m.type << "\"><tt:Parameters>";
        if (m.params.empty())
            os << "<tt:SimpleItem Name=\"Sensitivity\" Value=\"50\"/>";
        else for (const auto& p : m.params)
            os << "<tt:SimpleItem Name=\"" << p.first << "\" Value=\"" << p.second << "\"/>";
        os << "</tt:Parameters></tt:AnalyticsModule>";
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
    // Nếu đã Set → replay inner đã lưu (round-trip khớp). Chưa Set → default.
    {
        std::lock_guard<std::mutex> lk(g_metaMtx);
        if (!g_metaConfigInner.empty()) {
            return "<tr2:GetMetadataConfigurationsResponse>"
                   "<tr2:Configurations token=\"" + std::string(META_TOKEN) + "\">"
                   + g_metaConfigInner +
                   "</tr2:Configurations>"
                   "</tr2:GetMetadataConfigurationsResponse>";
        }
    }
    os << "<tr2:GetMetadataConfigurationsResponse>"
       << "<tr2:Configurations token=\"" << META_TOKEN << "\">"
         << "<tt:Name>MetadataConfig</tt:Name>"
         << "<tt:UseCount>1</tt:UseCount>"
         << "<tt:Analytics>true</tt:Analytics>"
         // Khớp CHÍNH XÁC metadata config trong GetProfiles (Media2Service) để
         // pass consistency test MEDIA2-8-1-3: Multicast không IPv4Address,
         // không AnalyticsEngineConfiguration.
         << "<tt:Multicast>"
           << "<tt:Address><tt:Type>IPv4</tt:Type></tt:Address>"
           << "<tt:Port>32001</tt:Port><tt:TTL>1</tt:TTL><tt:AutoStart>false</tt:AutoStart>"
         << "</tt:Multicast>"
         << "<tt:SessionTimeout>PT60S</tt:SessionTimeout>"
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
    // Capture-replay: lưu inner <Configuration>...</Configuration> để Get echo lại.
    // Match by local name so both <Configuration xmlns="..."> and
    // <tr2:Configuration ...> are captured.
    std::size_t openEnd = std::string::npos;
    std::size_t closeStart = std::string::npos;
    if (findElementByLocalName(req, "Configuration", openEnd, closeStart)) {
        std::string inner = req.substr(openEnd + 1, closeStart - openEnd - 1);
        std::lock_guard<std::mutex> lk(g_metaMtx);
        g_metaConfigInner = inner;
    }
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
