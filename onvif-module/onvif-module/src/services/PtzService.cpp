// PtzService.cpp — PTZ Service (ver20), zoom-only node.
// Xem giải thích thiết kế đầy đủ ở đầu PtzService.h.

#include "services/PtzService.h"
#include "utils/FaultBuilder.h"
#include <ctime>
#include <iostream>
#include <set>
#include <sstream>

// Định nghĩa thật: Media2Service.cpp (tra g_dynProfiles — profile tạo động
// qua CreateProfile, backend_->getProfiles() KHÔNG biết những profile này).
extern std::string resolveDynProfileSourceToken(const std::string& profileToken);

namespace {
// Generic normalized zoom space [0,1] — AlvisBackendFacade tự quy đổi sang/từ
// physical zoomValue thật (MGMT LensBounds), PtzService không cần biết physical
// range. Xem AlvisBackendFacade.cpp::normalizeZoom/denormalizeZoom.
const char* kZoomSpaceUri = "http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace";

std::string escapeXml(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            default:   out += c;
        }
    }
    return out;
}

std::string utcNow() {
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#if defined(_WIN32)
    gmtime_s(&tmv, &t);
#else
    gmtime_r(&t, &tmv);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
    return std::string(buf);
}

float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

const char* moveStatusStr(MoveStatus s) {
    switch (s) {
        case MoveStatus::MOVING: return "MOVING";
        case MoveStatus::UNKNOWN: return "UNKNOWN";
        default: return "IDLE";
    }
}
} // namespace

PtzService::PtzService(struct soap* soap, const ServiceConfig& cfg,
                       std::shared_ptr<ICameraBackend> backend)
    : PTZBindingService(soap), cfg_(cfg), backend_(std::move(backend)) {}

PTZBindingService* PtzService::copy() {
    return new PtzService(this->soap, cfg_, backend_);
}

std::string PtzService::nodeToken(const std::string& src) { return "ptz_node_" + src; }
std::string PtzService::configToken(const std::string& src) { return "ptz_config_" + src; }

std::string PtzService::sourceTokenFromNode(const std::string& tok) {
    static const std::string prefix = "ptz_node_";
    if (tok.rfind(prefix, 0) != 0) return "";
    return tok.substr(prefix.size());
}
std::string PtzService::sourceTokenFromConfig(const std::string& tok) {
    static const std::string prefix = "ptz_config_";
    if (tok.rfind(prefix, 0) != 0) return "";
    return tok.substr(prefix.size());
}

std::string PtzService::resolveProfileToSource(const std::string& profileToken) const {
    if (backend_) {
        try {
            for (const auto& p : backend_->getProfiles()) {
                if (p.token == profileToken) return p.sourceToken;
            }
        } catch (const std::exception&) {}
    }
    // Fallback: profile tạo động qua CreateProfile (Media2/Media1) — không có
    // trong backend_->getProfiles() (xem resolveDynProfileSourceToken).
    return resolveDynProfileSourceToken(profileToken);
}

int PtzService::sendXml(const std::string& body) {
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
       << "<SOAP-ENV:Envelope"
       << " xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\""
       << " xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\""
       << " xmlns:tt=\"http://www.onvif.org/ver10/schema\">"
       << "<SOAP-ENV:Body>" << body << "</SOAP-ENV:Body></SOAP-ENV:Envelope>";
    std::string xml = os.str();
    this->soap->http_content = "application/soap+xml; charset=utf-8";
    soap_response(this->soap, SOAP_FILE);
    soap_send_raw(this->soap, xml.data(), xml.size());
    soap_end_send(this->soap);
    return SOAP_STOP;
}

int PtzService::sendFault(const std::string& faultXml) {
    this->soap->http_content = "application/soap+xml; charset=utf-8";
    soap_response(this->soap, SOAP_FILE);
    soap_send_raw(this->soap, faultXml.data(), faultXml.size());
    soap_end_send(this->soap);
    return SOAP_STOP;
}

// ── Helper: enumerate distinct sourceToken thật, giữ thứ tự xuất hiện ──────
static std::vector<std::string> distinctSourceTokens(ICameraBackend& backend) {
    std::vector<std::string> result;
    std::set<std::string> seen;
    for (const auto& p : backend.getProfiles()) {
        if (seen.insert(p.sourceToken).second) result.push_back(p.sourceToken);
    }
    return result;
}

// ── GetServiceCapabilities ─────────────────────────────────────────────────
int PtzService::GetServiceCapabilities(_tptz__GetServiceCapabilities*,
                                       _tptz__GetServiceCapabilitiesResponse&) {
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    return sendXml(
        "<tptz:GetServiceCapabilitiesResponse>"
          "<tptz:Capabilities EFlip=\"false\" Reverse=\"false\" "
           "GetCompatibleConfigurations=\"true\" MoveStatus=\"true\" "
           "StatusPosition=\"true\"/>"
        "</tptz:GetServiceCapabilitiesResponse>");
}

// ── GetNodes ────────────────────────────────────────────────────────────────
int PtzService::GetNodes(_tptz__GetNodes*, _tptz__GetNodesResponse&) {
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!backend_) return sendFault(FaultBuilder::receiver("ter:Action", "Backend unavailable"));

    std::vector<std::string> sources;
    try { sources = distinctSourceTokens(*backend_); }
    catch (const std::exception& e) {
        std::cerr << "[PtzService] GetNodes backend error: " << e.what() << "\n";
        return sendFault(FaultBuilder::receiver("ter:Action", "Backend unavailable"));
    }

    std::ostringstream body;
    body << "<tptz:GetNodesResponse>";
    for (const auto& src : sources) {
        body << "<tptz:PTZNode token=\"" << escapeXml(nodeToken(src)) << "\" "
              "FixedHomePosition=\"true\">"
              "<tt:Name>PTZ Node " << escapeXml(src) << "</tt:Name>"
              "<tt:SupportedPTZSpaces>"
                "<tt:AbsoluteZoomPositionSpace>"
                  "<tt:URI>" << kZoomSpaceUri << "</tt:URI>"
                  "<tt:XRange><tt:Min>0</tt:Min><tt:Max>1</tt:Max></tt:XRange>"
                "</tt:AbsoluteZoomPositionSpace>"
              "</tt:SupportedPTZSpaces>"
              "<tt:MaximumNumberOfPresets>0</tt:MaximumNumberOfPresets>"
              "<tt:HomeSupported>false</tt:HomeSupported>"
             "</tptz:PTZNode>";
    }
    body << "</tptz:GetNodesResponse>";
    return sendXml(body.str());
}

// ── GetNode ─────────────────────────────────────────────────────────────────
int PtzService::GetNode(_tptz__GetNode *req, _tptz__GetNodeResponse&) {
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    const std::string src = sourceTokenFromNode(req ? req->NodeToken : "");
    if (src.empty()) {
        return sendFault(FaultBuilder::sender("ter:InvalidArgVal", "ter:NoEntity",
                                              "No PTZNode with the given token"));
    }
    std::ostringstream body;
    body << "<tptz:GetNodeResponse>"
          "<tptz:PTZNode token=\"" << escapeXml(nodeToken(src)) << "\" "
          "FixedHomePosition=\"true\">"
          "<tt:Name>PTZ Node " << escapeXml(src) << "</tt:Name>"
          "<tt:SupportedPTZSpaces>"
            "<tt:AbsoluteZoomPositionSpace>"
              "<tt:URI>" << kZoomSpaceUri << "</tt:URI>"
              "<tt:XRange><tt:Min>0</tt:Min><tt:Max>1</tt:Max></tt:XRange>"
            "</tt:AbsoluteZoomPositionSpace>"
          "</tt:SupportedPTZSpaces>"
          "<tt:MaximumNumberOfPresets>0</tt:MaximumNumberOfPresets>"
          "<tt:HomeSupported>false</tt:HomeSupported>"
         "</tptz:PTZNode>"
         "</tptz:GetNodeResponse>";
    return sendXml(body.str());
}

// ── GetConfigurations ────────────────────────────────────────────────────────
int PtzService::GetConfigurations(_tptz__GetConfigurations*, _tptz__GetConfigurationsResponse&) {
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!backend_) return sendFault(FaultBuilder::receiver("ter:Action", "Backend unavailable"));

    std::vector<std::string> sources;
    try { sources = distinctSourceTokens(*backend_); }
    catch (const std::exception& e) {
        std::cerr << "[PtzService] GetConfigurations backend error: " << e.what() << "\n";
        return sendFault(FaultBuilder::receiver("ter:Action", "Backend unavailable"));
    }

    std::ostringstream body;
    body << "<tptz:GetConfigurationsResponse>";
    for (const auto& src : sources) {
        body << "<tptz:PTZConfiguration token=\"" << escapeXml(configToken(src)) << "\">"
              "<tt:Name>PTZ Configuration " << escapeXml(src) << "</tt:Name>"
              "<tt:UseCount>1</tt:UseCount>"
              "<tt:NodeToken>" << escapeXml(nodeToken(src)) << "</tt:NodeToken>"
              "<tt:DefaultAbsoluteZoomPositionSpace>" << kZoomSpaceUri
                << "</tt:DefaultAbsoluteZoomPositionSpace>"
             "</tptz:PTZConfiguration>";
    }
    body << "</tptz:GetConfigurationsResponse>";
    return sendXml(body.str());
}

// ── GetConfiguration ─────────────────────────────────────────────────────────
int PtzService::GetConfiguration(_tptz__GetConfiguration *req, _tptz__GetConfigurationResponse&) {
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    const std::string src = sourceTokenFromConfig(req ? req->PTZConfigurationToken : "");
    if (src.empty()) {
        return sendFault(FaultBuilder::sender("ter:InvalidArgVal", "ter:NoToken",
                                              "Invalid PTZConfigurationToken"));
    }
    std::ostringstream body;
    body << "<tptz:GetConfigurationResponse>"
          "<tptz:PTZConfiguration token=\"" << escapeXml(configToken(src)) << "\">"
            "<tt:Name>PTZ Configuration " << escapeXml(src) << "</tt:Name>"
            "<tt:UseCount>1</tt:UseCount>"
            "<tt:NodeToken>" << escapeXml(nodeToken(src)) << "</tt:NodeToken>"
            "<tt:DefaultAbsoluteZoomPositionSpace>" << kZoomSpaceUri
              << "</tt:DefaultAbsoluteZoomPositionSpace>"
          "</tptz:PTZConfiguration>"
         "</tptz:GetConfigurationResponse>";
    return sendXml(body.str());
}

// ── GetConfigurationOptions ──────────────────────────────────────────────────
int PtzService::GetConfigurationOptions(_tptz__GetConfigurationOptions *req,
                                        _tptz__GetConfigurationOptionsResponse&) {
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    const std::string src = sourceTokenFromConfig(req ? req->ConfigurationToken : "");
    if (src.empty()) {
        return sendFault(FaultBuilder::sender("ter:InvalidArgVal", "ter:NoToken",
                                              "Invalid PTZConfigurationToken"));
    }
    return sendXml(
        "<tptz:GetConfigurationOptionsResponse>"
          "<tptz:PTZConfigurationOptions>"
            "<tt:Spaces>"
              "<tt:AbsoluteZoomPositionSpace>"
                "<tt:URI>" + std::string(kZoomSpaceUri) + "</tt:URI>"
                "<tt:XRange><tt:Min>0</tt:Min><tt:Max>1</tt:Max></tt:XRange>"
              "</tt:AbsoluteZoomPositionSpace>"
            "</tt:Spaces>"
            // Không hỗ trợ ContinuousMove (không velocity) → PTZTimeout cố
            // định 0 (không dùng), tránh quảng bá timeout-based move giả.
            "<tt:PTZTimeout><tt:Min>PT0S</tt:Min><tt:Max>PT0S</tt:Max></tt:PTZTimeout>"
          "</tptz:PTZConfigurationOptions>"
        "</tptz:GetConfigurationOptionsResponse>");
}

// ── SetConfiguration ─────────────────────────────────────────────────────────
int PtzService::SetConfiguration(_tptz__SetConfiguration *req, _tptz__SetConfigurationResponse&) {
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    const std::string tok = (req && req->PTZConfiguration) ? req->PTZConfiguration->token : "";
    const std::string src = sourceTokenFromConfig(tok);
    if (src.empty()) {
        return sendFault(FaultBuilder::sender("ter:InvalidArgVal", "ter:NoToken",
                                              "Invalid PTZConfigurationToken"));
    }
    // Configuration cố định 1:1 theo sourceToken thật, không có field nào
    // user-configurable (không pan/tilt, không đổi default space) — chấp nhận
    // request hợp lệ nhưng không đổi gì, khớp docs Phase 5 "chưa làm".
    return sendXml("<tptz:SetConfigurationResponse/>");
}

// ── GetCompatibleConfigurations ─────────────────────────────────────────────
int PtzService::GetCompatibleConfigurations(_tptz__GetCompatibleConfigurations *req,
                                            _tptz__GetCompatibleConfigurationsResponse&) {
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    const std::string src = resolveProfileToSource(req ? req->ProfileToken : "");
    if (src.empty()) return sendFault(FaultBuilder::noProfile());

    return sendXml(
        "<tptz:GetCompatibleConfigurationsResponse>"
          "<tptz:PTZConfiguration token=\"" + escapeXml(configToken(src)) + "\">"
            "<tt:Name>PTZ Configuration " + escapeXml(src) + "</tt:Name>"
            "<tt:UseCount>1</tt:UseCount>"
            "<tt:NodeToken>" + escapeXml(nodeToken(src)) + "</tt:NodeToken>"
            "<tt:DefaultAbsoluteZoomPositionSpace>" + std::string(kZoomSpaceUri) +
              "</tt:DefaultAbsoluteZoomPositionSpace>"
          "</tptz:PTZConfiguration>"
        "</tptz:GetCompatibleConfigurationsResponse>");
}

// ── AbsoluteMove ─────────────────────────────────────────────────────────────
int PtzService::AbsoluteMove(_tptz__AbsoluteMove *req, _tptz__AbsoluteMoveResponse&) {
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!req) return sendFault(FaultBuilder::invalidArgVal("Missing request"));
    const std::string src = resolveProfileToSource(req->ProfileToken);
    if (src.empty()) return sendFault(FaultBuilder::noProfile());

    if (!req->Position) {
        return sendFault(FaultBuilder::invalidArgVal("Missing PTZ Position"));
    }
    if (req->Position->PanTilt) {
        // Node zoom-only — không có hardware pan/tilt (xem PtzService.h).
        return sendFault(FaultBuilder::sender("ter:InvalidArgVal", "ter:NoSpace",
                                              "PanTilt not supported: zoom-only PTZ node"));
    }
    if (!req->Position->Zoom) {
        return sendFault(FaultBuilder::invalidArgVal("Missing Zoom position"));
    }

    PTZVector pos;
    pos.zoom = clamp01(req->Position->Zoom->x);
    PTZVector speed;
    speed.zoom = (req->Speed && req->Speed->Zoom) ? clamp01(req->Speed->Zoom->x) : 1.0f;

    try {
        if (!backend_->ptzAbsoluteMove(src, pos, speed)) {
            return sendFault(FaultBuilder::receiver("ter:Action", "AbsoluteMove rejected by backend"));
        }
    } catch (const std::exception& e) {
        std::cerr << "[PtzService] AbsoluteMove backend error: " << e.what() << "\n";
        return sendFault(FaultBuilder::receiver("ter:Action", "PTZ backend unavailable"));
    }
    return sendXml("<tptz:AbsoluteMoveResponse/>");
}

// ── Stop ──────────────────────────────────────────────────────────────────────
int PtzService::Stop(_tptz__Stop *req, _tptz__StopResponse&) {
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!req) return sendFault(FaultBuilder::invalidArgVal("Missing request"));
    const std::string src = resolveProfileToSource(req->ProfileToken);
    if (src.empty()) return sendFault(FaultBuilder::noProfile());

    // Theo spec: nếu PanTilt/Zoom đều không set trong request → dừng tất cả axes.
    const bool stopPanTilt = req->PanTilt ? *req->PanTilt : true;
    const bool stopZoom    = req->Zoom    ? *req->Zoom    : true;
    try {
        backend_->ptzStop(src, stopPanTilt, stopZoom);
    } catch (const std::exception& e) {
        std::cerr << "[PtzService] Stop backend error: " << e.what() << "\n";
        return sendFault(FaultBuilder::receiver("ter:Action", "PTZ backend unavailable"));
    }
    return sendXml("<tptz:StopResponse/>");
}

// ── GetStatus ─────────────────────────────────────────────────────────────────
int PtzService::GetStatus(_tptz__GetStatus *req, _tptz__GetStatusResponse&) {
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!req) return sendFault(FaultBuilder::invalidArgVal("Missing request"));
    const std::string src = resolveProfileToSource(req->ProfileToken);
    if (src.empty()) return sendFault(FaultBuilder::noProfile());

    PTZStatus status;
    try {
        status = backend_->getPtzStatus(src);
    } catch (const std::exception& e) {
        std::cerr << "[PtzService] GetStatus backend error: " << e.what() << "\n";
        return sendFault(FaultBuilder::receiver("ter:Action", "PTZ backend unavailable"));
    }

    std::ostringstream body;
    body << "<tptz:GetStatusResponse>"
          "<tptz:PTZStatus>"
            "<tt:Position><tt:Zoom x=\"" << clamp01(status.position.zoom) << "\" "
              "space=\"" << kZoomSpaceUri << "\"/></tt:Position>"
            "<tt:MoveStatus><tt:Zoom>" << moveStatusStr(status.zoomStatus)
              << "</tt:Zoom></tt:MoveStatus>"
            "<tt:UtcTime>" << utcNow() << "</tt:UtcTime>"
          "</tptz:PTZStatus>"
         "</tptz:GetStatusResponse>";
    return sendXml(body.str());
}

// ── Không hỗ trợ: luôn ter:ActionNotSupported ────────────────────────────────
#define PTZ_NOT_SUPPORTED(Method, ReqT, RespT) \
    int PtzService::Method(ReqT*, RespT&) { \
        this->soap->mustUnderstand = 0; this->soap->header = nullptr; \
        return sendFault(FaultBuilder::actionNotSupported()); \
    }

PTZ_NOT_SUPPORTED(GetPresets, _tptz__GetPresets, _tptz__GetPresetsResponse)
PTZ_NOT_SUPPORTED(SetPreset, _tptz__SetPreset, _tptz__SetPresetResponse)
PTZ_NOT_SUPPORTED(RemovePreset, _tptz__RemovePreset, _tptz__RemovePresetResponse)
PTZ_NOT_SUPPORTED(GotoPreset, _tptz__GotoPreset, _tptz__GotoPresetResponse)
PTZ_NOT_SUPPORTED(ContinuousMove, _tptz__ContinuousMove, _tptz__ContinuousMoveResponse)
PTZ_NOT_SUPPORTED(RelativeMove, _tptz__RelativeMove, _tptz__RelativeMoveResponse)
PTZ_NOT_SUPPORTED(SendAuxiliaryCommand, _tptz__SendAuxiliaryCommand, _tptz__SendAuxiliaryCommandResponse)
PTZ_NOT_SUPPORTED(GeoMove, _tptz__GeoMove, _tptz__GeoMoveResponse)
PTZ_NOT_SUPPORTED(GotoHomePosition, _tptz__GotoHomePosition, _tptz__GotoHomePositionResponse)
PTZ_NOT_SUPPORTED(SetHomePosition, _tptz__SetHomePosition, _tptz__SetHomePositionResponse)
PTZ_NOT_SUPPORTED(GetPresetTours, _tptz__GetPresetTours, _tptz__GetPresetToursResponse)
PTZ_NOT_SUPPORTED(GetPresetTour, _tptz__GetPresetTour, _tptz__GetPresetTourResponse)
PTZ_NOT_SUPPORTED(GetPresetTourOptions, _tptz__GetPresetTourOptions, _tptz__GetPresetTourOptionsResponse)
PTZ_NOT_SUPPORTED(CreatePresetTour, _tptz__CreatePresetTour, _tptz__CreatePresetTourResponse)
PTZ_NOT_SUPPORTED(ModifyPresetTour, _tptz__ModifyPresetTour, _tptz__ModifyPresetTourResponse)
PTZ_NOT_SUPPORTED(OperatePresetTour, _tptz__OperatePresetTour, _tptz__OperatePresetTourResponse)
PTZ_NOT_SUPPORTED(RemovePresetTour, _tptz__RemovePresetTour, _tptz__RemovePresetTourResponse)
PTZ_NOT_SUPPORTED(MoveAndStartTracking, _tptz__MoveAndStartTracking, _tptz__MoveAndStartTrackingResponse)

#undef PTZ_NOT_SUPPORTED
