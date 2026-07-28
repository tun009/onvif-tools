// ReplayService.cpp — ONVIF Replay Control ver10 (Profile G) — string-based.
// SKELETON: GetServiceCapabilities. ReversePlayback=false (scope bỏ reverse).

#include "services/ReplayService.h"
#include <sstream>

namespace {
const char* NS_REPLAY = "http://www.onvif.org/ver10/replay/wsdl";
const char* ACT = "http://www.onvif.org/ver10/replay/wsdl/ReplayPort/";
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
    if (req.find("GetServiceCapabilities") != std::string::npos)
        return wrap(std::string(ACT) + "GetServiceCapabilitiesResponse", rel,
                    handleGetServiceCapabilities());
    return "";  // op không nhận diện → OnvifServer fallback fault.
}

std::string ReplayService::handleGetServiceCapabilities() {
    return
        "<trp:GetServiceCapabilitiesResponse>"
          "<trp:Capabilities ReversePlayback=\"false\" "
           "SessionTimeoutRange=\"0 4294967295\" RTP_RTSP_TCP=\"true\"/>"
        "</trp:GetServiceCapabilitiesResponse>";
}
