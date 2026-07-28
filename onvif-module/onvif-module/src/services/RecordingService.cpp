// RecordingService.cpp — ONVIF Recording Control ver10 (Profile G) — string-based.
// SKELETON: GetServiceCapabilities. Non-dynamic, 1 recording dựng sẵn, H264.

#include "services/RecordingService.h"
#include <sstream>

namespace {
const char* NS_RECORDING = "http://www.onvif.org/ver10/recording/wsdl";
const char* ACT = "http://www.onvif.org/ver10/recording/wsdl/RecordingPort/";
} // namespace

std::string RecordingService::extractRelatesTo(const std::string& xml) {
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

std::string RecordingService::wrap(const std::string& action,
                                   const std::string& relatesTo,
                                   const std::string& bodyXml) {
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
       << "<SOAP-ENV:Envelope"
       << " xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\""
       << " xmlns:wsa=\"http://www.w3.org/2005/08/addressing\""
       << " xmlns:trc=\"" << NS_RECORDING << "\""
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

std::string RecordingService::handle(const std::string& req) {
    std::string rel = extractRelatesTo(req);
    if (req.find("GetServiceCapabilities") != std::string::npos)
        return wrap(std::string(ACT) + "GetServiceCapabilitiesResponse", rel,
                    handleGetServiceCapabilities());
    return "";  // op không nhận diện → OnvifServer fallback fault.
}

std::string RecordingService::handleGetServiceCapabilities() {
    return
        "<trc:GetServiceCapabilitiesResponse>"
          "<trc:Capabilities DynamicRecordings=\"false\" DynamicTracks=\"false\" "
           "DeleteData=\"false\" Encoding=\"H264\" MaxRate=\"20000\" "
           "MaxTotalRate=\"20000\" MaxRecordings=\"1\" MaxRecordingJobs=\"1\" "
           "Options=\"false\"/>"
        "</trc:GetServiceCapabilitiesResponse>";
}
