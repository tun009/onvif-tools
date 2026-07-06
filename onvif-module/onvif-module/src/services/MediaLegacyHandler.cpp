// MediaLegacyHandler.cpp — Media Service ver10 (Profile S/G/Q backward compat).
// Toàn bộ ops trả XML thủ công. State đơn giản: 3 fixed profiles
// (profile_main/sub1/sub2) khớp Media2, VideoSource src_main, VideoEncoder
// per profile. Không backend real — profile CRUD chỉ ack (không persist Media1 riêng).

#include "services/MediaLegacyHandler.h"
#include <sstream>
#include <mutex>
#include <map>
#include <set>
#include <vector>

namespace {
const char* NS_MEDIA1 = "http://www.onvif.org/ver10/media/wsdl";
const char* ACT = "http://www.onvif.org/ver10/media/wsdl/Media/";

std::string g_deviceIp = "127.0.0.1";
int         g_httpPort = 8080;
int         g_rtspPort = 8554;

std::string actUrl(const char* op) {
    return std::string(ACT) + op + "Response";
}

// ── Media1 state tracking ───────────────────────────────────────────────────
// Persist state qua các op để tool test consistency (Set→Get, Create→Get,...).
struct DynProfile {
    std::string token;
    std::string name;
    std::string vsToken;    // empty = no VideoSourceConfig attached
    std::string veToken;    // empty = no VideoEncoderConfig
    std::string mdToken;    // empty = no MetadataConfig
};
struct VECState {
    std::string encoding = "H264";  // H264, JPEG, MPEG4
    std::string name;
    std::string h264Profile = "Main";
    int width = 3840, height = 2160;
    int frameRate = 30, bitrate = 20000;
    int govLength = 30;
    int quality = 5;
    int encodingInterval = 1;
    std::string multicastAddr = "239.0.0.1";
    int multicastPort = 32000;
    int multicastTTL = 1;
    bool multicastAutoStart = false;
    std::string sessionTimeout = "PT60S";
};
struct VSCState {
    int x=0, y=0, width=1920, height=1080;
};

std::mutex g_stateMtx;
std::map<std::string, DynProfile> g_dynProfiles;   // token → dyn profile
std::set<std::string> g_deletedFixed;              // fixed profile tokens deleted by tool
std::map<std::string, VECState> g_vecState;        // vec token → state (persist Set)
std::map<std::string, VSCState> g_vscState;

// Init fixed VEC states (chỉ init 1 lần khi truy cập).
void ensureFixedVecState() {
    if (!g_vecState.empty()) return;
    VECState m; m.name="VideoEncoderConfig";
    m.width=3840; m.height=2160; m.frameRate=30; m.bitrate=20000;
    g_vecState["video_encoder_config"] = m;
    VECState s1; s1.name="VideoEncoderConfig_sub1";
    s1.width=1280; s1.height=720; s1.frameRate=15; s1.bitrate=2000;
    g_vecState["video_encoder_config_profile_sub1"] = s1;
    VECState s2; s2.name="VideoEncoderConfig_sub2";
    s2.width=640; s2.height=480; s2.frameRate=10; s2.bitrate=512;
    g_vecState["video_encoder_config_profile_sub2"] = s2;
    // JPEG encoder for Profile S JPEG test suite (RTSS-1-1-31..36/45/53, MEDIA-2-1-9)
    VECState jp; jp.name="VideoEncoderConfig_jpeg"; jp.encoding="JPEG";
    jp.width=640; jp.height=480; jp.frameRate=15; jp.bitrate=1000;
    g_vecState["video_encoder_config_jpeg"] = jp;
    // VSC
    VSCState vsc;
    g_vscState["video_source_config"] = vsc;
}

// UseCount = số fixed profiles không bị delete + số dyn profiles reference config
// (VSC dùng bởi tất cả fixed + jpeg; VEC dùng bởi 1 profile mỗi cái).
int countVSCUsage(const std::string& vscToken) {
    if (vscToken != "video_source_config") return 0;
    int count = 0;
    // 4 fixed profiles (main/sub1/sub2/jpeg) all use video_source_config
    for (const char* p : {"profile_main","profile_sub1","profile_sub2","profile_jpeg"}) {
        if (g_deletedFixed.count(p) == 0) count++;
    }
    for (const auto& kv : g_dynProfiles) {
        if (kv.second.vsToken == vscToken) count++;
    }
    return count;
}
int countVECUsage(const std::string& vecToken) {
    // Each VEC used by 1 fixed profile by default
    int count = 0;
    static const std::map<std::string, std::string> vec2profile = {
        {"video_encoder_config", "profile_main"},
        {"video_encoder_config_profile_sub1", "profile_sub1"},
        {"video_encoder_config_profile_sub2", "profile_sub2"},
        {"video_encoder_config_jpeg", "profile_jpeg"},
    };
    auto it = vec2profile.find(vecToken);
    if (it != vec2profile.end() && !g_deletedFixed.count(it->second)) count++;
    for (const auto& kv : g_dynProfiles) {
        if (kv.second.veToken == vecToken) count++;
    }
    return count;
}
} // namespace

void MediaLegacyHandler::setEndpoint(const std::string& ip, int port) {
    g_deviceIp = ip;
    g_httpPort = port;
    // RTSP port config từ ServiceConfig — mock giữ 8554
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
// Media1 tt:Profile struct: VideoSourceConfiguration + VideoEncoderConfiguration
std::string MediaLegacyHandler::profileXml(const char* wrapperElem,
                                            const char* token, const char* name,
                                            bool fixed, bool includeVSC, bool includeVEC) {
    std::lock_guard<std::mutex> lk(g_stateMtx);
    ensureFixedVecState();
    std::ostringstream os;
    os << "<trt:" << wrapperElem << " fixed=\"" << (fixed ? "true" : "false")
       << "\" token=\"" << token << "\">"
       << "<tt:Name>" << name << "</tt:Name>";
    if (includeVSC) {
        const auto& vsc = g_vscState["video_source_config"];
        int useCount = countVSCUsage("video_source_config");
        os << "<tt:VideoSourceConfiguration token=\"video_source_config\">"
             << "<tt:Name>VideoSourceConfig</tt:Name>"
             << "<tt:UseCount>" << useCount << "</tt:UseCount>"
             << "<tt:SourceToken>src_main</tt:SourceToken>"
             << "<tt:Bounds x=\"" << vsc.x << "\" y=\"" << vsc.y
                          << "\" width=\"" << vsc.width << "\" height=\"" << vsc.height << "\"/>"
           << "</tt:VideoSourceConfiguration>";
    }
    if (includeVEC) {
        // Determine VEC token cho profile
        std::string vecToken = "video_encoder_config";
        if (std::string(token) == "profile_sub1") vecToken = "video_encoder_config_profile_sub1";
        else if (std::string(token) == "profile_sub2") vecToken = "video_encoder_config_profile_sub2";
        else if (std::string(token) == "profile_jpeg") vecToken = "video_encoder_config_jpeg";
        else {
            // dyn profile: look up dyn state
            auto it = g_dynProfiles.find(token);
            if (it != g_dynProfiles.end() && !it->second.veToken.empty()) vecToken = it->second.veToken;
        }
        const auto& v = g_vecState[vecToken];
        int useCount = countVECUsage(vecToken);
        os << "<tt:VideoEncoderConfiguration token=\"" << vecToken << "\">"
           << "<tt:Name>" << v.name << "</tt:Name>"
           << "<tt:UseCount>" << useCount << "</tt:UseCount>"
           << "<tt:Encoding>" << v.encoding << "</tt:Encoding>"
           << "<tt:Resolution>"
             << "<tt:Width>" << v.width << "</tt:Width>"
             << "<tt:Height>" << v.height << "</tt:Height>"
           << "</tt:Resolution>"
           << "<tt:Quality>" << v.quality << "</tt:Quality>"
           << "<tt:RateControl>"
             << "<tt:FrameRateLimit>" << v.frameRate << "</tt:FrameRateLimit>"
             << "<tt:EncodingInterval>" << v.encodingInterval << "</tt:EncodingInterval>"
             << "<tt:BitrateLimit>" << v.bitrate << "</tt:BitrateLimit>"
           << "</tt:RateControl>";
        if (v.encoding == "H264") {
            os << "<tt:H264>"
                 << "<tt:GovLength>" << v.govLength << "</tt:GovLength>"
                 << "<tt:H264Profile>" << v.h264Profile << "</tt:H264Profile>"
               << "</tt:H264>";
        }
        os << "<tt:Multicast>"
             << "<tt:Address><tt:Type>IPv4</tt:Type><tt:IPv4Address>" << v.multicastAddr << "</tt:IPv4Address></tt:Address>"
             << "<tt:Port>" << v.multicastPort << "</tt:Port>"
             << "<tt:TTL>" << v.multicastTTL << "</tt:TTL>"
             << "<tt:AutoStart>" << (v.multicastAutoStart?"true":"false") << "</tt:AutoStart>"
           << "</tt:Multicast>"
           << "<tt:SessionTimeout>" << v.sessionTimeout << "</tt:SessionTimeout>"
           << "</tt:VideoEncoderConfiguration>";
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
    return
        "<trt:GetVideoSourcesResponse>"
          "<trt:VideoSources token=\"src_main\">"
            "<tt:Framerate>30.0</tt:Framerate>"
            "<tt:Resolution><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:Resolution>"
          "</trt:VideoSources>"
        "</trt:GetVideoSourcesResponse>";
}

std::string MediaLegacyHandler::handleGetAudioSources() {
    return "<trt:GetAudioSourcesResponse/>";
}

std::string MediaLegacyHandler::handleGetProfiles() {
    std::ostringstream os;
    os << "<trt:GetProfilesResponse>";
    // Fixed profiles (skip if tool đã DeleteProfile)
    std::set<std::string> deleted;
    std::vector<DynProfile> dyns;
    {
        std::lock_guard<std::mutex> lk(g_stateMtx);
        deleted = g_deletedFixed;
        for (auto& kv : g_dynProfiles) dyns.push_back(kv.second);
    }
    if (!deleted.count("profile_main"))
        os << profileXml("Profiles", "profile_main", "Main 4K", true, true, true);
    if (!deleted.count("profile_sub1"))
        os << profileXml("Profiles", "profile_sub1", "Sub1 720p", true, true, true);
    if (!deleted.count("profile_sub2"))
        os << profileXml("Profiles", "profile_sub2", "Sub2 480p", true, true, true);
    if (!deleted.count("profile_jpeg"))
        os << profileXml("Profiles", "profile_jpeg", "JPEG", true, true, true);
    // Dyn profiles: hiển thị VSC/VEC theo trạng thái đã Add
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
    // Fixed profiles
    bool isFixed = false; const char* fname = nullptr;
    if (token == "profile_main") { isFixed = true; fname = "Main 4K"; }
    else if (token == "profile_sub1") { isFixed = true; fname = "Sub1 720p"; }
    else if (token == "profile_sub2") { isFixed = true; fname = "Sub2 480p"; }
    else if (token == "profile_jpeg") { isFixed = true; fname = "JPEG"; }
    if (isFixed) {
        std::lock_guard<std::mutex> lk(g_stateMtx);
        if (g_deletedFixed.count(token)) isFixed = false;
    }
    if (isFixed) {
        std::ostringstream os;
        os << "<trt:GetProfileResponse>"
           << profileXml("Profile", token.c_str(), fname, true, true, true)
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
                     false, !dp.vsToken.empty(), !dp.veToken.empty())
       << "</trt:GetProfileResponse>";
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
    // Kiểm tra profile tồn tại (fixed hoặc dyn); nếu không → fault ter:NoProfile.
    bool valid = false;
    {
        std::lock_guard<std::mutex> lk(g_stateMtx);
        if ((token == "profile_main" || token == "profile_sub1" ||
             token == "profile_sub2" || token == "profile_jpeg") &&
            !g_deletedFixed.count(token)) {
            valid = true;
            g_deletedFixed.insert(token);
        } else if (g_dynProfiles.count(token)) {
            valid = true;
            g_dynProfiles.erase(token);
        }
    }
    if (!valid) {
        // Return SOAP fault manually via special marker — actual fault built in wrap
        // dùng cơ chế đơn giản: return SOAP fault XML thay envelope.
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
    return
        "<trt:GetVideoSourceConfigurationsResponse>"
          "<trt:Configurations token=\"video_source_config\">"
            "<tt:Name>VideoSourceConfig</tt:Name>"
            "<tt:UseCount>3</tt:UseCount>"
            "<tt:SourceToken>src_main</tt:SourceToken>"
            "<tt:Bounds x=\"0\" y=\"0\" width=\"1920\" height=\"1080\"/>"
          "</trt:Configurations>"
        "</trt:GetVideoSourceConfigurationsResponse>";
}

std::string MediaLegacyHandler::handleGetVideoSourceConfiguration(const std::string& req) {
    (void)req;
    return
        "<trt:GetVideoSourceConfigurationResponse>"
          "<trt:Configuration token=\"video_source_config\">"
            "<tt:Name>VideoSourceConfig</tt:Name>"
            "<tt:UseCount>3</tt:UseCount>"
            "<tt:SourceToken>src_main</tt:SourceToken>"
            "<tt:Bounds x=\"0\" y=\"0\" width=\"1920\" height=\"1080\"/>"
          "</trt:Configuration>"
        "</trt:GetVideoSourceConfigurationResponse>";
}

std::string MediaLegacyHandler::handleGetVideoSourceConfigurationOptions() {
    return
        "<trt:GetVideoSourceConfigurationOptionsResponse>"
          "<trt:Options>"
            "<tt:BoundsRange>"
              "<tt:XRange><tt:Min>0</tt:Min><tt:Max>0</tt:Max></tt:XRange>"
              "<tt:YRange><tt:Min>0</tt:Min><tt:Max>0</tt:Max></tt:YRange>"
              "<tt:WidthRange><tt:Min>1920</tt:Min><tt:Max>1920</tt:Max></tt:WidthRange>"
              "<tt:HeightRange><tt:Min>1080</tt:Min><tt:Max>1080</tt:Max></tt:HeightRange>"
            "</tt:BoundsRange>"
            "<tt:VideoSourceTokensAvailable>src_main</tt:VideoSourceTokensAvailable>"
          "</trt:Options>"
        "</trt:GetVideoSourceConfigurationOptionsResponse>";
}

std::string MediaLegacyHandler::handleGetCompatibleVideoSourceConfigurations() {
    return
        "<trt:GetCompatibleVideoSourceConfigurationsResponse>"
          "<trt:Configurations token=\"video_source_config\">"
            "<tt:Name>VideoSourceConfig</tt:Name>"
            "<tt:UseCount>3</tt:UseCount>"
            "<tt:SourceToken>src_main</tt:SourceToken>"
            "<tt:Bounds x=\"0\" y=\"0\" width=\"1920\" height=\"1080\"/>"
          "</trt:Configurations>"
        "</trt:GetCompatibleVideoSourceConfigurationsResponse>";
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
    // MEDIA-2-1-8/11: validate Bounds (max 1920x1080). Ngoài range → fault.
    std::string ws = extractInnerTag(req, "width");
    std::string hs = extractInnerTag(req, "height");
    int w = 0, h = 0;
    try { if (!ws.empty()) w = std::stoi(ws); } catch (...) {}
    try { if (!hs.empty()) h = std::stoi(hs); } catch (...) {}
    if (w < 0 || w > 1920 || h < 0 || h > 1080) {
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
    // Persist bounds
    std::lock_guard<std::mutex> lk(g_stateMtx);
    ensureFixedVecState();
    auto& vsc = g_vscState["video_source_config"];
    if (w > 0) vsc.width = w;
    if (h > 0) vsc.height = h;
    return "<trt:SetVideoSourceConfigurationResponse/>";
}

std::string MediaLegacyHandler::handleGetVideoEncoderConfigurations() {
    // Build từ state map — include JPEG + persisted Set values.
    std::lock_guard<std::mutex> lk(g_stateMtx);
    ensureFixedVecState();
    std::ostringstream os;
    os << "<trt:GetVideoEncoderConfigurationsResponse>";
    for (const auto& kv : g_vecState) {
        const auto& v = kv.second;
        int useCount = countVECUsage(kv.first);
        os << "<trt:Configurations token=\"" << kv.first << "\">"
           << "<tt:Name>" << v.name << "</tt:Name>"
           << "<tt:UseCount>" << useCount << "</tt:UseCount>"
           << "<tt:Encoding>" << v.encoding << "</tt:Encoding>"
           << "<tt:Resolution><tt:Width>" << v.width << "</tt:Width>"
           << "<tt:Height>" << v.height << "</tt:Height></tt:Resolution>"
           << "<tt:Quality>" << v.quality << "</tt:Quality>"
           << "<tt:RateControl><tt:FrameRateLimit>" << v.frameRate << "</tt:FrameRateLimit>"
           << "<tt:EncodingInterval>" << v.encodingInterval << "</tt:EncodingInterval>"
           << "<tt:BitrateLimit>" << v.bitrate << "</tt:BitrateLimit></tt:RateControl>";
        if (v.encoding == "H264") {
            os << "<tt:H264><tt:GovLength>" << v.govLength
               << "</tt:GovLength><tt:H264Profile>" << v.h264Profile << "</tt:H264Profile></tt:H264>";
        }
        os << "<tt:Multicast><tt:Address><tt:Type>IPv4</tt:Type>"
           << "<tt:IPv4Address>" << v.multicastAddr << "</tt:IPv4Address></tt:Address>"
           << "<tt:Port>" << v.multicastPort << "</tt:Port>"
           << "<tt:TTL>" << v.multicastTTL << "</tt:TTL>"
           << "<tt:AutoStart>" << (v.multicastAutoStart?"true":"false") << "</tt:AutoStart></tt:Multicast>"
           << "<tt:SessionTimeout>" << v.sessionTimeout << "</tt:SessionTimeout>"
           << "</trt:Configurations>";
    }
    os << "</trt:GetVideoEncoderConfigurationsResponse>";
    return os.str();
}

std::string MediaLegacyHandler::handleGetVideoEncoderConfiguration(const std::string& req) {
    std::string token = extractInnerTag(req, "ConfigurationToken");
    const char* codec = "H264";
    int w = 3840, h = 2160, fr = 30, br = 20000;
    std::string name = "VideoEncoderConfig";
    if (token == "video_encoder_config_profile_sub1") {
        w = 1280; h = 720; fr = 15; br = 2000; name = "VideoEncoderConfig_sub1";
    } else if (token == "video_encoder_config_profile_sub2") {
        // H264 (Media1 không hỗ trợ H265)
        w = 640; h = 480; fr = 10; br = 512; name = "VideoEncoderConfig_sub2";
    }
    std::ostringstream os;
    os << "<trt:GetVideoEncoderConfigurationResponse>"
       << "<trt:Configuration token=\"" << token << "\">"
         << "<tt:Name>" << name << "</tt:Name><tt:UseCount>1</tt:UseCount>"
         << "<tt:Encoding>" << codec << "</tt:Encoding>"
         << "<tt:Resolution>"
           << "<tt:Width>" << w << "</tt:Width>"
           << "<tt:Height>" << h << "</tt:Height>"
         << "</tt:Resolution>"
         << "<tt:Quality>5</tt:Quality>"
         << "<tt:RateControl>"
           << "<tt:FrameRateLimit>" << fr << "</tt:FrameRateLimit>"
           << "<tt:EncodingInterval>1</tt:EncodingInterval>"
           << "<tt:BitrateLimit>" << br << "</tt:BitrateLimit>"
         << "</tt:RateControl>";
    if (std::string(codec) == "H264") {
        os << "<tt:H264><tt:GovLength>30</tt:GovLength><tt:H264Profile>Main</tt:H264Profile></tt:H264>";
    }
    os << "<tt:Multicast><tt:Address><tt:Type>IPv4</tt:Type>"
         << "<tt:IPv4Address>239.0.0.1</tt:IPv4Address></tt:Address>"
         << "<tt:Port>32000</tt:Port><tt:TTL>1</tt:TTL>"
         << "<tt:AutoStart>false</tt:AutoStart></tt:Multicast>"
       << "<tt:SessionTimeout>PT60S</tt:SessionTimeout>"
       << "</trt:Configuration>"
       << "</trt:GetVideoEncoderConfigurationResponse>";
    return os.str();
}

std::string MediaLegacyHandler::handleGetVideoEncoderConfigurationOptions() {
    // Thêm JPEG option cho profile_jpeg tests + full resolution list.
    return
        "<trt:GetVideoEncoderConfigurationOptionsResponse>"
          "<trt:Options>"
            "<tt:QualityRange><tt:Min>0</tt:Min><tt:Max>10</tt:Max></tt:QualityRange>"
            "<tt:JPEG>"
              "<tt:ResolutionsAvailable><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:ResolutionsAvailable>"
              "<tt:ResolutionsAvailable><tt:Width>1280</tt:Width><tt:Height>720</tt:Height></tt:ResolutionsAvailable>"
              "<tt:ResolutionsAvailable><tt:Width>640</tt:Width><tt:Height>480</tt:Height></tt:ResolutionsAvailable>"
              "<tt:FrameRateRange><tt:Min>1</tt:Min><tt:Max>30</tt:Max></tt:FrameRateRange>"
              "<tt:EncodingIntervalRange><tt:Min>1</tt:Min><tt:Max>1</tt:Max></tt:EncodingIntervalRange>"
            "</tt:JPEG>"
            "<tt:H264>"
              "<tt:ResolutionsAvailable><tt:Width>3840</tt:Width><tt:Height>2160</tt:Height></tt:ResolutionsAvailable>"
              "<tt:ResolutionsAvailable><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:ResolutionsAvailable>"
              "<tt:ResolutionsAvailable><tt:Width>1280</tt:Width><tt:Height>720</tt:Height></tt:ResolutionsAvailable>"
              "<tt:ResolutionsAvailable><tt:Width>640</tt:Width><tt:Height>480</tt:Height></tt:ResolutionsAvailable>"
              "<tt:ResolutionsAvailable><tt:Width>320</tt:Width><tt:Height>240</tt:Height></tt:ResolutionsAvailable>"
              "<tt:GovLengthRange><tt:Min>1</tt:Min><tt:Max>60</tt:Max></tt:GovLengthRange>"
              "<tt:FrameRateRange><tt:Min>1</tt:Min><tt:Max>30</tt:Max></tt:FrameRateRange>"
              "<tt:EncodingIntervalRange><tt:Min>1</tt:Min><tt:Max>1</tt:Max></tt:EncodingIntervalRange>"
              "<tt:H264ProfilesSupported>Baseline</tt:H264ProfilesSupported>"
              "<tt:H264ProfilesSupported>Main</tt:H264ProfilesSupported>"
              "<tt:H264ProfilesSupported>High</tt:H264ProfilesSupported>"
            "</tt:H264>"
          "</trt:Options>"
        "</trt:GetVideoEncoderConfigurationOptionsResponse>";
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
    // Persist VEC settings để Get sau trả cùng giá trị (MEDIA-2-3-12 H264Profile).
    std::string cfgTok = extractInnerTag(req, "token");  // attribute on Configuration
    if (cfgTok.empty()) {
        // Fallback: look for token= in body
        size_t p = req.find("token=\"");
        if (p != std::string::npos) {
            size_t q = req.find('"', p + 7);
            if (q != std::string::npos) cfgTok = req.substr(p + 7, q - p - 7);
        }
    }
    if (cfgTok.empty()) return "<trt:SetVideoEncoderConfigurationResponse/>";

    std::lock_guard<std::mutex> lk(g_stateMtx);
    ensureFixedVecState();
    auto& v = g_vecState[cfgTok];
    std::string enc = extractInnerTag(req, "Encoding");
    if (!enc.empty()) v.encoding = enc;
    std::string ws = extractInnerTag(req, "Width");
    std::string hs = extractInnerTag(req, "Height");
    try { if (!ws.empty()) v.width = std::stoi(ws); } catch (...) {}
    try { if (!hs.empty()) v.height = std::stoi(hs); } catch (...) {}
    std::string fr = extractInnerTag(req, "FrameRateLimit");
    try { if (!fr.empty()) v.frameRate = std::stoi(fr); } catch (...) {}
    std::string br = extractInnerTag(req, "BitrateLimit");
    try { if (!br.empty()) v.bitrate = std::stoi(br); } catch (...) {}
    std::string gv = extractInnerTag(req, "GovLength");
    try { if (!gv.empty()) v.govLength = std::stoi(gv); } catch (...) {}
    std::string h264p = extractInnerTag(req, "H264Profile");
    if (!h264p.empty()) v.h264Profile = h264p;
    std::string ql = extractInnerTag(req, "Quality");
    try { if (!ql.empty()) v.quality = std::stoi(ql); } catch (...) {}
    return "<trt:SetVideoEncoderConfigurationResponse/>";
}

std::string MediaLegacyHandler::handleGetGuaranteedNumberOfVideoEncoderInstances(const std::string& req) {
    (void)req;
    // Declare JPEG + H264 support (server có profile_jpeg và profile_main/sub1/sub2).
    return
        "<trt:GetGuaranteedNumberOfVideoEncoderInstancesResponse>"
          "<trt:TotalNumber>2</trt:TotalNumber>"
          "<trt:JPEG>1</trt:JPEG>"
          "<trt:H264>1</trt:H264>"
        "</trt:GetGuaranteedNumberOfVideoEncoderInstancesResponse>";
}

std::string MediaLegacyHandler::handleGetStreamUri(const std::string& req) {
    std::string token = extractInnerTag(req, "ProfileToken");
    std::string stream = "main";
    if (token == "profile_sub1") stream = "sub1";
    else if (token == "profile_sub2") stream = "sub2";
    std::ostringstream os;
    os << "<trt:GetStreamUriResponse>"
       << "<trt:MediaUri>"
         << "<tt:Uri>rtsp://" << g_deviceIp << ":8554/" << stream << "</tt:Uri>"
         << "<tt:InvalidAfterConnect>false</tt:InvalidAfterConnect>"
         << "<tt:InvalidAfterReboot>false</tt:InvalidAfterReboot>"
         << "<tt:Timeout>PT60S</tt:Timeout>"
       << "</trt:MediaUri>"
       << "</trt:GetStreamUriResponse>";
    return os.str();
}

std::string MediaLegacyHandler::handleGetSnapshotUri(const std::string& req) {
    std::string token = extractInnerTag(req, "ProfileToken");
    if (token.empty()) token = "profile_main";
    std::ostringstream os;
    os << "<trt:GetSnapshotUriResponse>"
       << "<trt:MediaUri>"
         << "<tt:Uri>http://" << g_deviceIp << ":" << g_httpPort
                                  << "/snapshot?token=" << token << "</tt:Uri>"
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
        return wrap(actUrl("GetVideoSourceConfigurationOptions"), rel, handleGetVideoSourceConfigurationOptions());
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
        return wrap(actUrl("GetVideoEncoderConfigurationOptions"), rel, handleGetVideoEncoderConfigurationOptions());
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

    // Metadata (empty list — không hỗ trợ)
    if (req.find("GetMetadataConfigurations") != std::string::npos)
        return wrap(actUrl("GetMetadataConfigurations"), rel, handleGetMetadataConfigurations());

    return "";  // op không nhận diện, để gSOAP fault mặc định
}
