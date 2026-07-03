// DeviceIOHandler.cpp — manual XML cho DeviceIO service (ver10/deviceIO/wsdl).
// Cần cho Profile T §7.10.3 (GetVideoSources mandatory qua DeviceIO namespace).

#include "services/DeviceIOHandler.h"
#include <sstream>

namespace {
const char* NS_DEVICEIO = "http://www.onvif.org/ver10/deviceIO/wsdl";
const char* ACT_CAPS_RESP =
    "http://www.onvif.org/ver10/deviceIO/wsdl/DeviceIO/GetServiceCapabilitiesResponse";
const char* ACT_GET_VS_RESP =
    "http://www.onvif.org/ver10/deviceIO/wsdl/DeviceIO/GetVideoSourcesResponse";
const char* ACT_GET_AS_RESP =
    "http://www.onvif.org/ver10/deviceIO/wsdl/DeviceIO/GetAudioSourcesResponse";
const char* ACT_GET_RELAY_RESP =
    "http://www.onvif.org/ver10/deviceIO/wsdl/DeviceIO/GetRelayOutputsResponse";
const char* ACT_GET_DIN_RESP =
    "http://www.onvif.org/ver10/deviceIO/wsdl/DeviceIO/GetDigitalInputsResponse";
} // namespace

std::string DeviceIOHandler::extractMessageId(const std::string& xml) {
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

std::string DeviceIOHandler::wrap(const std::string& action,
                                  const std::string& relatesTo,
                                  const std::string& bodyXml) {
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
       << "<SOAP-ENV:Envelope"
       << " xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\""
       << " xmlns:wsa=\"http://www.w3.org/2005/08/addressing\""
       << " xmlns:tmd=\"" << NS_DEVICEIO << "\""
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

std::string DeviceIOHandler::handleGetServiceCapabilities() {
    // Declare capability tối thiểu (không hỗ trợ Audio backchannel, Relay, DigitalInput).
    return
        "<tmd:GetServiceCapabilitiesResponse>"
          "<tmd:Capabilities"
             " VideoSources=\"1\""
             " VideoOutputs=\"0\""
             " AudioSources=\"0\""
             " AudioOutputs=\"0\""
             " RelayOutputs=\"0\""
             " DigitalInputs=\"0\""
             " SerialPorts=\"0\"/>"
        "</tmd:GetServiceCapabilitiesResponse>";
}

std::string DeviceIOHandler::handleGetVideoSources() {
    // Profile T §7.10.3 mandatory: trả list VideoSource tokens.
    // DeviceIO GetVideoSources trả tt:VideoSource item (khác Media1 trt:VideoSources).
    // Schema: <tmd:VideoSources>...token...</tmd:VideoSources> — nội dung là tt:VideoSource
    // hoặc chỉ token tuỳ WSDL version. Dùng dạng phổ biến nhất: element rỗng với
    // attribute token.
    return
        "<tmd:GetVideoSourcesResponse>"
          "<tmd:VideoSources token=\"src_main\"/>"
        "</tmd:GetVideoSourcesResponse>";
}

std::string DeviceIOHandler::handleGetAudioSources() {
    return "<tmd:GetAudioSourcesResponse/>";
}

std::string DeviceIOHandler::handleGetRelayOutputs() {
    return "<tmd:GetRelayOutputsResponse/>";
}

std::string DeviceIOHandler::handleGetDigitalInputs() {
    return "<tmd:GetDigitalInputsResponse/>";
}

std::string DeviceIOHandler::dispatch(const std::string& req) {
    // Chỉ xử lý request có namespace DeviceIO.
    if (req.find(NS_DEVICEIO) == std::string::npos) return "";
    std::string rel = extractMessageId(req);

    if (req.find("GetServiceCapabilities") != std::string::npos)
        return wrap(ACT_CAPS_RESP, rel, handleGetServiceCapabilities());
    if (req.find("GetVideoSources") != std::string::npos)
        return wrap(ACT_GET_VS_RESP, rel, handleGetVideoSources());
    if (req.find("GetAudioSources") != std::string::npos)
        return wrap(ACT_GET_AS_RESP, rel, handleGetAudioSources());
    if (req.find("GetRelayOutputs") != std::string::npos)
        return wrap(ACT_GET_RELAY_RESP, rel, handleGetRelayOutputs());
    if (req.find("GetDigitalInputs") != std::string::npos)
        return wrap(ACT_GET_DIN_RESP, rel, handleGetDigitalInputs());
    return "";
}
