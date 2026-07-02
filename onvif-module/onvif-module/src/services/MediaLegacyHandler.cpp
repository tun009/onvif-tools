// MediaLegacyHandler.cpp — trả manual XML cho các op Media ver10.
// Chỉ implement subset đủ cho test tool Imaging: GetVideoSources.

#include "services/MediaLegacyHandler.h"
#include <sstream>

namespace {
const char* NS_MEDIA1 = "http://www.onvif.org/ver10/media/wsdl";
const char* ACT_GET_VIDEOSOURCES_RESP =
    "http://www.onvif.org/ver10/media/wsdl/Media/GetVideoSourcesResponse";
} // namespace

std::string MediaLegacyHandler::extractMessageId(const std::string& xml) {
    size_t p = xml.find("MessageID");
    if (p == std::string::npos) return "";
    size_t gt = xml.find('>', p);
    if (gt == std::string::npos) return "";
    size_t end = xml.find('<', gt + 1);
    if (end == std::string::npos) return "";
    std::string v = xml.substr(gt + 1, end - gt - 1);
    size_t a = v.find_first_not_of(" \t\r\n");
    size_t b = v.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return v.substr(a, b - a + 1);
}

std::string MediaLegacyHandler::wrap(const std::string& action,
                                     const std::string& relatesTo,
                                     const std::string& bodyXml) {
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
       << "<SOAP-ENV:Envelope"
       << " xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\""
       << " xmlns:wsa=\"http://www.w3.org/2005/08/addressing\""
       << " xmlns:trt=\"" << NS_MEDIA1 << "\""
       << " xmlns:tt=\"http://www.onvif.org/ver10/schema\">"
       << "<SOAP-ENV:Header>"
       << "<wsa:Action>" << action << "</wsa:Action>";
    if (!relatesTo.empty())
        os << "<wsa:RelatesTo>" << relatesTo << "</wsa:RelatesTo>";
    os << "</SOAP-ENV:Header>"
       << "<SOAP-ENV:Body>" << bodyXml << "</SOAP-ENV:Body>"
       << "</SOAP-ENV:Envelope>";
    return os.str();
}

std::string MediaLegacyHandler::handleGetVideoSources() {
    // Trả 1 VideoSource với token khớp Media2Service (src_main).
    // Framerate/Resolution: 1080p @ 30 fps (mock).
    std::string body =
        "<trt:GetVideoSourcesResponse>"
          "<trt:VideoSources token=\"src_main\">"
            "<tt:Framerate>30.0</tt:Framerate>"
            "<tt:Resolution>"
              "<tt:Width>1920</tt:Width>"
              "<tt:Height>1080</tt:Height>"
            "</tt:Resolution>"
          "</trt:VideoSources>"
        "</trt:GetVideoSourcesResponse>";
    return body;
}

std::string MediaLegacyHandler::dispatch(const std::string& req) {
    // Chỉ nhận diện Media ver10 (namespace ver10/media/wsdl).
    // Media ver20 (Media2) sẽ do gSOAP-generated dispatch xử lý bình thường.
    const bool isMedia1 = req.find(NS_MEDIA1) != std::string::npos;
    if (!isMedia1) return "";

    if (req.find("GetVideoSources") != std::string::npos) {
        std::string rel = extractMessageId(req);
        return wrap(ACT_GET_VIDEOSOURCES_RESP, rel, handleGetVideoSources());
    }
    return "";
}
