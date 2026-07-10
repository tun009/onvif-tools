// AnalyticsService.cpp — ONVIF Analytics ver20 (Profile M) — string-based.
// M1: GetServiceCapabilities, GetSupportedMetadata, GetSupportedAnalyticsModules,
//     GetAnalyticsModules, Create/Delete/Modify, GetAnalyticsModuleOptions.
// Mock: state module trong RAM. Metadata stream thật (§7.5) làm ở M3.

#include "services/AnalyticsService.h"
#include <sstream>
#include <mutex>
#include <map>
#include <vector>

namespace {
const char* NS_ANALYTICS = "http://www.onvif.org/ver20/analytics/wsdl";
const char* ACT = "http://www.onvif.org/ver20/analytics/wsdl/AnalyticsEngine/";

// ConfigurationToken cố định của VideoAnalyticsConfiguration mock.
const char* VAC_TOKEN = "vac_main";

// State: module đã cấu hình (name → type QName).
struct AModule { std::string name; std::string type; };
std::mutex g_amMtx;
std::map<std::string, AModule> g_modules;   // name → module

void ensureDefaultModules() {
    if (!g_modules.empty()) return;
    // 2 module mặc định (mock). Type là QName chuẩn ONVIF.
    g_modules["MyCellMotion"]   = {"MyCellMotion",   "tt:CellMotionEngine"};
    g_modules["MyObjectDetect"] = {"MyObjectDetect", "tt:ObjectDetection"};
}
} // namespace

// ── Helpers ─────────────────────────────────────────────────────────────────
std::string AnalyticsService::extractRelatesTo(const std::string& xml) {
    // Lấy wsa:MessageID để set RelatesTo trong response.
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

std::string AnalyticsService::extractInnerTag(const std::string& xml,
                                              const std::string& localName) {
    // Tìm <...localName>value</...localName> hoặc <...localName ...>value.
    auto pos = xml.find(localName);
    while (pos != std::string::npos) {
        // Đảm bảo là tên tag (ký tự trước là '<' hoặc ':')
        char prev = pos > 0 ? xml[pos - 1] : '<';
        if (prev == '<' || prev == ':') {
            auto gt = xml.find('>', pos);
            if (gt != std::string::npos && xml[gt - 1] != '/') {
                auto lt = xml.find('<', gt);
                if (lt != std::string::npos)
                    return xml.substr(gt + 1, lt - gt - 1);
            }
        }
        pos = xml.find(localName, pos + localName.size());
    }
    return "";
}

std::string AnalyticsService::wrap(const std::string& action,
                                   const std::string& relatesTo,
                                   const std::string& bodyXml) {
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
       << "<SOAP-ENV:Envelope"
       << " xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\""
       << " xmlns:wsa=\"http://www.w3.org/2005/08/addressing\""
       << " xmlns:tan=\"" << NS_ANALYTICS << "\""
       << " xmlns:tt=\"http://www.onvif.org/ver10/schema\""
       << " xmlns:ter=\"http://www.onvif.org/ver10/error\">"
       << "<SOAP-ENV:Header>"
       << "<wsa:Action>" << action << "</wsa:Action>";
    if (!relatesTo.empty())
        os << "<wsa:RelatesTo>" << relatesTo << "</wsa:RelatesTo>";
    os << "</SOAP-ENV:Header>"
       << "<SOAP-ENV:Body>" << bodyXml << "</SOAP-ENV:Body>"
       << "</SOAP-ENV:Envelope>";
    return os.str();
}

// ── Dispatch ────────────────────────────────────────────────────────────────
std::string AnalyticsService::handle(const std::string& req) {
    std::string rel = extractRelatesTo(req);
    auto has = [&](const char* op) { return req.find(op) != std::string::npos; };

    if (has("GetServiceCapabilities"))
        return wrap(std::string(ACT) + "GetServiceCapabilitiesResponse", rel,
                    handleGetServiceCapabilities());
    if (has("GetSupportedMetadata"))
        return wrap(std::string(ACT) + "GetSupportedMetadataResponse", rel,
                    handleGetSupportedMetadata(req));
    if (has("GetSupportedAnalyticsModules"))
        return wrap(std::string(ACT) + "GetSupportedAnalyticsModulesResponse", rel,
                    handleGetSupportedAnalyticsModules(req));
    if (has("GetAnalyticsModuleOptions"))
        return wrap(std::string(ACT) + "GetAnalyticsModuleOptionsResponse", rel,
                    handleGetAnalyticsModuleOptions(req));
    if (has("GetAnalyticsModules"))
        return wrap(std::string(ACT) + "GetAnalyticsModulesResponse", rel,
                    handleGetAnalyticsModules(req));
    if (has("CreateAnalyticsModules"))
        return wrap(std::string(ACT) + "CreateAnalyticsModulesResponse", rel,
                    handleCreateAnalyticsModules(req));
    if (has("DeleteAnalyticsModules"))
        return wrap(std::string(ACT) + "DeleteAnalyticsModulesResponse", rel,
                    handleDeleteAnalyticsModules(req));
    if (has("ModifyAnalyticsModules"))
        return wrap(std::string(ACT) + "ModifyAnalyticsModulesResponse", rel,
                    handleModifyAnalyticsModules(req));

    // Không nhận diện op → "" để OnvifServer fallback (fault ActionNotSupported).
    return "";
}

// ── §7.10 GetServiceCapabilities ─────────────────────────────────────────────
std::string AnalyticsService::handleGetServiceCapabilities() {
    return
        "<tan:GetServiceCapabilitiesResponse>"
          "<tan:Capabilities RuleSupport=\"true\" AnalyticsModuleSupport=\"true\" "
           "CellBasedSceneDescriptionSupported=\"false\" RuleOptionsSupported=\"true\" "
           "AnalyticsModuleOptionsSupported=\"true\" SupportedMetadata=\"true\"/>"
        "</tan:GetServiceCapabilitiesResponse>";
}

// ── §7.6 GetSupportedMetadata ────────────────────────────────────────────────
// Trả MetadataInfo mô tả metadata device có thể sinh (sample frame).
std::string AnalyticsService::handleGetSupportedMetadata(const std::string& req) {
    (void)req;
    return
        "<tan:GetSupportedMetadataResponse>"
          "<tan:AnalyticsModule Type=\"tt:ObjectDetection\">"
            "<tt:Frame>"
              "<tt:Object ObjectId=\"0\">"
                "<tt:Appearance>"
                  "<tt:Shape>"
                    "<tt:BoundingBox left=\"0.0\" top=\"0.0\" right=\"0.0\" bottom=\"0.0\"/>"
                  "</tt:Shape>"
                  "<tt:Class>"
                    "<tt:Type Likelihood=\"0.0\">Human</tt:Type>"
                  "</tt:Class>"
                "</tt:Appearance>"
              "</tt:Object>"
            "</tt:Frame>"
          "</tan:AnalyticsModule>"
        "</tan:GetSupportedMetadataResponse>";
}

// ── §7.10 GetSupportedAnalyticsModules ───────────────────────────────────────
std::string AnalyticsService::handleGetSupportedAnalyticsModules(const std::string& req) {
    (void)req;
    return
        "<tan:GetSupportedAnalyticsModulesResponse>"
          "<tan:SupportedAnalyticsModules>"
            "<tt:AnalyticsModuleDescription Name=\"tt:CellMotionEngine\" "
             "fixed=\"false\" maxInstances=\"1\">"
              "<tt:Parameters>"
                "<tt:SimpleItemDescription Name=\"Sensitivity\" Type=\"xs:int\"/>"
              "</tt:Parameters>"
              "<tt:Messages>"
                "<tt:ParentTopic>tns1:VideoAnalytics/tnsaxis:MotionDetection</tt:ParentTopic>"
              "</tt:Messages>"
            "</tt:AnalyticsModuleDescription>"
            "<tt:AnalyticsModuleDescription Name=\"tt:ObjectDetection\" "
             "fixed=\"false\" maxInstances=\"1\">"
              "<tt:Parameters>"
                "<tt:SimpleItemDescription Name=\"Sensitivity\" Type=\"xs:int\"/>"
              "</tt:Parameters>"
            "</tt:AnalyticsModuleDescription>"
          "</tan:SupportedAnalyticsModules>"
        "</tan:GetSupportedAnalyticsModulesResponse>";
}

// ── GetAnalyticsModules — module đã gán cho VAC ──────────────────────────────
std::string AnalyticsService::handleGetAnalyticsModules(const std::string& req) {
    (void)req;
    std::lock_guard<std::mutex> lk(g_amMtx);
    ensureDefaultModules();
    std::ostringstream os;
    os << "<tan:GetAnalyticsModulesResponse>";
    for (const auto& kv : g_modules) {
        os << "<tan:AnalyticsModule Name=\"" << kv.second.name
           << "\" Type=\"" << kv.second.type << "\">"
           << "<tt:Parameters>"
             << "<tt:SimpleItem Name=\"Sensitivity\" Value=\"50\"/>"
           << "</tt:Parameters>"
           << "</tan:AnalyticsModule>";
    }
    os << "</tan:GetAnalyticsModulesResponse>";
    return os.str();
}

// ── CreateAnalyticsModules ───────────────────────────────────────────────────
std::string AnalyticsService::handleCreateAnalyticsModules(const std::string& req) {
    std::string name = extractInnerTag(req, "Name");
    std::string type = extractInnerTag(req, "Type");
    if (!name.empty()) {
        std::lock_guard<std::mutex> lk(g_amMtx);
        ensureDefaultModules();
        g_modules[name] = {name, type.empty() ? "tt:ObjectDetection" : type};
    }
    return "<tan:CreateAnalyticsModulesResponse/>";
}

// ── DeleteAnalyticsModules ───────────────────────────────────────────────────
std::string AnalyticsService::handleDeleteAnalyticsModules(const std::string& req) {
    std::string name = extractInnerTag(req, "Name");
    if (!name.empty()) {
        std::lock_guard<std::mutex> lk(g_amMtx);
        ensureDefaultModules();
        g_modules.erase(name);
    }
    return "<tan:DeleteAnalyticsModulesResponse/>";
}

// ── ModifyAnalyticsModules ───────────────────────────────────────────────────
std::string AnalyticsService::handleModifyAnalyticsModules(const std::string& req) {
    (void)req;
    return "<tan:ModifyAnalyticsModulesResponse/>";
}

// ── GetAnalyticsModuleOptions ────────────────────────────────────────────────
std::string AnalyticsService::handleGetAnalyticsModuleOptions(const std::string& req) {
    (void)req;
    return
        "<tan:GetAnalyticsModuleOptionsResponse>"
          "<tan:Options RuleType=\"tt:ObjectDetection\">"
            "<tt:IntRange>"
              "<tt:Min>0</tt:Min><tt:Max>100</tt:Max>"
            "</tt:IntRange>"
          "</tan:Options>"
        "</tan:GetAnalyticsModuleOptionsResponse>";
}
