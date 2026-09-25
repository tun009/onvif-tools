// MediaLegacyHandler.cpp — Media Service ver10 (Profile S/G/Q backward compat).
// Toàn bộ ops trả XML thủ công. Profile/VideoSource/VideoEncoder config đọc
// THẬT từ ICameraBackend (2026-09-22) — xem docs/onvif-alvis/
// 01-IMPLEMENTATION_PLAN.md mục "Media1 (Profile S) vào backend thật".
// Chỉ còn 2 phần state cục bộ KHÔNG có nguồn thật tương ứng:
//   - g_dynProfiles/g_deletedFixed: CreateProfile/DeleteProfile (ONVIF không
//     yêu cầu DVR phải biết những thay đổi này).
//   - g_vecOverride/g_vscOverride: giá trị Set* client đã gửi, echo lại cho
//     Get sau — KHÔNG áp dụng thật lên encoder DVR (giữ nguyên compromise đã
//     dùng ở Media2Service::SetVideoEncoderConfiguration: Set chỉ là SOAP
//     state, không respawn/reconfigure stream thật, tránh làm gián đoạn các
//     stream đang được test streaming dùng chung).
// MJPEG (2026-09-24): DVR team đã bổ sung lại continuous MJPEG streaming
// (commit AlvisOS/DVR 2c74b09, do ONVIF Profile S bắt buộc — Device
// MANDATORY, xem ONVIF Profile S Specification v1.3 mục 7.9). Mỗi kênh có
// thêm 1 profile "<channel>_mjpeg" (token DVR thật, VD "0_mjpeg"/"1_mjpeg"),
// Codec::JPEG, kích thước/FPS cố định theo HAL
// (AlvisOS/DVR include/encoder_worker/encoder_worker.h: kMjpegWidth/Height,
// kMjpegMinFps/MaxFps=1-20) — không cho SetVideoEncoderConfiguration đổi
// chéo giữa profile MJPEG và profile H264/H265 (đúng khoá của chính DVR).

#include "services/MediaLegacyHandler.h"
#include "interface/ICameraBackend.h"
#include <sstream>
#include <mutex>
#include <map>
#include <set>
#include <vector>
#include <cstring>
#include <cstdio>

namespace {
const char* NS_MEDIA1 = "http://www.onvif.org/ver10/media/wsdl";
const char* ACT = "http://www.onvif.org/ver10/media/wsdl/Media/";

std::string g_deviceIp = "127.0.0.1";
int         g_httpPort = 8080;
int         g_rtspPort = 8554;
CameraBackendPtr g_backend;

std::string actUrl(const char* op) {
    return std::string(ACT) + op + "Response";
}

// Quy ước đặt tên config token cho backend thật — CÙNG quy ước
// Media2Service.cpp dùng cho nhánh backend thật (videoSourceConfigToken()),
// giữ nhất quán token giữa Media1/Media2 cho cùng 1 nguồn vật lý.
std::string videoSourceConfigToken(const std::string& sourceToken) {
    return sourceToken.empty() ? "video_source_config" : ("video_source_config_" + sourceToken);
}
std::string videoEncoderConfigToken(const std::string& profileToken) {
    return "video_encoder_config_" + profileToken;
}

// Gọi backend lấy danh sách profile thật. Không lock g_stateMtx khi gọi hàm
// này (network call) — luôn fetch TRƯỚC khi lock để đọc state cục bộ.
std::vector<StreamProfile> backendProfiles() {
    if (!g_backend) return {};
    try { return g_backend->getProfiles(); }
    catch (const std::exception&) { return {}; }
}

// ── Media1 dynamic-profile + override state ────────────────────────────────
struct DynProfile {
    std::string token;
    std::string name;
    std::string vsToken;    // empty = chưa AddVideoSourceConfiguration
    std::string veToken;    // empty = chưa AddVideoEncoderConfiguration
    std::string mdToken;    // empty = chưa AddMetadataConfiguration
};
struct VECOverride {
    bool hasEncoding = false;   std::string encoding;
    bool hasResolution = false; int width = 0, height = 0;
    bool hasFrameRate = false;  int frameRate = 0;
    bool hasBitrate = false;    int bitrate = 0;
    bool hasGovLength = false;  int govLength = 0;
    bool hasQuality = false;    int quality = 0;
    bool hasH264Profile = false; std::string h264Profile;
};
struct VSCOverride {
    bool has = false;
    int x = 0, y = 0, width = 0, height = 0;
};

std::mutex g_stateMtx;
std::map<std::string, DynProfile> g_dynProfiles;    // token -> dyn profile
std::set<std::string> g_deletedFixed;               // token backend bị DeleteProfile (ẩn, không xoá thật)
std::map<std::string, VECOverride> g_vecOverride;   // VEC token -> override
std::map<std::string, VSCOverride> g_vscOverride;   // VSC token -> override

// ── Baseline resolvers: kết hợp giá trị backend thật + override cục bộ ─────
struct VecBaseline {
    std::string encoding = "H264";
    int width = 1920, height = 1080, frameRate = 25, bitrate = 4000;
    int govLength = 30, quality = 5;
    std::string h264Profile = "Main";
};
VecBaseline resolveVecBaseline(const std::string& vecToken,
                                const std::vector<StreamProfile>& profiles) {
    VecBaseline b;
    static const std::string prefix = "video_encoder_config_";
    std::string profileToken = vecToken.rfind(prefix, 0) == 0 ? vecToken.substr(prefix.size()) : std::string();
    for (const auto& p : profiles) {
        if (p.token != profileToken) continue;
        b.encoding = (p.videoConfig.codec == Codec::JPEG) ? "JPEG"
                   : (p.videoConfig.codec == Codec::H265) ? "H265" : "H264";
        b.width = p.videoConfig.resolution.width;
        b.height = p.videoConfig.resolution.height;
        b.frameRate = p.videoConfig.framerate;
        b.bitrate = p.videoConfig.bitrate;
        if (!p.videoConfig.profile.empty()) b.h264Profile = p.videoConfig.profile;
        break;
    }
    auto it = g_vecOverride.find(vecToken);
    if (it != g_vecOverride.end()) {
        const auto& o = it->second;
        if (o.hasEncoding)    b.encoding = o.encoding;
        if (o.hasResolution)  { b.width = o.width; b.height = o.height; }
        if (o.hasFrameRate)   b.frameRate = o.frameRate;
        if (o.hasBitrate)     b.bitrate = o.bitrate;
        if (o.hasGovLength)   b.govLength = o.govLength;
        if (o.hasQuality)     b.quality = o.quality;
        if (o.hasH264Profile) b.h264Profile = o.h264Profile;
    }
    return b;
}
struct VscBaseline {
    std::string sourceToken;
    int width = 1920, height = 1080;
};
VscBaseline resolveVscBaseline(const std::string& vscToken,
                                const std::vector<StreamProfile>& profiles) {
    VscBaseline b;
    static const std::string prefix = "video_source_config_";
    b.sourceToken = vscToken.rfind(prefix, 0) == 0 ? vscToken.substr(prefix.size()) : std::string();
    bool found = false;
    for (const auto& p : profiles) {
        if (p.sourceToken != b.sourceToken) continue;
        b.width = p.sourceBounds.width;
        b.height = p.sourceBounds.height;
        found = true;
        break;
    }
    if (!found && b.sourceToken.empty() && !profiles.empty()) {
        b.sourceToken = profiles.front().sourceToken;
        b.width = profiles.front().sourceBounds.width;
        b.height = profiles.front().sourceBounds.height;
    }
    auto it = g_vscOverride.find(vscToken);
    if (it != g_vscOverride.end() && it->second.has) {
        b.width = it->second.width;
        b.height = it->second.height;
    }
    return b;
}

// UseCount = số profile (backend chưa bị Delete + dyn) đang tham chiếu config.
int countVSCUsage(const std::string& vscToken, const std::vector<StreamProfile>& profiles) {
    static const std::string prefix = "video_source_config_";
    std::string sourceToken = vscToken.rfind(prefix, 0) == 0 ? vscToken.substr(prefix.size()) : std::string();
    int count = 0;
    for (const auto& p : profiles) {
        if (p.sourceToken == sourceToken && !g_deletedFixed.count(p.token)) count++;
    }
    for (const auto& kv : g_dynProfiles) {
        if (kv.second.vsToken == vscToken) count++;
    }
    return count;
}
int countVECUsage(const std::string& vecToken, const std::vector<StreamProfile>& profiles) {
    static const std::string prefix = "video_encoder_config_";
    std::string profileToken = vecToken.rfind(prefix, 0) == 0 ? vecToken.substr(prefix.size()) : std::string();
    int count = 0;
    for (const auto& p : profiles) {
        if (p.token == profileToken && !g_deletedFixed.count(p.token)) count++;
    }
    for (const auto& kv : g_dynProfiles) {
        if (kv.second.veToken == vecToken) count++;
    }
    return count;
}
} // namespace

// Tra sourceToken của 1 profile tạo động qua Media1 CreateProfile (g_dynProfiles
// ở trên). Dùng bởi PtzService — các case PTZ-* (không phải MEDIA2_PTZ-*) dùng
// Media1 legacy CreateProfile (xmlns=".../ver10/media/wsdl") để tạo profile
// test, backend_->getProfiles() (DVR thật) không biết profile này (PTZ-3-1-1/
// 3-1-2/3-1-4/3-1-5/5-1-3/7-1-3/7-2-3 fail "No profile with the given token"
// ở r20.xml — cùng nguyên nhân đã fix cho Media2Service.cpp::g_dynProfiles,
// nhưng đây là kho lưu RIÊNG của Media1, phải export thêm 1 hàm nữa).
std::string resolveLegacyDynProfileSourceToken(const std::string& profileToken) {
    std::lock_guard<std::mutex> lk(g_stateMtx);
    auto it = g_dynProfiles.find(profileToken);
    if (it == g_dynProfiles.end()) return "";
    static const std::string prefix = "video_source_config_";
    const std::string& vs = it->second.vsToken;
    if (vs.rfind(prefix, 0) == 0) return vs.substr(prefix.size());
    return "";
}

void MediaLegacyHandler::setEndpoint(const std::string& ip, int httpPort, int rtspPort) {
    g_deviceIp = ip;
    g_httpPort = httpPort;
    g_rtspPort = rtspPort;
}

void MediaLegacyHandler::setBackend(CameraBackendPtr backend) {
    g_backend = std::move(backend);
}

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

std::string MediaLegacyHandler::extractInnerTag(const std::string& xml,
                                                const std::string& localName) {
    // Tìm element có local-name = localName (bỏ qua prefix), lấy nội dung.
    size_t search = 0;
    while (search < xml.size()) {
        size_t lt = xml.find('<', search);
        if (lt == std::string::npos) return "";
        size_t ns = lt + 1;
        if (ns < xml.size() && xml[ns] == '/') { search = lt + 1; continue; }
        size_t gt = xml.find('>', ns);
        if (gt == std::string::npos) return "";
        size_t sp = xml.find_first_of(" \t\r\n/>", ns);
        size_t nameEnd = std::min(sp, gt);
        size_t colon = xml.find(':', ns);
        size_t start = (colon != std::string::npos && colon < nameEnd) ? colon + 1 : ns;
        if (xml.substr(start, nameEnd - start) == localName) {
            if (xml[gt - 1] == '/') return "";
            size_t contentStart = gt + 1;
            size_t contentEnd = xml.find('<', contentStart);
            if (contentEnd == std::string::npos) return "";
            std::string v = xml.substr(contentStart, contentEnd - contentStart);
            size_t a = v.find_first_not_of(" \t\r\n");
            size_t b = v.find_last_not_of(" \t\r\n");
            if (a == std::string::npos) return "";
            return v.substr(a, b - a + 1);
        }
        search = lt + 1;
    }
    return "";
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

// ── Profile XML fragment ────────────────────────────────────────────────────
// Media1 tt:Profile struct: VideoSourceConfiguration + VideoEncoderConfiguration.
// fixed=true -> token là profile backend thật (đọc field trực tiếp từ
// StreamProfile khớp token). fixed=false -> token là dyn profile, VSC/VEC
// (nếu có) là token đã AddVideoSource/EncoderConfiguration trỏ tới.
std::string MediaLegacyHandler::profileXml(const char* wrapperElem,
                                            const char* token, const char* name,
                                            bool fixed, bool includeVSC, bool includeVEC) {
    std::vector<StreamProfile> profiles =
        (includeVSC || includeVEC) ? backendProfiles() : std::vector<StreamProfile>{};
    std::ostringstream os;
    os << "<trt:" << wrapperElem << " fixed=\"" << (fixed ? "true" : "false")
       << "\" token=\"" << token << "\">"
       << "<tt:Name>" << name << "</tt:Name>";

    std::lock_guard<std::mutex> lk(g_stateMtx);

    if (includeVSC) {
        std::string vscToken;
        if (fixed) {
            std::string sourceToken;
            for (const auto& p : profiles) {
                if (p.token == token) { sourceToken = p.sourceToken; break; }
            }
            vscToken = videoSourceConfigToken(sourceToken);
        } else {
            auto it = g_dynProfiles.find(token);
            if (it != g_dynProfiles.end()) vscToken = it->second.vsToken;
        }
        if (!vscToken.empty()) {
            VscBaseline b = resolveVscBaseline(vscToken, profiles);
            os << "<tt:VideoSourceConfiguration token=\"" << vscToken << "\">"
                 << "<tt:Name>VideoSourceConfig</tt:Name>"
                 << "<tt:UseCount>" << countVSCUsage(vscToken, profiles) << "</tt:UseCount>"
                 << "<tt:SourceToken>" << (b.sourceToken.empty() ? "src_main" : b.sourceToken) << "</tt:SourceToken>"
                 << "<tt:Bounds x=\"0\" y=\"0\" width=\"" << b.width << "\" height=\"" << b.height << "\"/>"
               << "</tt:VideoSourceConfiguration>";
        }
    }
    if (includeVEC) {
        std::string vecToken;
        if (fixed) {
            vecToken = videoEncoderConfigToken(token);
        } else {
            auto it = g_dynProfiles.find(token);
            if (it != g_dynProfiles.end()) vecToken = it->second.veToken;
        }
        if (!vecToken.empty()) {
            VecBaseline b = resolveVecBaseline(vecToken, profiles);
            os << "<tt:VideoEncoderConfiguration token=\"" << vecToken << "\">"
               << "<tt:Name>VideoEncoderConfig</tt:Name>"
               << "<tt:UseCount>" << countVECUsage(vecToken, profiles) << "</tt:UseCount>"
               << "<tt:Encoding>" << b.encoding << "</tt:Encoding>"
               << "<tt:Resolution>"
                 << "<tt:Width>" << b.width << "</tt:Width>"
                 << "<tt:Height>" << b.height << "</tt:Height>"
               << "</tt:Resolution>"
               << "<tt:Quality>" << b.quality << "</tt:Quality>"
               << "<tt:RateControl>"
                 << "<tt:FrameRateLimit>" << b.frameRate << "</tt:FrameRateLimit>"
                 << "<tt:EncodingInterval>1</tt:EncodingInterval>"
                 << "<tt:BitrateLimit>" << b.bitrate << "</tt:BitrateLimit>"
               << "</tt:RateControl>";
            if (b.encoding == "H264") {
                os << "<tt:H264>"
                     << "<tt:GovLength>" << b.govLength << "</tt:GovLength>"
                     << "<tt:H264Profile>" << b.h264Profile << "</tt:H264Profile>"
                   << "</tt:H264>";
            }
            os << "<tt:Multicast>"
                 << "<tt:Address><tt:Type>IPv4</tt:Type><tt:IPv4Address>239.0.0.1</tt:IPv4Address></tt:Address>"
                 << "<tt:Port>32000</tt:Port>"
                 << "<tt:TTL>1</tt:TTL>"
                 << "<tt:AutoStart>false</tt:AutoStart>"
               << "</tt:Multicast>"
               << "<tt:SessionTimeout>PT60S</tt:SessionTimeout>"
               << "</tt:VideoEncoderConfiguration>";
        }
    }
    os << "</trt:" << wrapperElem << ">";
    return os.str();
}

// ── Individual op handlers ──────────────────────────────────────────────────

std::string MediaLegacyHandler::handleGetServiceCapabilities() {
    return
        "<trt:GetServiceCapabilitiesResponse>"
          "<trt:Capabilities SnapshotUri=\"true\" Rotation=\"false\" "
            "VideoSourceMode=\"false\" OSD=\"true\">"
            "<trt:ProfileCapabilities MaximumNumberOfProfiles=\"3\"/>"
            "<trt:StreamingCapabilities RTPMulticast=\"false\" "
              "RTP_TCP=\"true\" RTP_RTSP_TCP=\"true\" NonAggregateControl=\"false\"/>"
          "</trt:Capabilities>"
        "</trt:GetServiceCapabilitiesResponse>";
}

std::string MediaLegacyHandler::handleGetVideoSources() {
    std::vector<StreamProfile> profiles = backendProfiles();
    std::set<std::string> seen;
    std::ostringstream os;
    os << "<trt:GetVideoSourcesResponse>";
    bool any = false;
    for (const auto& p : profiles) {
        if (p.sourceToken.empty() || !seen.insert(p.sourceToken).second) continue;
        any = true;
        os << "<trt:VideoSources token=\"" << p.sourceToken << "\">"
             << "<tt:Framerate>" << p.videoConfig.framerate << ".0</tt:Framerate>"
             << "<tt:Resolution><tt:Width>" << p.sourceBounds.width << "</tt:Width>"
                             << "<tt:Height>" << p.sourceBounds.height << "</tt:Height></tt:Resolution>"
           << "</trt:VideoSources>";
    }
    if (!any) {
        os << "<trt:VideoSources token=\"src_main\">"
             << "<tt:Framerate>30.0</tt:Framerate>"
             << "<tt:Resolution><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:Resolution>"
           << "</trt:VideoSources>";
    }
    os << "</trt:GetVideoSourcesResponse>";
    return os.str();
}

std::string MediaLegacyHandler::handleGetAudioSources() {
    return "<trt:GetAudioSourcesResponse/>";
}

std::string MediaLegacyHandler::handleGetProfiles() {
    std::vector<StreamProfile> profiles = backendProfiles();
    std::set<std::string> deleted;
    std::vector<DynProfile> dyns;
    {
        std::lock_guard<std::mutex> lk(g_stateMtx);
        deleted = g_deletedFixed;
        for (auto& kv : g_dynProfiles) dyns.push_back(kv.second);
    }
    std::ostringstream os;
    os << "<trt:GetProfilesResponse>";
    for (const auto& p : profiles) {
        if (deleted.count(p.token)) continue;
        os << profileXml("Profiles", p.token.c_str(), p.name.c_str(), true, true, true);
    }
    for (const auto& d : dyns) {
        bool hasVSC = !d.vsToken.empty();
        bool hasVEC = !d.veToken.empty();
        os << profileXml("Profiles", d.token.c_str(), d.name.c_str(),
                          false, hasVSC, hasVEC);
    }
    os << "</trt:GetProfilesResponse>";
    return os.str();
}

std::string MediaLegacyHandler::handleGetProfile(const std::string& req) {
    std::string token = extractInnerTag(req, "ProfileToken");
    std::vector<StreamProfile> profiles = backendProfiles();
    const StreamProfile* bp = nullptr;
    for (const auto& p : profiles) {
        if (p.token == token) { bp = &p; break; }
    }
    bool deleted = false;
    {
        std::lock_guard<std::mutex> lk(g_stateMtx);
        deleted = g_deletedFixed.count(token) > 0;
    }
    if (bp && !deleted) {
        std::ostringstream os;
        os << "<trt:GetProfileResponse>"
           << profileXml("Profile", bp->token.c_str(), bp->name.c_str(), true, true, true)
           << "</trt:GetProfileResponse>";
        return os.str();
    }
    // Dyn profile
    DynProfile dp;
    {
        std::lock_guard<std::mutex> lk(g_stateMtx);
        auto it = g_dynProfiles.find(token);
        if (it == g_dynProfiles.end()) {
            return
                "<SOAP-ENV:Fault>"
                  "<SOAP-ENV:Code>"
                    "<SOAP-ENV:Value>SOAP-ENV:Sender</SOAP-ENV:Value>"
                    "<SOAP-ENV:Subcode>"
                      "<SOAP-ENV:Value>ter:InvalidArgVal</SOAP-ENV:Value>"
                      "<SOAP-ENV:Subcode><SOAP-ENV:Value>ter:NoProfile</SOAP-ENV:Value></SOAP-ENV:Subcode>"
                    "</SOAP-ENV:Subcode>"
                  "</SOAP-ENV:Code>"
                  "<SOAP-ENV:Reason><SOAP-ENV:Text xml:lang=\"en\">Profile not found</SOAP-ENV:Text></SOAP-ENV:Reason>"
                "</SOAP-ENV:Fault>";
        }
        dp = it->second;
    }
    std::ostringstream os;
    os << "<trt:GetProfileResponse>"
       << profileXml("Profile", dp.token.c_str(), dp.name.c_str(),
                     false, !dp.vsToken.empty(), !dp.veToken.empty());
    if (!dp.mdToken.empty()) {
        const std::string marker = "</trt:Profile>";
        const std::string metadata =
            "<tt:MetadataConfiguration token=\"" + dp.mdToken + "\">"
            "<tt:Name>MetadataConfig</tt:Name><tt:UseCount>1</tt:UseCount>"
            "<tt:Analytics>true</tt:Analytics>"
            "<tt:Multicast><tt:Address><tt:Type>IPv4</tt:Type></tt:Address>"
            "<tt:Port>32001</tt:Port><tt:TTL>1</tt:TTL><tt:AutoStart>false</tt:AutoStart></tt:Multicast>"
            "<tt:SessionTimeout>PT60S</tt:SessionTimeout>"
            "</tt:MetadataConfiguration>";
        std::string profile = os.str();
        auto end = profile.rfind(marker);
        if (end != std::string::npos) profile.insert(end, metadata);
        return profile + "</trt:GetProfileResponse>";
    }
    os << "</trt:GetProfileResponse>";
    return os.str();
}

std::string MediaLegacyHandler::handleCreateProfile(const std::string& req) {
    std::string name = extractInnerTag(req, "Name");
    std::string token = extractInnerTag(req, "Token");
    if (token.empty()) token = "profile_" + name;
    {
        std::lock_guard<std::mutex> lk(g_stateMtx);
        DynProfile p; p.token = token; p.name = name;
        g_dynProfiles[token] = p;
    }
    std::ostringstream os;
    os << "<trt:CreateProfileResponse>"
       // CreateProfile: dyn profile, fixed=false, NO configs (tool expect empty).
       << profileXml("Profile", token.c_str(), name.c_str(), false, false, false)
       << "</trt:CreateProfileResponse>";
    return os.str();
}

std::string MediaLegacyHandler::handleDeleteProfile(const std::string& req) {
    std::string token = extractInnerTag(req, "ProfileToken");
    bool valid = false;
    {
        std::lock_guard<std::mutex> lk(g_stateMtx);
        if (g_dynProfiles.count(token)) {
            valid = true;
            g_dynProfiles.erase(token);
        }
    }
    if (!valid) {
        bool isBackend = false;
        for (const auto& p : backendProfiles()) {
            if (p.token == token) { isBackend = true; break; }
        }
        if (isBackend) {
            std::lock_guard<std::mutex> lk(g_stateMtx);
            if (!g_deletedFixed.count(token)) {
                g_deletedFixed.insert(token);
                valid = true;
            }
        }
    }
    if (!valid) {
        return
            "<SOAP-ENV:Fault>"
              "<SOAP-ENV:Code>"
                "<SOAP-ENV:Value>SOAP-ENV:Sender</SOAP-ENV:Value>"
                "<SOAP-ENV:Subcode>"
                  "<SOAP-ENV:Value>ter:InvalidArgVal</SOAP-ENV:Value>"
                  "<SOAP-ENV:Subcode>"
                    "<SOAP-ENV:Value>ter:NoProfile</SOAP-ENV:Value>"
                  "</SOAP-ENV:Subcode>"
                "</SOAP-ENV:Subcode>"
              "</SOAP-ENV:Code>"
              "<SOAP-ENV:Reason>"
                "<SOAP-ENV:Text xml:lang=\"en\">Profile not found</SOAP-ENV:Text>"
              "</SOAP-ENV:Reason>"
            "</SOAP-ENV:Fault>";
    }
    return "<trt:DeleteProfileResponse/>";
}

std::string MediaLegacyHandler::handleGetVideoSourceConfigurations() {
    std::vector<StreamProfile> profiles = backendProfiles();
    std::set<std::string> deleted;
    { std::lock_guard<std::mutex> lk(g_stateMtx); deleted = g_deletedFixed; }
    std::set<std::string> seen;
    std::ostringstream os;
    os << "<trt:GetVideoSourceConfigurationsResponse>";
    for (const auto& p : profiles) {
        if (deleted.count(p.token)) continue;
        if (!seen.insert(p.sourceToken).second) continue;
        std::string vscToken = videoSourceConfigToken(p.sourceToken);
        VscBaseline b;
        { std::lock_guard<std::mutex> lk(g_stateMtx); b = resolveVscBaseline(vscToken, profiles); }
        int useCount;
        { std::lock_guard<std::mutex> lk(g_stateMtx); useCount = countVSCUsage(vscToken, profiles); }
        os << "<trt:Configurations token=\"" << vscToken << "\">"
             << "<tt:Name>VideoSourceConfig</tt:Name>"
             << "<tt:UseCount>" << useCount << "</tt:UseCount>"
             << "<tt:SourceToken>" << (b.sourceToken.empty() ? "src_main" : b.sourceToken) << "</tt:SourceToken>"
             << "<tt:Bounds x=\"0\" y=\"0\" width=\"" << b.width << "\" height=\"" << b.height << "\"/>"
           << "</trt:Configurations>";
    }
    os << "</trt:GetVideoSourceConfigurationsResponse>";
    return os.str();
}

std::string MediaLegacyHandler::handleGetVideoSourceConfiguration(const std::string& req) {
    std::string cfgToken = extractInnerTag(req, "ConfigurationToken");
    std::vector<StreamProfile> profiles = backendProfiles();
    if (cfgToken.empty()) {
        cfgToken = profiles.empty() ? "video_source_config"
                                     : videoSourceConfigToken(profiles.front().sourceToken);
    }
    std::lock_guard<std::mutex> lk(g_stateMtx);
    VscBaseline b = resolveVscBaseline(cfgToken, profiles);
    int useCount = countVSCUsage(cfgToken, profiles);
    std::ostringstream os;
    os << "<trt:GetVideoSourceConfigurationResponse>"
       << "<trt:Configuration token=\"" << cfgToken << "\">"
         << "<tt:Name>VideoSourceConfig</tt:Name>"
         << "<tt:UseCount>" << useCount << "</tt:UseCount>"
         << "<tt:SourceToken>" << (b.sourceToken.empty() ? "src_main" : b.sourceToken) << "</tt:SourceToken>"
         << "<tt:Bounds x=\"0\" y=\"0\" width=\"" << b.width << "\" height=\"" << b.height << "\"/>"
       << "</trt:Configuration>"
       << "</trt:GetVideoSourceConfigurationResponse>";
    return os.str();
}

std::string MediaLegacyHandler::handleGetVideoSourceConfigurationOptions(const std::string& req) {
    std::string cfgToken = extractInnerTag(req, "ConfigurationToken");
    std::string profToken = extractInnerTag(req, "ProfileToken");
    std::vector<StreamProfile> profiles = backendProfiles();

    std::string targetSource;
    if (!profToken.empty()) {
        for (const auto& p : profiles) {
            if (p.token == profToken) { targetSource = p.sourceToken; break; }
        }
    } else if (!cfgToken.empty()) {
        static const std::string prefix = "video_source_config_";
        if (cfgToken.rfind(prefix, 0) == 0) targetSource = cfgToken.substr(prefix.size());
    }

    int w = 1920, h = 1080;
    bool found = false;
    for (const auto& p : profiles) {
        if (p.sourceToken == targetSource) { w = p.sourceBounds.width; h = p.sourceBounds.height; found = true; break; }
    }
    if (!found && !profiles.empty()) { w = profiles.front().sourceBounds.width; h = profiles.front().sourceBounds.height; }

    std::set<std::string> sources;
    for (const auto& p : profiles) if (!p.sourceToken.empty()) sources.insert(p.sourceToken);
    std::ostringstream tokens;
    bool first = true;
    for (const auto& s : sources) { if (!first) tokens << " "; tokens << s; first = false; }
    if (sources.empty()) tokens << "src_main";

    std::ostringstream os;
    os << "<trt:GetVideoSourceConfigurationOptionsResponse>"
          "<trt:Options>"
            "<tt:BoundsRange>"
              "<tt:XRange><tt:Min>0</tt:Min><tt:Max>0</tt:Max></tt:XRange>"
              "<tt:YRange><tt:Min>0</tt:Min><tt:Max>0</tt:Max></tt:YRange>"
              "<tt:WidthRange><tt:Min>" << w << "</tt:Min><tt:Max>" << w << "</tt:Max></tt:WidthRange>"
              "<tt:HeightRange><tt:Min>" << h << "</tt:Min><tt:Max>" << h << "</tt:Max></tt:HeightRange>"
            "</tt:BoundsRange>"
            "<tt:VideoSourceTokensAvailable>" << tokens.str() << "</tt:VideoSourceTokensAvailable>"
          "</trt:Options>"
        "</trt:GetVideoSourceConfigurationOptionsResponse>";
    return os.str();
}

std::string MediaLegacyHandler::handleGetCompatibleVideoSourceConfigurations() {
    std::vector<StreamProfile> profiles = backendProfiles();
    std::string vscToken = profiles.empty() ? "video_source_config"
                                             : videoSourceConfigToken(profiles.front().sourceToken);
    std::lock_guard<std::mutex> lk(g_stateMtx);
    VscBaseline b = resolveVscBaseline(vscToken, profiles);
    int useCount = countVSCUsage(vscToken, profiles);
    std::ostringstream os;
    os << "<trt:GetCompatibleVideoSourceConfigurationsResponse>"
       << "<trt:Configurations token=\"" << vscToken << "\">"
         << "<tt:Name>VideoSourceConfig</tt:Name>"
         << "<tt:UseCount>" << useCount << "</tt:UseCount>"
         << "<tt:SourceToken>" << (b.sourceToken.empty() ? "src_main" : b.sourceToken) << "</tt:SourceToken>"
         << "<tt:Bounds x=\"0\" y=\"0\" width=\"" << b.width << "\" height=\"" << b.height << "\"/>"
       << "</trt:Configurations>"
       << "</trt:GetCompatibleVideoSourceConfigurationsResponse>";
    return os.str();
}

std::string MediaLegacyHandler::handleAddVideoSourceConfiguration(const std::string& req) {
    std::string profileTok = extractInnerTag(req, "ProfileToken");
    std::string cfgTok = extractInnerTag(req, "ConfigurationToken");
    std::lock_guard<std::mutex> lk(g_stateMtx);
    auto it = g_dynProfiles.find(profileTok);
    if (it != g_dynProfiles.end()) it->second.vsToken = cfgTok;
    return "<trt:AddVideoSourceConfigurationResponse/>";
}

std::string MediaLegacyHandler::handleRemoveVideoSourceConfiguration(const std::string& req) {
    std::string profileTok = extractInnerTag(req, "ProfileToken");
    std::lock_guard<std::mutex> lk(g_stateMtx);
    auto it = g_dynProfiles.find(profileTok);
    if (it != g_dynProfiles.end()) it->second.vsToken.clear();
    return "<trt:RemoveVideoSourceConfigurationResponse/>";
}

std::string MediaLegacyHandler::handleSetVideoSourceConfiguration(const std::string& req) {
    // MEDIA-2-1-8: validate Bounds. Width/Height là ATTRIBUTES của <Bounds .../>,
    // không phải element — parse thủ công.
    int w = 0, h = 0, xv = 0, yv = 0;
    auto attrVal = [&](const std::string& xml, const std::string& attr) -> int {
        auto p = xml.find(attr + "=\"");
        if (p == std::string::npos) return -1;
        p += attr.size() + 2;
        auto e = xml.find('"', p);
        if (e == std::string::npos) return -1;
        try { return std::stoi(xml.substr(p, e - p)); } catch (...) { return -1; }
    };
    std::string cfgToken;
    {
        auto cp = req.find("<tt:Configuration");
        if (cp == std::string::npos) cp = req.find("<Configuration");
        if (cp != std::string::npos) {
            auto ce = req.find('>', cp);
            std::string cfgTag = req.substr(cp, ce - cp + 1);
            auto tp = cfgTag.find("token=\"");
            if (tp != std::string::npos) {
                auto te = cfgTag.find('"', tp + 7);
                if (te != std::string::npos) cfgToken = cfgTag.substr(tp + 7, te - (tp + 7));
            }
        }
    }
    auto bp = req.find("<tt:Bounds");
    if (bp == std::string::npos) bp = req.find("<Bounds");
    if (bp != std::string::npos) {
        auto be = req.find('>', bp);
        std::string bTag = req.substr(bp, be - bp + 1);
        int tw = attrVal(bTag, "width");  if (tw >= 0) w = tw;
        int th = attrVal(bTag, "height"); if (th >= 0) h = th;
        int tx = attrVal(bTag, "x");      if (tx >= 0) xv = tx;
        int ty = attrVal(bTag, "y");      if (ty >= 0) yv = ty;
    }
    if (w <= 0 || h <= 0 || xv < 0 || yv < 0) {
        return
            "<SOAP-ENV:Fault>"
              "<SOAP-ENV:Code>"
                "<SOAP-ENV:Value>SOAP-ENV:Sender</SOAP-ENV:Value>"
                "<SOAP-ENV:Subcode>"
                  "<SOAP-ENV:Value>ter:InvalidArgVal</SOAP-ENV:Value>"
                  "<SOAP-ENV:Subcode><SOAP-ENV:Value>ter:ConfigModify</SOAP-ENV:Value></SOAP-ENV:Subcode>"
                "</SOAP-ENV:Subcode>"
              "</SOAP-ENV:Code>"
              "<SOAP-ENV:Reason><SOAP-ENV:Text xml:lang=\"en\">Bounds out of range</SOAP-ENV:Text></SOAP-ENV:Reason>"
            "</SOAP-ENV:Fault>";
    }
    if (cfgToken.empty()) {
        std::vector<StreamProfile> profiles = backendProfiles();
        cfgToken = profiles.empty() ? "video_source_config"
                                     : videoSourceConfigToken(profiles.front().sourceToken);
    }
    std::lock_guard<std::mutex> lk(g_stateMtx);
    auto& ov = g_vscOverride[cfgToken];
    ov.has = true; ov.x = xv; ov.y = yv; ov.width = w; ov.height = h;
    return "<trt:SetVideoSourceConfigurationResponse/>";
}

std::string MediaLegacyHandler::handleGetVideoEncoderConfigurations() {
    std::vector<StreamProfile> profiles = backendProfiles();
    std::set<std::string> deleted;
    { std::lock_guard<std::mutex> lk(g_stateMtx); deleted = g_deletedFixed; }
    std::ostringstream os;
    os << "<trt:GetVideoEncoderConfigurationsResponse>";
    for (const auto& p : profiles) {
        if (deleted.count(p.token)) continue;
        std::string vecToken = videoEncoderConfigToken(p.token);
        VecBaseline b;
        int useCount;
        {
            std::lock_guard<std::mutex> lk(g_stateMtx);
            b = resolveVecBaseline(vecToken, profiles);
            useCount = countVECUsage(vecToken, profiles);
        }
        os << "<trt:Configurations token=\"" << vecToken << "\">"
           << "<tt:Name>VideoEncoderConfig</tt:Name>"
           << "<tt:UseCount>" << useCount << "</tt:UseCount>"
           << "<tt:Encoding>" << b.encoding << "</tt:Encoding>"
           << "<tt:Resolution><tt:Width>" << b.width << "</tt:Width>"
           << "<tt:Height>" << b.height << "</tt:Height></tt:Resolution>"
           << "<tt:Quality>" << b.quality << "</tt:Quality>"
           << "<tt:RateControl><tt:FrameRateLimit>" << b.frameRate << "</tt:FrameRateLimit>"
           << "<tt:EncodingInterval>1</tt:EncodingInterval>"
           << "<tt:BitrateLimit>" << b.bitrate << "</tt:BitrateLimit></tt:RateControl>";
        if (b.encoding == "H264") {
            os << "<tt:H264><tt:GovLength>" << b.govLength
               << "</tt:GovLength><tt:H264Profile>" << b.h264Profile << "</tt:H264Profile></tt:H264>";
        }
        os << "<tt:Multicast><tt:Address><tt:Type>IPv4</tt:Type>"
           << "<tt:IPv4Address>239.0.0.1</tt:IPv4Address></tt:Address>"
           << "<tt:Port>32000</tt:Port>"
           << "<tt:TTL>1</tt:TTL>"
           << "<tt:AutoStart>false</tt:AutoStart></tt:Multicast>"
           << "<tt:SessionTimeout>PT60S</tt:SessionTimeout>"
           << "</trt:Configurations>";
    }
    os << "</trt:GetVideoEncoderConfigurationsResponse>";
    return os.str();
}

std::string MediaLegacyHandler::handleGetVideoEncoderConfiguration(const std::string& req) {
    std::string token = extractInnerTag(req, "ConfigurationToken");
    std::vector<StreamProfile> profiles = backendProfiles();
    static const std::string prefix = "video_encoder_config_";
    std::string profileToken = token.rfind(prefix, 0) == 0 ? token.substr(prefix.size()) : std::string();

    bool exists = false;
    {
        std::lock_guard<std::mutex> lk(g_stateMtx);
        for (const auto& p : profiles) {
            if (p.token == profileToken && !g_deletedFixed.count(p.token)) { exists = true; break; }
        }
        if (!exists) {
            for (const auto& kv : g_dynProfiles) {
                if (kv.second.veToken == token) { exists = true; break; }
            }
        }
    }
    if (!exists) {
        return
            "<SOAP-ENV:Fault>"
              "<SOAP-ENV:Code><SOAP-ENV:Value>SOAP-ENV:Sender</SOAP-ENV:Value>"
                "<SOAP-ENV:Subcode><SOAP-ENV:Value>ter:InvalidArgVal</SOAP-ENV:Value>"
                  "<SOAP-ENV:Subcode><SOAP-ENV:Value>ter:NoConfig</SOAP-ENV:Value></SOAP-ENV:Subcode>"
                "</SOAP-ENV:Subcode>"
              "</SOAP-ENV:Code>"
              "<SOAP-ENV:Reason><SOAP-ENV:Text xml:lang=\"en\">Unknown VideoEncoderConfig</SOAP-ENV:Text></SOAP-ENV:Reason>"
            "</SOAP-ENV:Fault>";
    }

    std::lock_guard<std::mutex> lk(g_stateMtx);
    VecBaseline b = resolveVecBaseline(token, profiles);
    int useCount = countVECUsage(token, profiles);
    std::ostringstream os;
    os << "<trt:GetVideoEncoderConfigurationResponse>"
       << "<trt:Configuration token=\"" << token << "\">"
         << "<tt:Name>VideoEncoderConfig</tt:Name>"
         << "<tt:UseCount>" << useCount << "</tt:UseCount>"
         << "<tt:Encoding>" << b.encoding << "</tt:Encoding>"
         << "<tt:Resolution>"
           << "<tt:Width>" << b.width << "</tt:Width>"
           << "<tt:Height>" << b.height << "</tt:Height>"
         << "</tt:Resolution>"
         << "<tt:Quality>" << b.quality << "</tt:Quality>"
         << "<tt:RateControl>"
           << "<tt:FrameRateLimit>" << b.frameRate << "</tt:FrameRateLimit>"
           << "<tt:EncodingInterval>1</tt:EncodingInterval>"
           << "<tt:BitrateLimit>" << b.bitrate << "</tt:BitrateLimit>"
         << "</tt:RateControl>";
    if (b.encoding == "H264") {
        os << "<tt:H264><tt:GovLength>" << b.govLength
           << "</tt:GovLength><tt:H264Profile>" << b.h264Profile << "</tt:H264Profile></tt:H264>";
    }
    os << "<tt:Multicast><tt:Address><tt:Type>IPv4</tt:Type>"
         << "<tt:IPv4Address>239.0.0.1</tt:IPv4Address></tt:Address>"
         << "<tt:Port>32000</tt:Port>"
         << "<tt:TTL>1</tt:TTL>"
         << "<tt:AutoStart>false</tt:AutoStart></tt:Multicast>"
       << "<tt:SessionTimeout>PT60S</tt:SessionTimeout>"
       << "</trt:Configuration>"
       << "</trt:GetVideoEncoderConfigurationResponse>";
    return os.str();
}

std::string MediaLegacyHandler::handleGetVideoEncoderConfigurationOptions(const std::string& req) {
    std::string cfgToken = extractInnerTag(req, "ConfigurationToken");
    std::string profToken = extractInnerTag(req, "ProfileToken");
    std::vector<StreamProfile> profiles = backendProfiles();

    std::string targetProfile = profToken;
    if (targetProfile.empty() && !cfgToken.empty()) {
        static const std::string prefix = "video_encoder_config_";
        if (cfgToken.rfind(prefix, 0) == 0) targetProfile = cfgToken.substr(prefix.size());
    }
    int w = 1920, h = 1080;
    bool found = false;
    Codec targetCodec = Codec::H264;
    for (const auto& p : profiles) {
        if (p.token == targetProfile) {
            w = p.videoConfig.resolution.width; h = p.videoConfig.resolution.height;
            targetCodec = p.videoConfig.codec;
            found = true; break;
        }
    }
    if (!found && !profiles.empty()) {
        w = profiles.front().videoConfig.resolution.width;
        h = profiles.front().videoConfig.resolution.height;
    }

    std::ostringstream os;
    os << "<trt:GetVideoEncoderConfigurationOptionsResponse>"
          "<trt:Options>"
            "<tt:QualityRange><tt:Min>0</tt:Min><tt:Max>10</tt:Max></tt:QualityRange>";
    if (targetCodec == Codec::JPEG) {
        // DVR MJPEG profile (AlvisOS/DVR include/encoder_worker/encoder_worker.h):
        // kích thước cố định (đọc động từ backend ở trên, không hardcode), FPS
        // 1-20 (kMjpegMinFps/kMjpegMaxFps). Không có GovLength/H264Profile —
        // JPEG không có khái niệm GOP/profile như H264.
        os << "<tt:JPEG>"
              "<tt:ResolutionsAvailable><tt:Width>" << w << "</tt:Width><tt:Height>" << h << "</tt:Height></tt:ResolutionsAvailable>"
              "<tt:FrameRateRange><tt:Min>1</tt:Min><tt:Max>20</tt:Max></tt:FrameRateRange>"
              "<tt:EncodingIntervalRange><tt:Min>1</tt:Min><tt:Max>1</tt:Max></tt:EncodingIntervalRange>"
            "</tt:JPEG>";
    } else {
        os << "<tt:H264>"
              "<tt:ResolutionsAvailable><tt:Width>" << w << "</tt:Width><tt:Height>" << h << "</tt:Height></tt:ResolutionsAvailable>"
              "<tt:GovLengthRange><tt:Min>1</tt:Min><tt:Max>60</tt:Max></tt:GovLengthRange>"
              "<tt:FrameRateRange><tt:Min>1</tt:Min><tt:Max>30</tt:Max></tt:FrameRateRange>"
              "<tt:EncodingIntervalRange><tt:Min>1</tt:Min><tt:Max>1</tt:Max></tt:EncodingIntervalRange>"
              "<tt:H264ProfilesSupported>Baseline</tt:H264ProfilesSupported>"
              "<tt:H264ProfilesSupported>Main</tt:H264ProfilesSupported>"
              "<tt:H264ProfilesSupported>High</tt:H264ProfilesSupported>"
            "</tt:H264>";
    }
    os << "</trt:Options>"
        "</trt:GetVideoEncoderConfigurationOptionsResponse>";
    return os.str();
}

std::string MediaLegacyHandler::handleGetCompatibleVideoEncoderConfigurations() {
    // Same content as GetVideoEncoderConfigurations
    std::string body = handleGetVideoEncoderConfigurations();
    // Replace tag name
    size_t p = body.find("VideoEncoderConfigurationsResponse");
    if (p != std::string::npos)
        body.replace(p, 34, "CompatibleVideoEncoderConfigurationsResponse");
    size_t p2 = body.find("VideoEncoderConfigurationsResponse", p+40);
    if (p2 != std::string::npos)
        body.replace(p2, 34, "CompatibleVideoEncoderConfigurationsResponse");
    return body;
}

std::string MediaLegacyHandler::handleAddVideoEncoderConfiguration(const std::string& req) {
    std::string profileTok = extractInnerTag(req, "ProfileToken");
    std::string cfgTok = extractInnerTag(req, "ConfigurationToken");
    std::lock_guard<std::mutex> lk(g_stateMtx);
    auto it = g_dynProfiles.find(profileTok);
    if (it != g_dynProfiles.end()) it->second.veToken = cfgTok;
    return "<trt:AddVideoEncoderConfigurationResponse/>";
}

std::string MediaLegacyHandler::handleRemoveVideoEncoderConfiguration(const std::string& req) {
    std::string profileTok = extractInnerTag(req, "ProfileToken");
    std::lock_guard<std::mutex> lk(g_stateMtx);
    auto it = g_dynProfiles.find(profileTok);
    if (it != g_dynProfiles.end()) it->second.veToken.clear();
    return "<trt:RemoveVideoEncoderConfigurationResponse/>";
}

std::string MediaLegacyHandler::handleSetVideoEncoderConfiguration(const std::string& req) {
    // Echo lại giá trị Set vào override map để Get sau trả cùng giá trị
    // (MEDIA-2-3-12 H264Profile) — KHÔNG áp dụng thật lên encoder DVR, giữ
    // đúng compromise Media2Service đã dùng (xem đầu file).
    std::string cfgTok = extractInnerTag(req, "token");  // attribute on Configuration
    if (cfgTok.empty()) {
        size_t p = req.find("token=\"");
        if (p != std::string::npos) {
            size_t q = req.find('"', p + 7);
            if (q != std::string::npos) cfgTok = req.substr(p + 7, q - p - 7);
        }
    }
    if (cfgTok.empty()) return "<trt:SetVideoEncoderConfigurationResponse/>";

    std::string enc = extractInnerTag(req, "Encoding");
    std::string ws = extractInnerTag(req, "Width");
    std::string hs = extractInnerTag(req, "Height");
    std::string fr = extractInnerTag(req, "FrameRateLimit");
    std::string gv = extractInnerTag(req, "GovLength");
    std::string ql = extractInnerTag(req, "Quality");
    auto toi = [](const std::string& s, int def) {
        try { return s.empty() ? def : std::stoi(s); } catch (...) { return def; }
    };
    int nw = toi(ws, 0), nh = toi(hs, 0);
    int nfr = toi(fr, 0), ngv = toi(gv, 0), nql = toi(ql, -1);

    // Suy ngược profile token thật từ cfgTok (đúng quy ước
    // videoEncoderConfigToken()) để biết baseline codec DVR thật đang là gì.
    // DVR thật khoá cứng: profile MJPEG chỉ nhận Encoding=JPEG, profile
    // H264 chỉ nhận Encoding=H264 — không cho đổi chéo (xem AlvisOS/DVR
    // src/controller/api/rest/routes_live_profiles.cpp, log
    // "SET-ENC-REJECT ... JPEG is only available on the MJPEG profile").
    static const std::string vecPrefix = "video_encoder_config_";
    std::string profTok = cfgTok.rfind(vecPrefix, 0) == 0 ? cfgTok.substr(vecPrefix.size()) : std::string();
    bool baselineIsJpeg = false;
    int baseW = 0, baseH = 0;
    for (const auto& p : backendProfiles()) {
        if (p.token != profTok) continue;
        baselineIsJpeg = (p.videoConfig.codec == Codec::JPEG);
        baseW = p.videoConfig.resolution.width;
        baseH = p.videoConfig.resolution.height;
        break;
    }

    bool invalid = false;
    if (!enc.empty()) {
        if (enc != "H264" && enc != "JPEG") invalid = true;
        else if ((enc == "JPEG") != baselineIsJpeg) invalid = true;
    }
    if (baselineIsJpeg) {
        // MJPEG DVR thật: kích thước cố định (không cho đổi), FPS 1-20
        // (AlvisOS/DVR include/encoder_worker/encoder_worker.h:
        // kMjpegMinFps/kMjpegMaxFps).
        if (!ws.empty() && !hs.empty() && (nw != baseW || nh != baseH)) invalid = true;
        if (!fr.empty() && (nfr > 20 || nfr < 1)) invalid = true;
    } else {
        if (nw > 3840 || nh > 2160) invalid = true;
        if (nfr > 30 || nfr < 0) invalid = true;
    }
    if (!gv.empty() && (ngv > 60 || ngv < 0)) invalid = true;
    if (nql > 10 || nql < -1) invalid = true;
    if (invalid) {
        return
            "<SOAP-ENV:Fault>"
              "<SOAP-ENV:Code><SOAP-ENV:Value>SOAP-ENV:Sender</SOAP-ENV:Value>"
                "<SOAP-ENV:Subcode><SOAP-ENV:Value>ter:InvalidArgVal</SOAP-ENV:Value>"
                  "<SOAP-ENV:Subcode><SOAP-ENV:Value>ter:ConfigModify</SOAP-ENV:Value></SOAP-ENV:Subcode>"
                "</SOAP-ENV:Subcode>"
              "</SOAP-ENV:Code>"
              "<SOAP-ENV:Reason><SOAP-ENV:Text xml:lang=\"en\">VideoEncoder value out of range</SOAP-ENV:Text></SOAP-ENV:Reason>"
            "</SOAP-ENV:Fault>";
    }

    std::lock_guard<std::mutex> lk(g_stateMtx);
    auto& ov = g_vecOverride[cfgTok];
    if (!enc.empty()) { ov.hasEncoding = true; ov.encoding = enc; }
    if (!ws.empty() && !hs.empty()) { ov.hasResolution = true; ov.width = nw; ov.height = nh; }
    if (!fr.empty()) { ov.hasFrameRate = true; ov.frameRate = nfr; }
    std::string br = extractInnerTag(req, "BitrateLimit");
    if (!br.empty()) { try { ov.hasBitrate = true; ov.bitrate = std::stoi(br); } catch (...) {} }
    if (!gv.empty()) { ov.hasGovLength = true; ov.govLength = ngv; }
    std::string h264p = extractInnerTag(req, "H264Profile");
    if (!h264p.empty()) { ov.hasH264Profile = true; ov.h264Profile = h264p; }
    if (!ql.empty()) { ov.hasQuality = true; ov.quality = nql; }
    return "<trt:SetVideoEncoderConfigurationResponse/>";
}

std::string MediaLegacyHandler::handleGetGuaranteedNumberOfVideoEncoderInstances(const std::string& req) {
    (void)req;
    // GIỮ NGUYÊN 1/H264 — dù DVR giờ chạy JPEG thường trực song song H264,
    // RTSS-1-1-27..30 (đang PASS) dùng chính TotalNumber này để quyết định mở
    // bao nhiêu RTSP session đồng thời; đổi lên 2 là thay đổi hành vi test
    // chưa verify được (không có DTT tool ở đây) — để riêng, không đụng vào
    // trong lần sửa JPEG này.
    return
        "<trt:GetGuaranteedNumberOfVideoEncoderInstancesResponse>"
          "<trt:TotalNumber>1</trt:TotalNumber>"
          "<trt:H264>1</trt:H264>"
        "</trt:GetGuaranteedNumberOfVideoEncoderInstancesResponse>";
}

std::string MediaLegacyHandler::handleGetStreamUri(const std::string& req) {
    std::string token = extractInnerTag(req, "ProfileToken");
    std::vector<StreamProfile> profiles = backendProfiles();

    bool valid = false;
    {
        std::lock_guard<std::mutex> lk(g_stateMtx);
        if (!token.empty() && !g_deletedFixed.count(token)) {
            if (g_dynProfiles.count(token)) {
                valid = true;
            } else {
                for (const auto& p : profiles) {
                    if (p.token == token) { valid = true; break; }
                }
            }
        }
    }
    if (!valid) {
        return
            "<SOAP-ENV:Fault>"
              "<SOAP-ENV:Code><SOAP-ENV:Value>SOAP-ENV:Sender</SOAP-ENV:Value>"
                "<SOAP-ENV:Subcode><SOAP-ENV:Value>ter:InvalidArgVal</SOAP-ENV:Value>"
                  "<SOAP-ENV:Subcode><SOAP-ENV:Value>ter:NoProfile</SOAP-ENV:Value></SOAP-ENV:Subcode>"
                "</SOAP-ENV:Subcode>"
              "</SOAP-ENV:Code>"
              "<SOAP-ENV:Reason><SOAP-ENV:Text xml:lang=\"en\">No profile with the given token</SOAP-ENV:Text></SOAP-ENV:Reason>"
            "</SOAP-ENV:Fault>";
    }

    std::string uri;
    if (g_backend) {
        try { uri = g_backend->getStreamUri(token, StreamProtocol::RTSP).uri; }
        catch (const std::exception&) {}
    }

    // Transport/Protocol (ONVIF ver10 enum: UDP/TCP/RTSP/HTTP/HTTPS). HTTP/HTTPS
    // = RTSP tunnel qua chính port web service (xử lý thật ở
    // OnvifServer::proxyRtspHttpTunnel, dựa trên header x-rtsp-tunnelled —
    // không phụ thuộc URI path, chỉ cần đổi port/scheme cho đúng ở đây).
    std::string protocol = extractInnerTag(req, "Protocol");
    if (!uri.empty() && (protocol == "HTTP" || protocol == "HTTPS")) {
        std::string rtspPortStr = ":" + std::to_string(g_rtspPort);
        size_t pos = uri.find(rtspPortStr);
        if (pos != std::string::npos) {
            uri.replace(pos, rtspPortStr.length(), ":" + std::to_string(g_httpPort));
        }
        if (uri.rfind("rtsp://", 0) == 0) {
            uri.replace(0, 7, protocol == "HTTPS" ? "https://" : "http://");
        }
    }

    std::ostringstream os;
    os << "<trt:GetStreamUriResponse>"
       << "<trt:MediaUri>"
         << "<tt:Uri>" << uri << "</tt:Uri>"
         << "<tt:InvalidAfterConnect>false</tt:InvalidAfterConnect>"
         << "<tt:InvalidAfterReboot>false</tt:InvalidAfterReboot>"
         << "<tt:Timeout>PT60S</tt:Timeout>"
       << "</trt:MediaUri>"
       << "</trt:GetStreamUriResponse>";
    return os.str();
}

std::string MediaLegacyHandler::handleGetSnapshotUri(const std::string& req) {
    std::string token = extractInnerTag(req, "ProfileToken");
    std::string uri;
    if (g_backend) {
        try { uri = g_backend->getSnapshotUri(token).uri; }
        catch (const std::exception&) {}
    }
    std::ostringstream os;
    os << "<trt:GetSnapshotUriResponse>"
       << "<trt:MediaUri>"
         << "<tt:Uri>" << uri << "</tt:Uri>"
         << "<tt:InvalidAfterConnect>false</tt:InvalidAfterConnect>"
         << "<tt:InvalidAfterReboot>false</tt:InvalidAfterReboot>"
         << "<tt:Timeout>PT60S</tt:Timeout>"
       << "</trt:MediaUri>"
       << "</trt:GetSnapshotUriResponse>";
    return os.str();
}

std::string MediaLegacyHandler::handleSetSynchronizationPoint(const std::string& req) {
    (void)req;
    return "<trt:SetSynchronizationPointResponse/>";
}

std::string MediaLegacyHandler::handleGetMetadataConfigurations() {
    // Trả empty list — device không declare metadata support.
    return "<trt:GetMetadataConfigurationsResponse/>";
}

// ── Dispatcher ──────────────────────────────────────────────────────────────
std::string MediaLegacyHandler::dispatch(const std::string& req) {
    if (req.find(NS_MEDIA1) == std::string::npos) return "";
    std::string rel = extractMessageId(req);

    // Order matter: dispatch từ specific → generic để tránh substring match sai
    // (VD "GetVideoEncoderConfigurations" khớp trước "GetVideoEncoderConfiguration").

    if (req.find("GetServiceCapabilities") != std::string::npos)
        return wrap(actUrl("GetServiceCapabilities"), rel, handleGetServiceCapabilities());
    if (req.find("GetVideoSources") != std::string::npos)
        return wrap(actUrl("GetVideoSources"), rel, handleGetVideoSources());
    if (req.find("GetAudioSources") != std::string::npos)
        return wrap(actUrl("GetAudioSources"), rel, handleGetAudioSources());

    // Profile CRUD
    if (req.find("GetProfiles") != std::string::npos)
        return wrap(actUrl("GetProfiles"), rel, handleGetProfiles());
    if (req.find("GetProfile") != std::string::npos)
        return wrap(actUrl("GetProfile"), rel, handleGetProfile(req));
    if (req.find("CreateProfile") != std::string::npos)
        return wrap(actUrl("CreateProfile"), rel, handleCreateProfile(req));
    if (req.find("DeleteProfile") != std::string::npos)
        return wrap(actUrl("DeleteProfile"), rel, handleDeleteProfile(req));

    // Video Source Config ops
    if (req.find("GetVideoSourceConfigurationOptions") != std::string::npos)
        return wrap(actUrl("GetVideoSourceConfigurationOptions"), rel, handleGetVideoSourceConfigurationOptions(req));
    if (req.find("GetCompatibleVideoSourceConfigurations") != std::string::npos)
        return wrap(actUrl("GetCompatibleVideoSourceConfigurations"), rel, handleGetCompatibleVideoSourceConfigurations());
    if (req.find("GetVideoSourceConfigurations") != std::string::npos)
        return wrap(actUrl("GetVideoSourceConfigurations"), rel, handleGetVideoSourceConfigurations());
    if (req.find("GetVideoSourceConfiguration") != std::string::npos)
        return wrap(actUrl("GetVideoSourceConfiguration"), rel, handleGetVideoSourceConfiguration(req));
    if (req.find("AddVideoSourceConfiguration") != std::string::npos)
        return wrap(actUrl("AddVideoSourceConfiguration"), rel, handleAddVideoSourceConfiguration(req));
    if (req.find("RemoveVideoSourceConfiguration") != std::string::npos)
        return wrap(actUrl("RemoveVideoSourceConfiguration"), rel, handleRemoveVideoSourceConfiguration(req));
    if (req.find("SetVideoSourceConfiguration") != std::string::npos)
        return wrap(actUrl("SetVideoSourceConfiguration"), rel, handleSetVideoSourceConfiguration(req));

    // Video Encoder Config ops
    if (req.find("GetGuaranteedNumberOfVideoEncoderInstances") != std::string::npos)
        return wrap(actUrl("GetGuaranteedNumberOfVideoEncoderInstances"), rel, handleGetGuaranteedNumberOfVideoEncoderInstances(req));
    if (req.find("GetVideoEncoderConfigurationOptions") != std::string::npos)
        return wrap(actUrl("GetVideoEncoderConfigurationOptions"), rel, handleGetVideoEncoderConfigurationOptions(req));
    if (req.find("GetCompatibleVideoEncoderConfigurations") != std::string::npos)
        return wrap(actUrl("GetCompatibleVideoEncoderConfigurations"), rel, handleGetCompatibleVideoEncoderConfigurations());
    if (req.find("GetVideoEncoderConfigurations") != std::string::npos)
        return wrap(actUrl("GetVideoEncoderConfigurations"), rel, handleGetVideoEncoderConfigurations());
    if (req.find("GetVideoEncoderConfiguration") != std::string::npos)
        return wrap(actUrl("GetVideoEncoderConfiguration"), rel, handleGetVideoEncoderConfiguration(req));
    if (req.find("AddVideoEncoderConfiguration") != std::string::npos)
        return wrap(actUrl("AddVideoEncoderConfiguration"), rel, handleAddVideoEncoderConfiguration(req));
    if (req.find("RemoveVideoEncoderConfiguration") != std::string::npos)
        return wrap(actUrl("RemoveVideoEncoderConfiguration"), rel, handleRemoveVideoEncoderConfiguration(req));
    if (req.find("SetVideoEncoderConfiguration") != std::string::npos)
        return wrap(actUrl("SetVideoEncoderConfiguration"), rel, handleSetVideoEncoderConfiguration(req));

    // Streaming
    if (req.find("GetStreamUri") != std::string::npos)
        return wrap(actUrl("GetStreamUri"), rel, handleGetStreamUri(req));
    if (req.find("GetSnapshotUri") != std::string::npos)
        return wrap(actUrl("GetSnapshotUri"), rel, handleGetSnapshotUri(req));
    if (req.find("SetSynchronizationPoint") != std::string::npos)
        return wrap(actUrl("SetSynchronizationPoint"), rel, handleSetSynchronizationPoint(req));

    // Metadata configuration operations used by the Media1 dynamic-profile
    // conformance cases. Keep the fixed mock configuration compatible with
    // the Profile-M metadata service.
    if (req.find("GetCompatibleMetadataConfigurations") != std::string::npos)
        return wrap(actUrl("GetCompatibleMetadataConfigurations"), rel,
                    "<trt:GetCompatibleMetadataConfigurationsResponse>"
                    "<trt:Configurations token=\"metadata_config\">"
                    "<tt:Name>MetadataConfig</tt:Name><tt:UseCount>1</tt:UseCount>"
                    "<tt:Analytics>true</tt:Analytics>"
                    "<tt:Multicast><tt:Address><tt:Type>IPv4</tt:Type></tt:Address>"
                    "<tt:Port>32001</tt:Port><tt:TTL>1</tt:TTL><tt:AutoStart>false</tt:AutoStart></tt:Multicast>"
                    "<tt:SessionTimeout>PT60S</tt:SessionTimeout>"
                    "</trt:Configurations></trt:GetCompatibleMetadataConfigurationsResponse>");
    if (req.find("AddMetadataConfiguration") != std::string::npos) {
        std::string profileTok = extractInnerTag(req, "ProfileToken");
        std::string cfgTok = extractInnerTag(req, "ConfigurationToken");
        std::lock_guard<std::mutex> lk(g_stateMtx);
        auto it = g_dynProfiles.find(profileTok);
        if (it != g_dynProfiles.end()) it->second.mdToken = cfgTok;
        return wrap(actUrl("AddMetadataConfiguration"), rel,
                    "<trt:AddMetadataConfigurationResponse/>");
    }
    if (req.find("RemoveMetadataConfiguration") != std::string::npos) {
        std::string profileTok = extractInnerTag(req, "ProfileToken");
        std::lock_guard<std::mutex> lk(g_stateMtx);
        auto it = g_dynProfiles.find(profileTok);
        if (it != g_dynProfiles.end()) it->second.mdToken.clear();
        return wrap(actUrl("RemoveMetadataConfiguration"), rel,
                    "<trt:RemoveMetadataConfigurationResponse/>");
    }
    if (req.find("GetMetadataConfigurations") != std::string::npos)
        return wrap(actUrl("GetMetadataConfigurations"), rel, handleGetMetadataConfigurations());

    return "";  // op không nhận diện, để gSOAP fault mặc định
}
