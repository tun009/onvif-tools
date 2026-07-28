// ReplayService.cpp — ONVIF Replay Control ver10 (Profile G) — string-based.
// STUB-FEATURES: GetServiceCapabilities + GetReplayUri (URI RTSP tĩnh) +
// Get/SetReplayConfiguration. ReversePlayback=false (scope bỏ reverse). RTSP
// replay pipeline thật (RTP ext 0xABAC, Range, onvif-replay) là Phase streaming.

#include "services/ReplayService.h"
#include <sstream>

namespace {
const char* NS_REPLAY = "http://www.onvif.org/ver10/replay/wsdl";
const char* ACT = "http://www.onvif.org/ver10/replay/wsdl/ReplayPort/";

// URI replay tĩnh — endpoint /replay trên relay 8555 (chưa dựng pipeline; Phase
// streaming sẽ hiện thực). ponytail: host tĩnh 127.0.0.1 — thay bằng deviceIp
// thật khi làm RTSP replay endpoint.
const char* REPLAY_URI = "rtsp://127.0.0.1:8555/replay";
} // namespace

std::string ReplayService::extractRelatesTo(const std::string& xml) {
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

std::string ReplayService::wrap(const std::string& action,
                                const std::string& relatesTo,
                                const std::string& bodyXml) {
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
       << "<SOAP-ENV:Envelope"
       << " xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\""
       << " xmlns:wsa=\"http://www.w3.org/2005/08/addressing\""
       << " xmlns:trp=\"" << NS_REPLAY << "\""
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

std::string ReplayService::handle(const std::string& req) {
    std::string rel = extractRelatesTo(req);
    auto R = [&](const char* op, const std::string& body) {
        return wrap(std::string(ACT) + op, rel, body);
    };
    auto has = [&](const char* s) { return req.find(s) != std::string::npos; };

    if (has("GetServiceCapabilities"))
        return R("GetServiceCapabilitiesResponse",
            "<trp:GetServiceCapabilitiesResponse>"
              "<trp:Capabilities ReversePlayback=\"false\" "
               "SessionTimeoutRange=\"0 4294967295\" RTP_RTSP_TCP=\"true\"/>"
            "</trp:GetServiceCapabilitiesResponse>");

    if (has("GetReplayUri"))
        return R("GetReplayUriResponse",
            "<trp:GetReplayUriResponse>"
              "<trp:Uri>" + std::string(REPLAY_URI) + "</trp:Uri>"
            "</trp:GetReplayUriResponse>");

    if (has("GetReplayConfiguration"))
        return R("GetReplayConfigurationResponse",
            "<trp:GetReplayConfigurationResponse>"
              "<trp:Configuration>"
                "<tt:SessionTimeout>PT60S</tt:SessionTimeout>"
              "</trp:Configuration>"
            "</trp:GetReplayConfigurationResponse>");

    if (has("SetReplayConfiguration"))
        return R("SetReplayConfigurationResponse",
            "<trp:SetReplayConfigurationResponse/>");

    return "";  // op không nhận diện → OnvifServer fallback fault.
}
