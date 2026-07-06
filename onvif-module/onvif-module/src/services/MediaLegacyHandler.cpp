// MediaLegacyHandler.cpp — Media Service ver10 (Profile S/G/Q backward compat).
// Toàn bộ ops trả XML thủ công. State đơn giản: 3 fixed profiles
// (profile_main/sub1/sub2) khớp Media2, VideoSource src_main, VideoEncoder
// per profile. Không backend real — profile CRUD chỉ ack (không persist Media1 riêng).

#include "services/MediaLegacyHandler.h"
#include <sstream>

namespace {
const char* NS_MEDIA1 = "http://www.onvif.org/ver10/media/wsdl";
const char* ACT = "http://www.onvif.org/ver10/media/wsdl/Media/";

std::string g_deviceIp = "127.0.0.1";
int         g_httpPort = 8080;
int         g_rtspPort = 8554;

// Build wsa:Action URL cho response op X: prefix + "XResponse"
std::string actUrl(const char* op) {
    return std::string(ACT) + op + "Response";
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

// ── Profile XML fragment ────────────────────────────────────────────────────
// Media1 tt:Profile struct: VideoSourceConfiguration + VideoEncoderConfiguration
std::string MediaLegacyHandler::profileXml(const char* wrapperElem,
                                            const char* token, const char* name, bool includeVEC) {
    std::ostringstream os;
    os << "<trt:" << wrapperElem << " fixed=\"true\" token=\"" << token << "\">"
       << "<tt:Name>" << name << "</tt:Name>"
       // VideoSourceConfiguration
       << "<tt:VideoSourceConfiguration token=\"video_source_config\">"
         << "<tt:Name>VideoSourceConfig</tt:Name>"
         << "<tt:UseCount>1</tt:UseCount>"
         << "<tt:SourceToken>src_main</tt:SourceToken>"
         << "<tt:Bounds x=\"0\" y=\"0\" width=\"1920\" height=\"1080\"/>"
       << "</tt:VideoSourceConfiguration>";
    if (includeVEC) {
        // VideoEncoderConfiguration
        const char* codec = "H264";
        int w = 1920, h = 1080, fr = 30, br = 4000;
        std::string vecToken = "video_encoder_config";
        std::string vecName = "VideoEncoderConfig";
        if (std::string(token) == "profile_sub1") {
            w = 1280; h = 720; fr = 15; br = 2000;
            vecToken = "video_encoder_config_profile_sub1";
            vecName = "VideoEncoderConfig_sub1";
        } else if (std::string(token) == "profile_sub2") {
            // Media1 schema KHÔNG hỗ trợ H265 (chỉ JPEG/MPEG4/H264).
            // Dùng H264 480p cho sub2 (khác Media2 dùng H265).
            codec = "H264";
            w = 640; h = 480; fr = 10; br = 512;
            vecToken = "video_encoder_config_profile_sub2";
            vecName = "VideoEncoderConfig_sub2";
        } else {
            w = 3840; h = 2160; fr = 30; br = 20000;
        }
        os << "<tt:VideoEncoderConfiguration token=\"" << vecToken << "\">"
           << "<tt:Name>" << vecName << "</tt:Name>"
           << "<tt:UseCount>1</tt:UseCount>"
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
            os << "<tt:H264>"
                 << "<tt:GovLength>30</tt:GovLength>"
                 << "<tt:H264Profile>Main</tt:H264Profile>"
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
    os << "<trt:GetProfilesResponse>"
       << profileXml("Profiles", "profile_main", "Main 4K", true)
       << profileXml("Profiles", "profile_sub1", "Sub1 720p", true)
       << profileXml("Profiles", "profile_sub2", "Sub2 480p", true)
       << "</trt:GetProfilesResponse>";
    return os.str();
}

std::string MediaLegacyHandler::handleGetProfile(const std::string& req) {
    std::string token = extractInnerTag(req, "ProfileToken");
    const char* name = "Main 4K";
    if (token == "profile_sub1") name = "Sub1 720p";
    else if (token == "profile_sub2") name = "Sub2 480p";
    std::ostringstream os;
    os << "<trt:GetProfileResponse>"
       << profileXml("Profile", token.c_str(), name, true)
       << "</trt:GetProfileResponse>";
    return os.str();
}

std::string MediaLegacyHandler::handleCreateProfile(const std::string& req) {
    std::string name = extractInnerTag(req, "Name");
    std::string token = extractInnerTag(req, "Token");
    if (token.empty()) token = "profile_" + name;
    std::ostringstream os;
    os << "<trt:CreateProfileResponse>"
       << profileXml("Profile", token.c_str(), name.c_str(), false)
       << "</trt:CreateProfileResponse>";
    return os.str();
}

std::string MediaLegacyHandler::handleDeleteProfile(const std::string& req) {
    (void)req;
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
    (void)req;
    return "<trt:AddVideoSourceConfigurationResponse/>";
}

std::string MediaLegacyHandler::handleRemoveVideoSourceConfiguration(const std::string& req) {
    (void)req;
    return "<trt:RemoveVideoSourceConfigurationResponse/>";
}

std::string MediaLegacyHandler::handleSetVideoSourceConfiguration(const std::string& req) {
    (void)req;
    return "<trt:SetVideoSourceConfigurationResponse/>";
}

std::string MediaLegacyHandler::handleGetVideoEncoderConfigurations() {
    return
        "<trt:GetVideoEncoderConfigurationsResponse>"
          "<trt:Configurations token=\"video_encoder_config\">"
            "<tt:Name>VideoEncoderConfig</tt:Name><tt:UseCount>1</tt:UseCount>"
            "<tt:Encoding>H264</tt:Encoding>"
            "<tt:Resolution><tt:Width>3840</tt:Width><tt:Height>2160</tt:Height></tt:Resolution>"
            "<tt:Quality>5</tt:Quality>"
            "<tt:RateControl><tt:FrameRateLimit>30</tt:FrameRateLimit>"
              "<tt:EncodingInterval>1</tt:EncodingInterval>"
              "<tt:BitrateLimit>20000</tt:BitrateLimit></tt:RateControl>"
            "<tt:H264><tt:GovLength>30</tt:GovLength><tt:H264Profile>Main</tt:H264Profile></tt:H264>"
            "<tt:Multicast><tt:Address><tt:Type>IPv4</tt:Type>"
              "<tt:IPv4Address>239.0.0.1</tt:IPv4Address></tt:Address>"
              "<tt:Port>32000</tt:Port><tt:TTL>1</tt:TTL>"
              "<tt:AutoStart>false</tt:AutoStart></tt:Multicast>"
            "<tt:SessionTimeout>PT60S</tt:SessionTimeout>"
          "</trt:Configurations>"
          "<trt:Configurations token=\"video_encoder_config_profile_sub1\">"
            "<tt:Name>VideoEncoderConfig_sub1</tt:Name><tt:UseCount>1</tt:UseCount>"
            "<tt:Encoding>H264</tt:Encoding>"
            "<tt:Resolution><tt:Width>1280</tt:Width><tt:Height>720</tt:Height></tt:Resolution>"
            "<tt:Quality>5</tt:Quality>"
            "<tt:RateControl><tt:FrameRateLimit>15</tt:FrameRateLimit>"
              "<tt:EncodingInterval>1</tt:EncodingInterval>"
              "<tt:BitrateLimit>2000</tt:BitrateLimit></tt:RateControl>"
            "<tt:H264><tt:GovLength>30</tt:GovLength><tt:H264Profile>Main</tt:H264Profile></tt:H264>"
            "<tt:Multicast><tt:Address><tt:Type>IPv4</tt:Type>"
              "<tt:IPv4Address>239.0.0.1</tt:IPv4Address></tt:Address>"
              "<tt:Port>32000</tt:Port><tt:TTL>1</tt:TTL>"
              "<tt:AutoStart>false</tt:AutoStart></tt:Multicast>"
            "<tt:SessionTimeout>PT60S</tt:SessionTimeout>"
          "</trt:Configurations>"
          "<trt:Configurations token=\"video_encoder_config_profile_sub2\">"
            "<tt:Name>VideoEncoderConfig_sub2</tt:Name><tt:UseCount>1</tt:UseCount>"
            "<tt:Encoding>H264</tt:Encoding>"
            "<tt:Resolution><tt:Width>640</tt:Width><tt:Height>480</tt:Height></tt:Resolution>"
            "<tt:Quality>5</tt:Quality>"
            "<tt:RateControl><tt:FrameRateLimit>10</tt:FrameRateLimit>"
              "<tt:EncodingInterval>1</tt:EncodingInterval>"
              "<tt:BitrateLimit>512</tt:BitrateLimit></tt:RateControl>"
            "<tt:H264><tt:GovLength>30</tt:GovLength><tt:H264Profile>Main</tt:H264Profile></tt:H264>"
            "<tt:Multicast><tt:Address><tt:Type>IPv4</tt:Type>"
              "<tt:IPv4Address>239.0.0.1</tt:IPv4Address></tt:Address>"
              "<tt:Port>32000</tt:Port><tt:TTL>1</tt:TTL>"
              "<tt:AutoStart>false</tt:AutoStart></tt:Multicast>"
            "<tt:SessionTimeout>PT60S</tt:SessionTimeout>"
          "</trt:Configurations>"
        "</trt:GetVideoEncoderConfigurationsResponse>";
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
    return
        "<trt:GetVideoEncoderConfigurationOptionsResponse>"
          "<trt:Options>"
            "<tt:QualityRange><tt:Min>0</tt:Min><tt:Max>10</tt:Max></tt:QualityRange>"
            "<tt:H264>"
              "<tt:ResolutionsAvailable><tt:Width>3840</tt:Width><tt:Height>2160</tt:Height></tt:ResolutionsAvailable>"
              "<tt:ResolutionsAvailable><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:ResolutionsAvailable>"
              "<tt:ResolutionsAvailable><tt:Width>1280</tt:Width><tt:Height>720</tt:Height></tt:ResolutionsAvailable>"
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
    (void)req;
    return "<trt:AddVideoEncoderConfigurationResponse/>";
}

std::string MediaLegacyHandler::handleRemoveVideoEncoderConfiguration(const std::string& req) {
    (void)req;
    return "<trt:RemoveVideoEncoderConfigurationResponse/>";
}

std::string MediaLegacyHandler::handleSetVideoEncoderConfiguration(const std::string& req) {
    (void)req;
    return "<trt:SetVideoEncoderConfigurationResponse/>";
}

std::string MediaLegacyHandler::handleGetGuaranteedNumberOfVideoEncoderInstances(const std::string& req) {
    (void)req;
    // Schema Media1: TotalNumber (required) + optional JPEG/H264/MPEG4. H265 KHÔNG có.
    return
        "<trt:GetGuaranteedNumberOfVideoEncoderInstancesResponse>"
          "<trt:TotalNumber>1</trt:TotalNumber>"
          "<trt:JPEG>1</trt:JPEG>"
          "<trt:H264>1</trt:H264>"
          "<trt:MPEG4>1</trt:MPEG4>"
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

    return "";  // op không nhận diện, để gSOAP fault mặc định
}
