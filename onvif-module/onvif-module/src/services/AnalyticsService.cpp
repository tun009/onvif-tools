// AnalyticsService.cpp — ONVIF Analytics ver20 (Profile M) — string-based.
// M1: GetServiceCapabilities, GetSupportedMetadata, GetSupportedAnalyticsModules,
//     GetAnalyticsModules, Create/Delete/Modify, GetAnalyticsModuleOptions.
// Mock: state module trong RAM. Metadata stream thật (§7.5) làm ở M3.

#include "services/AnalyticsService.h"
#include "services/AnalyticsModuleStore.h"
#include <sstream>

namespace {
const char* NS_ANALYTICS = "http://www.onvif.org/ver20/analytics/wsdl";
const char* ACT = "http://www.onvif.org/ver20/analytics/wsdl/AnalyticsEngine/";
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

std::string AnalyticsService::extractAttr(const std::string& xml,
                                          const std::string& tagName,
                                          const std::string& attr) {
    // Tìm tag MỞ: tagName theo sau bởi whitespace — tránh khớp nhầm
    // "AnalyticsModule" bên trong "CreateAnalyticsModules"/"GetAnalyticsModules".
    size_t tp = 0;
    while ((tp = xml.find(tagName, tp)) != std::string::npos) {
        size_t after = tp + tagName.size();
        char c = after < xml.size() ? xml[after] : '\0';
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') break;
        tp = after;
    }
    if (tp == std::string::npos) return "";
    auto end = xml.find('>', tp);
    if (end == std::string::npos) return "";
    std::string tag = xml.substr(tp, end - tp);
    auto ap = tag.find(attr + "=\"");
    if (ap == std::string::npos) return "";
    ap += attr.size() + 2;
    auto e = tag.find('"', ap);
    if (e == std::string::npos) return "";
    return tag.substr(ap, e - ap);
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
       << " xmlns:xs=\"http://www.w3.org/2001/XMLSchema\""
       << " xmlns:tns1=\"http://www.onvif.org/ver10/topics\""
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
        // MetadataInfo (Type=QName ref module) + child SampleFrame (tt:Frame).
        "<tan:GetSupportedMetadataResponse>"
          "<tan:AnalyticsModule Type=\"tt:ObjectDetection\">"
            "<tan:SampleFrame UtcTime=\"2020-01-01T00:00:00Z\">"
              "<tt:Object ObjectId=\"0\">"
                "<tt:Appearance>"
                  "<tt:Shape>"
                    "<tt:BoundingBox left=\"0.0\" top=\"0.0\" right=\"0.0\" bottom=\"0.0\"/>"
                    "<tt:CenterOfGravity x=\"0.0\" y=\"0.0\"/>"
                  "</tt:Shape>"
                  "<tt:Class>"
                    "<tt:Type Likelihood=\"0.0\">Human</tt:Type>"
                  "</tt:Class>"
                "</tt:Appearance>"
              "</tt:Object>"
            "</tan:SampleFrame>"
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
                "<tt:ParentTopic>tns1:VideoSource/MotionAlarm</tt:ParentTopic>"
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
    std::ostringstream os;
    os << "<tan:GetAnalyticsModulesResponse>";
    for (const auto& m : AnalyticsModuleStore::instance().list()) {
        os << "<tan:AnalyticsModule Name=\"" << m.name
           << "\" Type=\"" << m.type << "\">"
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
    // Name/Type là ATTRIBUTE của <AnalyticsModule Name=".." Type="q1:CellMotionEngine">.
    std::string name = extractAttr(req, "AnalyticsModule", "Name");
    std::string type = extractAttr(req, "AnalyticsModule", "Type");
    // Strip prefix (q1:CellMotionEngine → CellMotionEngine), chuẩn hóa về tt:.
    auto c = type.find(':');
    if (c != std::string::npos) type = type.substr(c + 1);
    if (type.empty()) type = "ObjectDetection";
    if (!name.empty())
        AnalyticsModuleStore::instance().add(name, "tt:" + type);
    return "<tan:CreateAnalyticsModulesResponse/>";
}

// ── DeleteAnalyticsModules ───────────────────────────────────────────────────
std::string AnalyticsService::handleDeleteAnalyticsModules(const std::string& req) {
    // Delete gửi tên trong element <AnalyticsModuleName>NAME</AnalyticsModuleName>.
    std::string name = extractInnerTag(req, "AnalyticsModuleName");
    if (!name.empty())
        AnalyticsModuleStore::instance().remove(name);
    return "<tan:DeleteAnalyticsModulesResponse/>";
}

// ── ModifyAnalyticsModules ───────────────────────────────────────────────────
std::string AnalyticsService::handleModifyAnalyticsModules(const std::string& req) {
    // Modify: upsert module (tool sau đó verify qua GetAnalyticsModules).
    std::string name = extractAttr(req, "AnalyticsModule", "Name");
    std::string type = extractAttr(req, "AnalyticsModule", "Type");
    auto c = type.find(':');
    if (c != std::string::npos) type = type.substr(c + 1);
    if (!name.empty())
        AnalyticsModuleStore::instance().add(name, type.empty() ? "tt:ObjectDetection" : "tt:" + type);
    return "<tan:ModifyAnalyticsModulesResponse/>";
}

// ── GetAnalyticsModuleOptions ────────────────────────────────────────────────
std::string AnalyticsService::handleGetAnalyticsModuleOptions(const std::string& req) {
    (void)req;
    return
        "<tan:GetAnalyticsModuleOptionsResponse>"
          "<tan:Options Name=\"Sensitivity\" Type=\"xs:int\">"
            "<tt:IntRange>"
              "<tt:Min>0</tt:Min><tt:Max>100</tt:Max>"
            "</tt:IntRange>"
          "</tan:Options>"
        "</tan:GetAnalyticsModuleOptionsResponse>";
}
