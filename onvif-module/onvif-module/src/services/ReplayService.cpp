// ReplayService.cpp — ONVIF Replay Control ver10 (Profile G) — string-based.
// STUB-FEATURES: GetServiceCapabilities + GetReplayUri (URI RTSP tĩnh) +
// Get/SetReplayConfiguration. ReversePlayback=false (scope bỏ reverse). RTSP
// replay pipeline thật (RTP ext 0xABAC, Range, onvif-replay) là Phase streaming.

#include "services/ReplayService.h"
#include "utils/FaultBuilder.h"
#include <mutex>
#include <sstream>

namespace {
const char* NS_REPLAY = "http://www.onvif.org/ver10/replay/wsdl";
const char* ACT = "http://www.onvif.org/ver10/replay/wsdl/ReplayPort/";

// Replay endpoint dùng relay gortsplib 8555. Host lấy từ HTTP request để URI
// reachable từ DTT, không trả loopback của DUT.
std::string requestHost(const std::string& req) {
    for (const char* key : {"\r\nHost:", "\nHost:"}) {
        auto p = req.find(key);
        if (p == std::string::npos) continue;
        p += std::char_traits<char>::length(key);
        p = req.find_first_not_of(" \t", p);
        if (p == std::string::npos) return "";
        auto e = req.find_first_of(":\r\n", p);
        if (e == std::string::npos) return "";
        const auto host = req.substr(p, e - p);
        if (!host.empty() && host.find_first_not_of(
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-") ==
                std::string::npos)
            return host;
    }
    return "";
}

std::mutex g_replayConfigMtx;
std::string g_sessionTimeout = "PT60S";

std::string getSessionTimeout() {
    std::lock_guard<std::mutex> lock(g_replayConfigMtx);
    return g_sessionTimeout;
}

void setSessionTimeout(const std::string& value) {
    std::lock_guard<std::mutex> lock(g_replayConfigMtx);
    g_sessionTimeout = value;
}
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

std::string ReplayService::extractInnerTag(const std::string& xml,
                                           const std::string& tag) {
    size_t p = 0;
    while ((p = xml.find(tag, p)) != std::string::npos) {
        const auto after = p + tag.size();
        if (after < xml.size() && xml[after] != '>' && xml[after] != ' ' &&
            xml[after] != '\t' && xml[after] != '\r' && xml[after] != '\n') {
            p = after;
            continue;
        }
        const auto open = xml.rfind('<', p);
        if (open == std::string::npos || (open + 1 < xml.size() && xml[open + 1] == '/')) {
            p = after;
            continue;
        }
        const auto gt = xml.find('>', after);
        if (gt == std::string::npos) return "";
        const auto lt = xml.find('<', gt + 1);
        if (lt == std::string::npos) return "";
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

    if (has("GetReplayUri")) {
        if (extractInnerTag(req, "RecordingToken") != "Recording_0")
            return FaultBuilder::sender("ter:InvalidArgVal", "ter:NoRecording",
                                        "No recording with the given token");
        const auto host = requestHost(req);
        if (host.empty())
            return FaultBuilder::receiver("ter:Action", "Replay endpoint unavailable");
        return R("GetReplayUriResponse",
            "<trp:GetReplayUriResponse>"
              "<trp:Uri>rtsp://" + host + ":8555/replay</trp:Uri>"
            "</trp:GetReplayUriResponse>");
    }

    if (has("GetReplayConfiguration"))
        return R("GetReplayConfigurationResponse",
            "<trp:GetReplayConfigurationResponse>"
              "<trp:Configuration>"
                "<tt:SessionTimeout>" + getSessionTimeout() + "</tt:SessionTimeout>"
              "</trp:Configuration>"
            "</trp:GetReplayConfigurationResponse>");

    if (has("SetReplayConfiguration")) {
        auto timeout = extractInnerTag(req, "SessionTimeout");
        if (!timeout.empty()) setSessionTimeout(timeout);
        return R("SetReplayConfigurationResponse",
            "<trp:SetReplayConfigurationResponse/>");
    }

    return "";  // op không nhận diện → OnvifServer fallback fault.
}
