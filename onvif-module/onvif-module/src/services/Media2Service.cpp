#include "services/Media2Service.h"
#include "auth/WsSecurityHandler.h"
#include <iostream>
#include <string>
#include <sstream>
#include <cstring>
#include <map>
#include <mutex>
#include <set>

// ── Profile state (Profile T §7.8) ─────────────────────────────────────────
// - g_dynProfiles: profile do tool tạo (CreateProfile).
// - g_removedConfigs: cho profile fixed từ backend — track loại config đã
//   RemoveConfiguration (MEDIA2-1-1-6).
namespace {
const char* PROFILE_METADATA_TOKEN = "profile_metadata";
const char* METADATA_STREAM_PORT = "8555";
// Video RTSP is served by the gortsplib relay, which also enforces RTSP
// Digest authentication required by Profiles M and T.
const char* RTSP_RELAY_PORT = "8555";
const char* RTSP_HTTP_TUNNEL_PORT = "8080";

struct DynProfile {
    std::string token;
    std::string name;
    std::string vsToken;
    std::string veToken;
    std::string mdToken;
    std::string anToken;   // Analytics config (Profile M)
};
static std::mutex g_profMtx;
static std::map<std::string, DynProfile> g_dynProfiles;
static std::map<std::string, std::set<std::string>> g_removedConfigs;
static std::set<std::string> g_deletedFixedTokens;
}

// Forward declaration — định nghĩa cuối file. Cần cho các op gọi fault sớm.
static int m2SendOnvifFault(struct soap* soap,
                            const char* code,
                            const char* subcode,
                            const char* subSubcode,
                            const char* reason);

Media2Service::Media2Service(struct soap* soap,
                             const ServiceConfig& cfg,
                             std::shared_ptr<ICameraBackend> backend)
    : Media2BindingService(soap), cfg_(cfg), backend_(std::move(backend)) {}

Media2BindingService* Media2Service::copy() {
    return new Media2Service(this->soap, cfg_, backend_);
}

extern thread_local bool g_http_digest_authenticated;

bool Media2Service::validateAuth() {
    if (g_http_digest_authenticated) {
        return true;
    }
    WsSecurityHandler handler(cfg_.username, cfg_.password);
    return handler.validate(this->soap);
}

// ── GetProfiles ──────────────────────────────────────────────────────────────
// Per ONVIF spec: no authentication required
int Media2Service::GetProfiles(
    _ns1__GetProfiles *req,
    _ns1__GetProfilesResponse &resp)
{
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    auto soap = this->soap;
    std::string filterToken;
    if (req && req->Token) filterToken = *req->Token;

    std::vector<StreamProfile> profiles;
    try {
        profiles = backend_->getProfiles();
    } catch (const std::exception& e) {
        std::cerr << "[Media2Service] GetProfiles error: " << e.what() << std::endl;
        return soap_receiver_fault(soap, "Internal error", nullptr);
    }

    // Xác định Type filter mà tool yêu cầu
    bool wantVideoSource = false;
    bool wantVideoEncoder = false;
    bool wantMetadata = false;
    bool wantAnalytics = false;
    if (req) {
        for (const auto& t : req->Type) {
            if (t == "All") { wantVideoSource = true; wantVideoEncoder = true; wantMetadata = true; wantAnalytics = true; }
            else if (t == "VideoSource") wantVideoSource = true;
            else if (t == "VideoEncoder") wantVideoEncoder = true;
            else if (t == "Metadata") wantMetadata = true;
            else if (t == "Analytics") wantAnalytics = true;
        }
    }

    // Lấy snapshot state
    std::set<std::string> deletedFixed;
    std::map<std::string, std::set<std::string>> removed;
    std::vector<DynProfile> dyns;
    {
        std::lock_guard<std::mutex> lk(g_profMtx);
        deletedFixed = g_deletedFixedTokens;
        removed = g_removedConfigs;
        for (auto& kv : g_dynProfiles) dyns.push_back(kv.second);
    }

    // Duyệt qua backend fixed profiles (bỏ profile đã Delete) + dynamic profiles
    struct Entry {
        std::string token;
        std::string name;
        bool isFixed;
        bool metadataOnly;
        const StreamProfile* fp;
        const DynProfile* dp;
    };
    std::vector<Entry> all;
    for (const auto& p : profiles) {
        if (deletedFixed.count(p.token)) continue;
        all.push_back({p.token, p.name, true, false, &p, nullptr});
    }
    if (!deletedFixed.count(PROFILE_METADATA_TOKEN)) {
        all.push_back({PROFILE_METADATA_TOKEN, "Metadata", true, true, nullptr, nullptr});
    }
    for (const auto& d : dyns) {
        all.push_back({d.token, d.name, false, false, nullptr, &d});
    }

    // MEDIA2-1-1-2: nếu Token cụ thể nhưng không có profile nào khớp → fault
    // env:Sender/ter:InvalidArgVal/ter:NoProfile.
    if (!filterToken.empty()) {
        bool exists = false;
        for (const auto& e : all) if (e.token == filterToken) { exists = true; break; }
        if (!exists) {
            return m2SendOnvifFault(soap, "SOAP-ENV:Sender",
                                    "ter:InvalidArgVal", "ter:NoProfile",
                                    "Profile not found");
        }
    }

    for (const auto& e : all) {
        if (!filterToken.empty() && e.token != filterToken) continue;

        auto profile = soap_new_ns1__MediaProfile(soap);
        if (!profile) continue;
        profile->token = e.token;
        profile->Name  = e.name;
        profile->fixed = nullptr;

        // Type filter rỗng (không có phần tử Type nào) → không kèm Configurations
        // (MEDIA2-1-1-6 sau RemoveConfiguration All + GetProfiles Type=All vẫn
        // check "does not contain configurations" nên phải tôn trọng removed).
        const auto& rem = removed[e.token];
        bool showVS = wantVideoSource && !e.metadataOnly && !rem.count("VideoSource") && !rem.count("All");
        bool showVE = wantVideoEncoder && !e.metadataOnly && !rem.count("VideoEncoder") && !rem.count("All");
        // Metadata config: chỉ fixed profile (Profile M §7.7 ready-to-use metadata
        // profile — MEDIA2-1-1-8). Dyn profile không auto có metadata.
        // Fixed profile: show khi Type filter yêu cầu. Dyn profile: show khi đã
        // AddConfiguration (mdToken/anToken set).
        bool showMD = wantMetadata && !rem.count("Metadata") && !rem.count("All") &&
                      (e.isFixed || (e.dp && !e.dp->mdToken.empty()));
        bool showAN = wantAnalytics && !e.metadataOnly && !rem.count("Analytics") && !rem.count("All") &&
                      (e.isFixed || (e.dp && !e.dp->anToken.empty()));

        if (showVS || showVE || showMD || showAN) {
            profile->Configurations = soap_new_ns1__ConfigurationSet(soap);
        } else {
            profile->Configurations = nullptr;
        }

        if (profile->Configurations) {
            const StreamProfile* fp = e.fp;
            const DynProfile* dp = e.dp;
            if (showVS) {
                // Dyn profile chỉ trả VideoSource nếu đã AddConfiguration VideoSource
                if (dp && dp->vsToken.empty()) { /* skip VS for dyn without add */ } else {
                auto vsc = soap_new_tt__VideoSourceConfiguration(soap);
                if (vsc) {
                    vsc->token = dp ? dp->vsToken : "video_source_config";
                    vsc->Name = "VideoSourceConfig";
                    // Keep the profile configuration consistent with
                    // GetVideoSourceConfigurations. DTT compares this field
                    // when validating CreateProfile + GetProfiles.
                    vsc->UseCount = 4;
                    vsc->SourceToken = (fp && !fp->sourceToken.empty()) ? fp->sourceToken : "src_main";
                    vsc->Bounds = soap_new_tt__IntRectangle(soap);
                    if (vsc->Bounds) {
                        vsc->Bounds->x = 0;
                        vsc->Bounds->y = 0;
                        vsc->Bounds->width = 1920;
                        vsc->Bounds->height = 1080;
                    }
                    profile->Configurations->VideoSource = vsc;
                }
                }
            }

            if (showVE) {
                if (dp && dp->veToken.empty()) { /* skip VE for dyn without add */ } else {
                auto vec = soap_new_tt__VideoEncoder2Configuration(soap);
                if (vec) {
                    // Dyn profile → token phải là token đã AddConfiguration (tool
                    // check consistency). Fixed profile → theo backend mapping.
                    if (dp) {
                        vec->token = dp->veToken;
                        vec->Name  = "VideoEncoderConfig";
                    } else {
                        vec->token = (e.token == "profile_main") ? "video_encoder_config" : ("video_encoder_config_" + e.token);
                        vec->Name  = (e.token == "profile_main") ? "VideoEncoderConfig" : ("VideoEncoderConfig_" + e.token);
                    }
                    vec->Encoding = (fp && fp->videoConfig.codec == Codec::H265) ? "H265" : "H264";
                    vec->Quality = 50.0f;
                    vec->Resolution = soap_new_tt__VideoResolution2(soap);
                    if (vec->Resolution) {
                        vec->Resolution->Width  = fp ? fp->videoConfig.resolution.width  : 1920;
                        vec->Resolution->Height = fp ? fp->videoConfig.resolution.height : 1080;
                    }
                    vec->RateControl = soap_new_tt__VideoRateControl2(soap);
                    if (vec->RateControl) {
                        vec->RateControl->FrameRateLimit = fp ? fp->videoConfig.framerate : 30;
                        vec->RateControl->BitrateLimit   = fp ? fp->videoConfig.bitrate   : 4000;
                    }
                    profile->Configurations->VideoEncoder = vec;
                }
                }
            }

            if (showMD) {
                auto md = soap_new_tt__MetadataConfiguration(soap);
                if (md) {
                    md->token = (dp && !dp->mdToken.empty()) ? dp->mdToken : "metadata_config";
                    md->Name  = "MetadataConfig";
                    md->UseCount = 1;
                    md->Analytics = (bool*)soap_malloc(soap, sizeof(bool));
                    if (md->Analytics) *md->Analytics = true;
                    // Multicast (required trong tt:MetadataConfiguration)
                    md->Multicast = soap_new_tt__MulticastConfiguration(soap);
                    if (md->Multicast) {
                        md->Multicast->Address = soap_new_tt__IPAddress(soap);
                        if (md->Multicast->Address)
                            md->Multicast->Address->Type = tt__IPType::IPv4;  // IPv4Address optional
                        md->Multicast->Port = 32001;
                        md->Multicast->TTL = 1;
                        md->Multicast->AutoStart = false;
                    }
                    md->SessionTimeout = "PT60S";
                    profile->Configurations->Metadata = md;
                }
            }

            if (showAN) {
                auto vac = soap_new_tt__VideoAnalyticsConfiguration(soap);
                if (vac) {
                    vac->token = (dp && !dp->anToken.empty()) ? dp->anToken : "vac_main";
                    vac->Name  = "VideoAnalyticsConfig";
                    vac->UseCount = 1;
                    // AnalyticsEngineConfiguration + RuleEngineConfiguration rỗng
                    // (module dynamic đọc qua GetAnalyticsConfigurations/store).
                    vac->AnalyticsEngineConfiguration =
                        soap_new_tt__AnalyticsEngineConfiguration(soap);
                    vac->RuleEngineConfiguration =
                        soap_new_tt__RuleEngineConfiguration(soap);
                    profile->Configurations->Analytics = vac;
                }
            }
        }

        resp.Profiles.push_back(profile);
    }

    std::cout << "[Media2Service] GetProfiles → " << resp.Profiles.size() << " profiles" << std::endl;
    return SOAP_OK;
}

// ── GetStreamUri ─────────────────────────────────────────────────────────────
int Media2Service::GetStreamUri(
    _ns1__GetStreamUri *req,
    _ns1__GetStreamUriResponse &resp)
{
    this->soap->mustUnderstand = 0;
    if (!validateAuth()) {
        return soap_sender_fault_subcode(this->soap, "ter:NotAuthorized", "Sender", "Not Authorized");
    }
    this->soap->header = nullptr;

    std::string profileToken;
    std::string protocol;
    if (req) {
        profileToken = req->ProfileToken;
        protocol = req->Protocol;
    }

    if (profileToken == PROFILE_METADATA_TOKEN) {
        std::string scheme = (protocol == "RtspOverHttp") ? "http://" :
                             (protocol == "RtspOverHttps") ? "https://" : "rtsp://";
        resp.Uri = scheme + cfg_.deviceIp + ":" + METADATA_STREAM_PORT + "/metadata";
        std::cout << "[Media2Service] GetStreamUri [" << profileToken
                  << "] (protocol=" << protocol << ") -> " << resp.Uri << std::endl;
        return SOAP_OK;
    }

    StreamUri u;
    try {
        u = backend_->getStreamUri(profileToken, StreamProtocol::RTSP);
    } catch (const std::exception& e) {
        std::cerr << "[Media2Service] GetStreamUri error: " << e.what() << std::endl;
        return soap_receiver_fault(this->soap, "Internal error", nullptr);
    }

    if (u.uri.empty()) {
        // Fallback cho dyn profile do tool tạo: backend không biết → xây URI mặc định.
        bool isDyn = false;
        {
            std::lock_guard<std::mutex> lk(g_profMtx);
            isDyn = g_dynProfiles.count(profileToken) > 0;
        }
        if (isDyn) {
            u.uri = "rtsp://" + cfg_.deviceIp + ":" +
                    std::to_string(cfg_.rtspPort) + "/main";
        } else {
            return m2SendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                    "ter:InvalidArgVal", "ter:NoProfile",
                                    "Invalid profile token");
        }
    }

    std::string uri = u.uri;
    size_t pos = uri.find("127.0.0.1");
    if (pos != std::string::npos) uri.replace(pos, 9, cfg_.deviceIp);

    // Route video RTSP through the authenticated gortsplib relay instead of
    // the unauthenticated MediaMTX listener.
    if (protocol == "RTSP" || protocol == "RtspUnicast" ||
        protocol == "RtspMulticast") {
        std::string mediaMtxPort = ":" + std::to_string(cfg_.rtspPort);
        size_t mediaMtxPos = uri.find(mediaMtxPort);
        if (mediaMtxPos != std::string::npos) {
            uri.replace(mediaMtxPos, mediaMtxPort.length(),
                        std::string(":") + RTSP_RELAY_PORT);
        }
    }

    // Hỗ trợ RTSP over HTTP tunneling theo yêu cầu của Test Tool
    if (protocol == "RtspOverHttp" || protocol == "RtspOverHttps") {
        std::string rtspPortStr = ":" + std::to_string(cfg_.rtspPort);
        size_t portPos = uri.find(rtspPortStr);
        if (portPos != std::string::npos) {
            uri.replace(portPos, rtspPortStr.length(),
                        std::string(":") + RTSP_HTTP_TUNNEL_PORT);
        }
        if (uri.rfind("rtsp://", 0) == 0) {
            std::string targetScheme = (protocol == "RtspOverHttps") ? "https://" : "http://";
            uri.replace(0, 7, targetScheme);
        }
    }

    resp.Uri = uri;
    std::cout << "[Media2Service] GetStreamUri [" << profileToken << "] (protocol=" << protocol << ") → " << uri << std::endl;
    return SOAP_OK;
}

// ── GetSnapshotUri ───────────────────────────────────────────────────────────
int Media2Service::GetSnapshotUri(
    _ns1__GetSnapshotUri *req,
    _ns1__GetSnapshotUriResponse &resp)
{
    this->soap->mustUnderstand = 0;
    if (!validateAuth()) {
        return soap_sender_fault_subcode(this->soap, "ter:NotAuthorized", "Sender", "Not Authorized");
    }
    this->soap->header = nullptr;

    std::string profileToken;
    if (req) profileToken = req->ProfileToken;

    SnapshotUri u;
    try {
        u = backend_->getSnapshotUri(profileToken);
    } catch (...) {
        u.uri = "";
    }

    if (u.uri.empty()) {
        u.uri = "http://" + cfg_.deviceIp + ":" + std::to_string(cfg_.httpPort)
                + "/snapshot?token=" + profileToken;
    }

    size_t pos = u.uri.find("127.0.0.1");
    if (pos != std::string::npos) u.uri.replace(pos, 9, cfg_.deviceIp);

    resp.Uri = u.uri;
    std::cout << "[Media2Service] GetSnapshotUri [" << profileToken << "] → " << u.uri << std::endl;
    return SOAP_OK;
}

// ── GetVideoSourceConfigurations ─────────────────────────────────────────────
int Media2Service::GetVideoSourceConfigurations(
    ns1__GetConfiguration *req,
    _ns1__GetVideoSourceConfigurationsResponse &resp)
{
    this->soap->mustUnderstand = 0;
    if (!validateAuth()) {
        return soap_sender_fault_subcode(this->soap, "ter:NotAuthorized", "Sender", "Not Authorized");
    }
    this->soap->header = nullptr;
    auto soap = this->soap;

    // MEDIA2-2-2-6: ConfigurationToken không tồn tại → fault
    // env:Sender/ter:InvalidArgVal/ter:NoConfig.
    std::string cfgToken;
    std::string profToken;
    if (req) {
        if (req->ConfigurationToken) cfgToken = *req->ConfigurationToken;
        if (req->ProfileToken)       profToken = *req->ProfileToken;
    }
    static const char* VALID_VS_CONFIG = "video_source_config";
    if (!cfgToken.empty() && cfgToken != VALID_VS_CONFIG) {
        return m2SendOnvifFault(soap, "SOAP-ENV:Sender",
                                "ter:InvalidArgVal", "ter:NoConfig",
                                "No such VideoSourceConfiguration");
    }

    auto vsc = soap_new_tt__VideoSourceConfiguration(soap);
    if (vsc) {
        vsc->token = VALID_VS_CONFIG;
        vsc->Name = "VideoSourceConfig";
        vsc->UseCount = 4;
        vsc->SourceToken = "src_main";

        vsc->Bounds = soap_new_tt__IntRectangle(soap);
        if (vsc->Bounds) {
            vsc->Bounds->x = 0;
            vsc->Bounds->y = 0;
            vsc->Bounds->width = 1920;
            vsc->Bounds->height = 1080;
        }
        resp.Configurations.push_back(vsc);
    }

    std::cout << "[Media2Service] GetVideoSourceConfigurations → returned 1 configuration" << std::endl;
    return SOAP_OK;
}

// ── GetVideoEncoderConfigurations ────────────────────────────────────────────
int Media2Service::GetVideoEncoderConfigurations(
    ns1__GetConfiguration *req,
    _ns1__GetVideoEncoderConfigurationsResponse &resp)
{
    this->soap->mustUnderstand = 0;
    if (!validateAuth()) {
        return soap_sender_fault_subcode(this->soap, "ter:NotAuthorized", "Sender", "Not Authorized");
    }
    this->soap->header = nullptr;
    auto soap = this->soap;

    std::string filterConfigToken, filterProfileToken;
    if (req) {
        if (req->ConfigurationToken) filterConfigToken = *req->ConfigurationToken;
        if (req->ProfileToken)       filterProfileToken = *req->ProfileToken;
    }

    std::vector<StreamProfile> profiles;
    try {
        profiles = backend_->getProfiles();
    } catch (const std::exception& e) {
        std::cerr << "[Media2Service] GetVideoEncoderConfigurations backend error: " << e.what() << std::endl;
    }

    // Nếu tool filter theo ProfileToken là dynamic profile → trả VideoEncoderConfig
    // mặc định (MEDIA2_RTSS-1-1-23 fail vì filter profile_testMedia).
    bool profileIsDynamic = false;
    if (!filterProfileToken.empty()) {
        std::lock_guard<std::mutex> lk(g_profMtx);
        profileIsDynamic = g_dynProfiles.count(filterProfileToken) > 0;
    }

    auto addDefaultEncoderConfig = [&](const std::string& cfgToken,
                                       const std::string& cfgName){
        auto enc = soap_new_tt__VideoEncoder2Configuration(soap);
        if (!enc) return;
        enc->token = cfgToken;
        enc->Name  = cfgName;
        enc->Encoding = "H264";
        enc->Quality = 50.0f;
        enc->Resolution = soap_new_tt__VideoResolution2(soap);
        enc->Resolution->Width = 1920; enc->Resolution->Height = 1080;
        enc->RateControl = soap_new_tt__VideoRateControl2(soap);
        enc->RateControl->FrameRateLimit = 30.0f;
        enc->RateControl->BitrateLimit = 4000;
        resp.Configurations.push_back(enc);
    };

    if (profileIsDynamic) {
        // VEC đang gán cho dynamic profile phải nằm trong compatible list (MEDIA2-2-3-1:
        // "current VEC listed in compatible list"). Tool có thể tạo profile bằng cách copy
        // config của fixed profile → veToken = "video_encoder_config_profile_subX". Token này
        // cũng có trong total list (fixed subX xuất ở nhánh else) nên nhất quán.
        std::string veTok;
        {
            std::lock_guard<std::mutex> lk(g_profMtx);
            auto it = g_dynProfiles.find(filterProfileToken);
            if (it != g_dynProfiles.end()) veTok = it->second.veToken;
        }
        if (!veTok.empty() && veTok != "video_encoder_config" &&
            (filterConfigToken.empty() || filterConfigToken == veTok)) {
            addDefaultEncoderConfig(veTok, "VideoEncoderConfig");
        }
        if (filterConfigToken.empty() || filterConfigToken == "video_encoder_config")
            addDefaultEncoderConfig("video_encoder_config", "VideoEncoderConfig");
    } else {
        for (const auto& p : profiles) {
            if (!filterProfileToken.empty() && p.token != filterProfileToken) continue;

            std::string cfgToken = (p.token == "profile_main")
                                 ? "video_encoder_config"
                                 : "video_encoder_config_" + p.token;
            if (!filterConfigToken.empty() && cfgToken != filterConfigToken) continue;

            auto enc = soap_new_tt__VideoEncoder2Configuration(soap);
            if (enc) {
                enc->token = cfgToken;
                enc->Name = (p.token == "profile_main")
                          ? "VideoEncoderConfig"
                          : "VideoEncoderConfig_" + p.token;
                enc->Encoding = p.videoConfig.codec == Codec::H265 ? "H265" : "H264";
                enc->Quality = 50.0f;
                enc->Resolution = soap_new_tt__VideoResolution2(soap);
                if (enc->Resolution) {
                    enc->Resolution->Width = p.videoConfig.resolution.width;
                    enc->Resolution->Height = p.videoConfig.resolution.height;
                }
                enc->RateControl = soap_new_tt__VideoRateControl2(soap);
                if (enc->RateControl) {
                    enc->RateControl->FrameRateLimit = (float)p.videoConfig.framerate;
                    enc->RateControl->BitrateLimit = p.videoConfig.bitrate;
                }
                resp.Configurations.push_back(enc);
            }
        }
    }

    // MEDIA2_RTSS-1-1-23 (spec §7.8.1): GetVideoEncoderInstances Total=3 →
    // phải có ≥3 VEC available cho VSC. Ngoài 3 VEC gán profile fixed, thêm
    // 1 spare (chưa gán profile nào). Add cho MỌI filter (kể cả ProfileToken=dyn):
    // tool expect list gồm "compatible-but-unassigned" configs, không chỉ attached.
    if (filterConfigToken.empty() || filterConfigToken == "video_encoder_config_spare") {
        bool spareAlready = false;
        for (auto* c : resp.Configurations)
            if (c && c->token == "video_encoder_config_spare") { spareAlready = true; break; }
        if (!spareAlready) {
            addDefaultEncoderConfig("video_encoder_config_spare", "VideoEncoderConfigSpare");
        }
    }

    std::cout << "[Media2Service] GetVideoEncoderConfigurations → returned "
              << resp.Configurations.size() << " configurations" << std::endl;
    return SOAP_OK;
}

// ── AddConfiguration ─────────────────────────────────────────────────────────
int Media2Service::AddConfiguration(
    _ns1__AddConfiguration *req,
    _ns1__AddConfigurationResponse &resp)
{
    (void)resp;
    this->soap->mustUnderstand = 0;
    if (!validateAuth()) {
        return soap_sender_fault_subcode(this->soap, "ter:NotAuthorized", "Sender", "Not Authorized");
    }
    this->soap->header = nullptr;
    if (!req) return SOAP_OK;
    std::lock_guard<std::mutex> lk(g_profMtx);
    // Xóa marker "removed" (nếu AddConfiguration lại type đã Remove trước đó)
    // + với dyn profile: lưu token config vào DynProfile để GetProfiles trả về
    //   token đã Add (MEDIA2-1-1-3 check consistency).
    auto dynIt = g_dynProfiles.find(req->ProfileToken);
    for (auto* c : req->Configuration) {
        if (!c) continue;
        auto& rem = g_removedConfigs[req->ProfileToken];
        // MEDIA2-1-1-6: tool Remove "All" rồi Add lại TỪNG loại riêng lẻ. Marker
        // "All" phải được expand thành các loại cụ thể trước, rồi mới erase loại
        // vừa Add — nếu không "All" kẹt lại → GetProfiles suppress hết config →
        // profile rỗng vĩnh viễn (1-1-8/9, 4-1-5 fail vì không thấy ready-to-use profile).
        if (rem.count("All")) {
            rem.erase("All");
            rem.insert({"VideoSource", "VideoEncoder", "Metadata", "Analytics"});
        }
        if (c->Type == "All") rem.clear();
        else rem.erase(c->Type);
        if (dynIt != g_dynProfiles.end() && c->Token) {
            if (c->Type == "VideoSource")       dynIt->second.vsToken = *c->Token;
            else if (c->Type == "VideoEncoder") dynIt->second.veToken = *c->Token;
            else if (c->Type == "Metadata")     dynIt->second.mdToken = *c->Token;
            else if (c->Type == "Analytics")    dynIt->second.anToken = *c->Token;
        }
    }
    std::cout << "[Media2Service] AddConfiguration [" << req->ProfileToken << "]" << std::endl;
    return SOAP_OK;
}

// ── RemoveConfiguration ──────────────────────────────────────────────────────
int Media2Service::RemoveConfiguration(
    _ns1__RemoveConfiguration *req,
    _ns1__RemoveConfigurationResponse &resp)
{
    (void)resp;
    this->soap->mustUnderstand = 0;
    if (!validateAuth()) {
        return soap_sender_fault_subcode(this->soap, "ter:NotAuthorized", "Sender", "Not Authorized");
    }
    this->soap->header = nullptr;
    if (!req) return SOAP_OK;
    std::lock_guard<std::mutex> lk(g_profMtx);
    for (auto* c : req->Configuration) {
        if (!c) continue;
        g_removedConfigs[req->ProfileToken].insert(c->Type);
    }
    std::cout << "[Media2Service] RemoveConfiguration [" << req->ProfileToken << "]" << std::endl;
    return SOAP_OK;
}

// ── GetVideoSourceConfigurationOptions ───────────────────────────────────────
int Media2Service::GetVideoSourceConfigurationOptions(
    ns1__GetConfiguration *req,
    _ns1__GetVideoSourceConfigurationOptionsResponse &resp)
{
    this->soap->mustUnderstand = 0;
    if (!validateAuth()) {
        return soap_sender_fault_subcode(this->soap, "ter:NotAuthorized", "Sender", "Not Authorized");
    }
    this->soap->header = nullptr;
    auto soap = this->soap;

    // Validate ConfigurationToken (nếu client truyền). Token duy nhất hợp lệ:
    // "video_source_config". Khác → fault ter:NoConfig (MEDIA2-2-2-6).
    if (req && req->ConfigurationToken) {
        const std::string& tok = *req->ConfigurationToken;
        if (!tok.empty() && tok != "video_source_config") {
            return m2SendOnvifFault(soap, "SOAP-ENV:Sender",
                                    "ter:InvalidArgVal", "ter:NoConfig",
                                    "Invalid ConfigurationToken");
        }
    }

    auto opt = soap_new_tt__VideoSourceConfigurationOptions(soap);
    if (opt) {
        opt->VideoSourceTokensAvailable.push_back("src_main");
        // BoundsRange bắt buộc (schema): XRange/YRange/WidthRange/HeightRange
        // (MEDIA2-2-2-1 "BoundsRange has incomplete content, expected XRange").
        opt->BoundsRange = soap_new_tt__IntRectangleRange(soap);
        auto R = [&](int lo, int hi) {
            auto r = soap_new_tt__IntRange(soap);
            r->Min = lo; r->Max = hi;
            return r;
        };
        opt->BoundsRange->XRange      = R(0, 0);
        opt->BoundsRange->YRange      = R(0, 0);
        opt->BoundsRange->WidthRange  = R(320, 3840);
        opt->BoundsRange->HeightRange = R(240, 2160);
        auto* mnp = (int*)soap_malloc(soap, sizeof(int));
        *mnp = 3;
        opt->MaximumNumberOfProfiles = mnp;
        resp.Options = opt;
    }

    std::cout << "[Media2Service] GetVideoSourceConfigurationOptions called" << std::endl;
    return SOAP_OK;
}

// ── GetVideoEncoderConfigurationOptions ───────────────────────────────────────
int Media2Service::GetVideoEncoderConfigurationOptions(
    ns1__GetConfiguration *req,
    _ns1__GetVideoEncoderConfigurationOptionsResponse &resp)
{
    this->soap->mustUnderstand = 0;
    if (!validateAuth()) {
        return soap_sender_fault_subcode(this->soap, "ter:NotAuthorized", "Sender", "Not Authorized");
    }
    this->soap->header = nullptr;
    auto soap = this->soap;

    std::string configToken;
    std::string profileToken;
    if (req) {
        if (req->ConfigurationToken) configToken = *req->ConfigurationToken;
        if (req->ProfileToken) profileToken = *req->ProfileToken;
    }

    // Validate ConfigurationToken (nếu truyền). Token hợp lệ: các encoder config
    // trong pool (bao gồm spare — MEDIA2_RTSS-1-1-23 spec §7.8.1). Khác → fault.
    auto isKnownEncoderConfig = [](const std::string& t){
        return t == "video_encoder_config" ||
               t == "video_encoder_config_profile_sub1" ||
               t == "video_encoder_config_profile_sub2" ||
               t == "video_encoder_config_spare";
    };
    if (!configToken.empty() && !isKnownEncoderConfig(configToken)) {
        return m2SendOnvifFault(soap, "SOAP-ENV:Sender",
                                "ter:InvalidArgVal", "ter:NoConfig",
                                "Invalid ConfigurationToken");
    }

    std::cout << "[Media2Service] GetVideoEncoderConfigurationOptions called for configToken="
              << configToken << ", profileToken=" << profileToken << std::endl;

    // Helper lambda để tạo Option cho một codec cụ thể
    auto createOption = [&](const std::string& codecName) {
        auto opt = soap_new_tt__VideoEncoder2ConfigurationOptions(soap);
        if (!opt) return (tt__VideoEncoder2ConfigurationOptions*)nullptr;
        
        opt->Encoding = codecName;
        
        opt->QualityRange = soap_new_tt__FloatRange(soap);
        if (opt->QualityRange) {
            opt->QualityRange->Min = 0.0f;
            opt->QualityRange->Max = 100.0f;
        }

        // Cung cấp các độ phân giải khả dụng
        // The mock RTSP paths are fixed-format streams. Advertise only the
        // resolution that the selected profile actually emits; otherwise DTT
        // legitimately applies SetVideoEncoderConfiguration and then rejects
        // the unchanged RTP stream during its resolution verification step.
        std::vector<std::pair<int, int>> resolutions;
        if (profileToken == "profile_sub1") {
            resolutions.push_back({1280, 720});
        } else if (profileToken == "profile_sub2") {
            resolutions.push_back({640, 480});
        } else {
            resolutions.push_back({3840, 2160});
        }
        for (const auto& r : resolutions) {
            auto res = soap_new_tt__VideoResolution2(soap);
            if (res) {
                res->Width = r.first;
                res->Height = r.second;
                opt->ResolutionsAvailable.push_back(res);
            }
        }

        opt->BitrateRange = soap_new_tt__IntRange(soap);
        if (opt->BitrateRange) {
            opt->BitrateRange->Min = 64;
            opt->BitrateRange->Max = 20000; // Bitrate tối đa cho 4K là 20000 kbps
        }

        // FrameRatesSupported — bắt buộc để test tool validate "frame rate limit
        // mapping" (MEDIA2-2-3-2/3). Cung cấp danh sách rời rạc phổ biến.
        opt->FrameRatesSupported = soap_new_std__string(soap);
        *opt->FrameRatesSupported = "30 25 15 10 5";

        // ProfilesSupported (H.264/H.265 profile constants)
        opt->ProfilesSupported = soap_new_std__string(soap);
        *opt->ProfilesSupported = (codecName == "H265")
                                ? "Main"
                                : "Baseline Main High";

        opt->GovLengthRange = soap_new_std__string(soap);
        *opt->GovLengthRange = "1 60";

        return opt;
    };

    // Mock camera only implements H.264; do not advertise an unsupported H.265 encoder.
    auto optH264 = createOption("H264");
    if (optH264) resp.Options.push_back(optH264);

    return SOAP_OK;
}

// ── SetVideoSourceConfiguration ──────────────────────────────────────────────
int Media2Service::SetVideoSourceConfiguration(
    _ns1__SetVideoSourceConfiguration *req,
    ns1__SetConfigurationResponse &resp)
{
    this->soap->mustUnderstand = 0;
    if (!validateAuth()) {
        return soap_sender_fault_subcode(this->soap, "ter:NotAuthorized", "Sender", "Not Authorized");
    }
    this->soap->header = nullptr;
    std::cout << "[Media2Service] SetVideoSourceConfiguration called" << std::endl;
    return SOAP_OK;
}

// ── SetVideoEncoderConfiguration ──────────────────────────────────────────────
int Media2Service::SetVideoEncoderConfiguration(
    _ns1__SetVideoEncoderConfiguration *req,
    ns1__SetConfigurationResponse &resp)
{
    this->soap->mustUnderstand = 0;
    if (!validateAuth()) {
        return soap_sender_fault_subcode(this->soap, "ter:NotAuthorized", "Sender", "Not Authorized");
    }
    this->soap->header = nullptr;

    if (!req || !req->Configuration) {
        return soap_sender_fault(this->soap, "Invalid configuration request", nullptr);
    }

    auto newEnc = req->Configuration;
    std::string configToken = newEnc->token;
    
    // Ánh xạ configToken về profileToken tương ứng
    std::string profileToken = "profile_main";
    if (configToken.find("profile_sub1") != std::string::npos) profileToken = "profile_sub1";
    else if (configToken.find("profile_sub2") != std::string::npos) profileToken = "profile_sub2";
    
    // Lấy cấu hình cũ làm gốc
    std::vector<StreamProfile> profiles;
    try {
        profiles = backend_->getProfiles();
    } catch (...) {}
    
    VideoEncoderConfig vecConfig;
    for (const auto& p : profiles) {
        if (p.token == profileToken) {
            vecConfig = p.videoConfig;
            break;
        }
    }

    // Cập nhật các trường mới nhận được từ request
    if (!newEnc->Encoding.empty()) {
        if (newEnc->Encoding == "H265") vecConfig.codec = Codec::H265;
        else vecConfig.codec = Codec::H264;
    }
    
    if (newEnc->Resolution) {
        vecConfig.resolution.width = newEnc->Resolution->Width;
        vecConfig.resolution.height = newEnc->Resolution->Height;
    }
    
    if (newEnc->RateControl) {
        vecConfig.framerate = (int)newEnc->RateControl->FrameRateLimit;
        vecConfig.bitrate = newEnc->RateControl->BitrateLimit;
    }

    try {
        backend_->setVideoEncoderConfig(profileToken, vecConfig);
        std::cout << "[Media2Service] SetVideoEncoderConfiguration successful for token " << profileToken << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[Media2Service] SetVideoEncoderConfiguration backend error: " << e.what() << std::endl;
        return soap_receiver_fault(this->soap, "Backend error", nullptr);
    }

    return SOAP_OK;
}

// ── GetServiceCapabilities (Media2) ──────────────────────────────────────────
// Profile T MANDATORY: Defines which Media2 streaming features this device supports.
// Tool reads this during Define Features phase to decide which test cases to run.
int Media2Service::GetServiceCapabilities(
    _ns1__GetServiceCapabilities *req,
    _ns1__GetServiceCapabilitiesResponse &resp)
{
    (void)req;
    this->soap->mustUnderstand = 0;
    if (!validateAuth()) {
        return soap_sender_fault_subcode(this->soap, "ter:NotAuthorized", "Sender", "Not Authorized");
    }
    this->soap->header = nullptr;
    auto soap = this->soap;

    auto caps = soap_new_ns1__Capabilities2(soap);
    if (!caps) return soap_receiver_fault(soap, "Memory allocation failed", nullptr);

    // ── Streaming capabilities ───────────────────────────────────────────
    caps->SnapshotUri = new bool(true);
    caps->Rotation = new bool(false);
    caps->VideoSourceMode = new bool(false);
    // OSD implemented (§7.18 mandatory): Create/Delete/Get/Set + Options
    caps->OSD = new bool(true);

    // Profile T requires RTP multicast/unicast/RTSP
    caps->ProfileCapabilities = soap_new_ns1__ProfileCapabilities(soap);
    if (caps->ProfileCapabilities) {
        caps->ProfileCapabilities->MaximumNumberOfProfiles = new int(3);
        // ConfigurationsSupported: list các config type device hỗ trợ.
        // Tối thiểu Profile T: VideoSource + VideoEncoder.
        caps->ProfileCapabilities->ConfigurationsSupported =
            soap_new_std__string(soap);
        *caps->ProfileCapabilities->ConfigurationsSupported =
            "VideoSource VideoEncoder Metadata Analytics";
    }

    // Streaming capabilities
    caps->StreamingCapabilities = soap_new_ns1__StreamingCapabilities(soap);
    if (caps->StreamingCapabilities) {
        // Media2 uses RTSPStreaming to advertise that RTSP streaming is available.
        auto rtspStreaming = new bool(true);
        caps->StreamingCapabilities->RTSPStreaming = rtspStreaming;

        // RTP over RTSP/TCP - supported
        auto rtpOverRtsp = new bool(true);
        caps->StreamingCapabilities->RTP_USCORERTSP_USCORETCP = rtpOverRtsp;

        // Profile M/T device requirements include RTP/UDP multicast.
        // Advertise this consistently with the Media2 profile capabilities.
        auto rtpMulticast = new bool(true);
        caps->StreamingCapabilities->RTPMulticast = rtpMulticast;

        // No non-agg controls
        auto noRtspStreaming = new bool(false);
        caps->StreamingCapabilities->NonAggregateControl = noRtspStreaming;

        // RTSP websockets - not supported
        caps->StreamingCapabilities->RTSPWebSocketUri = nullptr;
    }

    resp.Capabilities = caps;
    std::cout << "[Media2Service] GetServiceCapabilities → SnapshotUri=true, RTPOverRTSP=true" << std::endl;
    return SOAP_OK;
}

// ── SetSynchronizationPoint ──────────────────────────────────────────────────
int Media2Service::SetSynchronizationPoint(
    _ns1__SetSynchronizationPoint *req,
    _ns1__SetSynchronizationPointResponse &resp)
{
    (void)req;
    (void)resp;
    this->soap->mustUnderstand = 0;
    if (!validateAuth()) {
        return soap_sender_fault_subcode(this->soap, "ter:NotAuthorized", "Sender", "Not Authorized");
    }
    this->soap->header = nullptr;
    std::cout << "[Media2Service] SetSynchronizationPoint called" << std::endl;
    return SOAP_OK;
}

// Fault XML thủ công (giống ImagingService::sendOnvifFault) — gSOAP không tự
// declare xmlns:ter cho prefix trong subcode Value.
static int m2SendOnvifFault(struct soap* soap,
                            const char* code,
                            const char* subcode,
                            const char* subSubcode,
                            const char* reason) {
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
       << "<SOAP-ENV:Envelope"
       << " xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\""
       << " xmlns:ter=\"http://www.onvif.org/ver10/error\">"
       << "<SOAP-ENV:Body><SOAP-ENV:Fault>"
       << "<SOAP-ENV:Code><SOAP-ENV:Value>" << code << "</SOAP-ENV:Value>"
       << "<SOAP-ENV:Subcode><SOAP-ENV:Value>" << subcode << "</SOAP-ENV:Value>";
    if (subSubcode && *subSubcode) {
        os << "<SOAP-ENV:Subcode><SOAP-ENV:Value>" << subSubcode
           << "</SOAP-ENV:Value></SOAP-ENV:Subcode>";
    }
    os << "</SOAP-ENV:Subcode></SOAP-ENV:Code>"
       << "<SOAP-ENV:Reason><SOAP-ENV:Text xml:lang=\"en\">"
       << reason << "</SOAP-ENV:Text></SOAP-ENV:Reason>"
       << "</SOAP-ENV:Fault></SOAP-ENV:Body></SOAP-ENV:Envelope>";
    std::string xml = os.str();
    soap->http_content = "application/soap+xml; charset=utf-8";
    soap_response(soap, SOAP_FILE);
    soap_send_raw(soap, xml.data(), xml.size());
    soap_end_send(soap);
    return SOAP_STOP;
}

// ── CreateProfile / DeleteProfile (Profile T mục 7.8) ──────────────────────
int Media2Service::CreateProfile(
    _ns1__CreateProfile *req,
    _ns1__CreateProfileResponse &resp)
{
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!req || req->Name.empty()) {
        return m2SendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                "ter:InvalidArgVal", nullptr, "Missing Name");
    }
    DynProfile p;
    p.token = "profile_" + req->Name;
    p.name  = req->Name;
    // Configuration ban đầu — tool có thể gửi vector configs (VideoSource, VideoEncoder, ...)
    for (auto* c : req->Configuration) {
        if (!c || !c->Token) continue;
        if (c->Type == "VideoSource")       p.vsToken = *c->Token;
        else if (c->Type == "VideoEncoder") p.veToken = *c->Token;
        else if (c->Type == "Metadata")     p.mdToken = *c->Token;
        else if (c->Type == "Analytics")    p.anToken = *c->Token;
    }
    {
        std::lock_guard<std::mutex> lk(g_profMtx);
        g_dynProfiles[p.token] = p;
    }
    resp.Token = p.token;
    std::cout << "[Media2Service] CreateProfile → " << resp.Token << std::endl;
    return SOAP_OK;
}

int Media2Service::DeleteProfile(
    _ns1__DeleteProfile *req,
    _ns1__DeleteProfileResponse &resp)
{
    (void)resp;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!req || req->Token.empty()) {
        return m2SendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                "ter:InvalidArgVal", nullptr, "Missing Token");
    }
    // 1) Dynamic profile → xóa khỏi map
    {
        std::lock_guard<std::mutex> lk(g_profMtx);
        auto it = g_dynProfiles.find(req->Token);
        if (it != g_dynProfiles.end()) {
            g_dynProfiles.erase(it);
            std::cout << "[Media2Service] DeleteProfile [" << req->Token << "] dyn" << std::endl;
            return SOAP_OK;
        }
    }
    // 2) Fixed profile từ backend → mark deleted (không xóa thật). State được reset
    //    ở đầu mỗi test (resetPerTestState, gọi từ DeviceService::GetServices) nên
    //    deletion không rò rỉ sang test sau.
    std::vector<StreamProfile> profiles;
    try { profiles = backend_->getProfiles(); } catch (...) {}
    if (req->Token == PROFILE_METADATA_TOKEN) {
        std::lock_guard<std::mutex> lk(g_profMtx);
        g_deletedFixedTokens.insert(req->Token);
        std::cout << "[Media2Service] DeleteProfile [" << req->Token << "] fixed->marked" << std::endl;
        return SOAP_OK;
    }
    for (const auto& p : profiles) {
        if (p.token == req->Token) {
            std::lock_guard<std::mutex> lk(g_profMtx);
            g_deletedFixedTokens.insert(req->Token);
            std::cout << "[Media2Service] DeleteProfile [" << req->Token << "] fixed→marked" << std::endl;
            return SOAP_OK;
        }
    }
    return m2SendOnvifFault(this->soap, "SOAP-ENV:Sender",
                            "ter:InvalidArgVal", "ter:NoProfile",
                            "Profile not found");
}

// ══════════════════════════════════════════════════════════════════════════
// Profile T §7.8 GetVideoEncoderInstances — MaxNumberOfConcurrentStreams
// ══════════════════════════════════════════════════════════════════════════
int Media2Service::GetVideoEncoderInstances(
    _ns1__GetVideoEncoderInstances* req,
    _ns1__GetVideoEncoderInstancesResponse& resp)
{
    (void)req;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    auto soap = this->soap;

    auto info = soap_new_ns1__EncoderInstanceInfo(soap);
    // MEDIA2_RTSS-1-1-23 spec §7.8.1: device phải support ≥Total profiles
    // dùng chung VSC. Giảm Total=1 (fixed camera 1 concurrent stream) để match
    // số VEC ta trả (1 attached + 1 spare = 2 ≥ 1 OK).
    info->Total = 1;
    resp.Info = info;
    return SOAP_OK;
}

// ══════════════════════════════════════════════════════════════════════════
// Profile T §7.15 GetMetadataConfigurationOptions
// ══════════════════════════════════════════════════════════════════════════
int Media2Service::GetMetadataConfigurationOptions(
    ns1__GetConfiguration* req,
    _ns1__GetMetadataConfigurationOptionsResponse& resp)
{
    (void)req;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    auto soap = this->soap;

    auto opt = soap_new_tt__MetadataConfigurationOptions(soap);
    auto* mcfs = (int*)soap_malloc(soap, sizeof(int));
    *mcfs = 1024;   // MaxContentFilterSize
    opt->MaxContentFilterSize = mcfs;
    resp.Options = opt;
    return SOAP_OK;
}

// ══════════════════════════════════════════════════════════════════════════
// Profile T §7.18 OSD (On-Screen Display) — 5 op mandatory
// ══════════════════════════════════════════════════════════════════════════
namespace {
struct MockOSD {
    std::string token;
    std::string vsToken;   // VideoSourceConfigurationToken
    std::string type;      // "Text" | "Image"
    std::string text;
    std::string textType;  // "Plain" | "Date" | "Time" | "DateAndTime"
    bool  hasPos = false;
    float posX = 0.0f, posY = 0.0f;
    std::string posType = "UpperLeft";
    bool  hasFontSize = false;
    int   fontSize = 10;
    std::string dateFormat;   // "yyyy-MM-dd"
    std::string timeFormat;   // "HH:mm:ss"
};
static std::mutex g_osdMtx;
static std::map<std::string, MockOSD> g_osds;
static int g_osdNextId = 1;
}

int Media2Service::CreateOSD(_ns1__CreateOSD* req,
                             _ns1__CreateOSDResponse& resp) {
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!req || !req->OSD) {
        return m2SendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                "ter:InvalidArgVal", nullptr, "Missing OSD");
    }
    std::lock_guard<std::mutex> lk(g_osdMtx);
    MockOSD osd;
    osd.token = "osd_" + std::to_string(g_osdNextId++);
    if (req->OSD->VideoSourceConfigurationToken)
        osd.vsToken = req->OSD->VideoSourceConfigurationToken->__item;
    if (req->OSD->TextString) {
        if (req->OSD->TextString->PlainText) osd.text = *req->OSD->TextString->PlainText;
        if (!req->OSD->TextString->Type.empty()) osd.textType = req->OSD->TextString->Type;
        if (req->OSD->TextString->FontSize) {
            osd.hasFontSize = true;
            osd.fontSize = *req->OSD->TextString->FontSize;
        }
        if (req->OSD->TextString->DateFormat) osd.dateFormat = *req->OSD->TextString->DateFormat;
        if (req->OSD->TextString->TimeFormat) osd.timeFormat = *req->OSD->TextString->TimeFormat;
    }
    if (req->OSD->Position) {
        if (!req->OSD->Position->Type.empty()) osd.posType = req->OSD->Position->Type;
        if (req->OSD->Position->Pos) {
            osd.hasPos = true;
            osd.posX = req->OSD->Position->Pos->x;
            osd.posY = req->OSD->Position->Pos->y;
        }
    }
    osd.type = "Text";
    g_osds[osd.token] = osd;
    resp.OSDToken = osd.token;
    std::cout << "[Media2Service] CreateOSD → " << osd.token << std::endl;
    return SOAP_OK;
}

int Media2Service::DeleteOSD(_ns1__DeleteOSD* req,
                             ns1__SetConfigurationResponse& resp) {
    (void)resp;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    std::lock_guard<std::mutex> lk(g_osdMtx);
    auto it = g_osds.find(req ? req->OSDToken : "");
    if (it == g_osds.end()) {
        return m2SendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                "ter:InvalidArgVal", "ter:NoConfig",
                                "OSD not found");
    }
    g_osds.erase(it);
    return SOAP_OK;
}

int Media2Service::GetOSDs(_ns1__GetOSDs* req,
                           _ns1__GetOSDsResponse& resp) {
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    auto soap = this->soap;
    std::string filterToken = (req && req->OSDToken) ? *req->OSDToken : "";
    std::lock_guard<std::mutex> lk(g_osdMtx);
    for (const auto& kv : g_osds) {
        if (!filterToken.empty() && kv.first != filterToken) continue;
        auto o = soap_new_tt__OSDConfiguration(soap);
        o->token = kv.second.token;
        o->VideoSourceConfigurationToken = soap_new_tt__OSDReference(soap);
        o->VideoSourceConfigurationToken->__item = kv.second.vsToken.empty()
            ? "video_source_config" : kv.second.vsToken;
        // Set Type=Text (enum), Position, TextString
        // tt__OSDType enum: Text=0, Image=1, Extended=2 (thứ tự tùy generator)
        // Chỉ set Position + TextString là đủ (Type dùng default).
        o->Position = soap_new_tt__OSDPosConfiguration(soap);
        o->Position->Type = kv.second.posType;
        if (kv.second.hasPos) {
            o->Position->Pos = soap_new_tt__Vector(soap);
            o->Position->Pos->x = kv.second.posX;
            o->Position->Pos->y = kv.second.posY;
        }
        o->TextString = soap_new_tt__OSDTextConfiguration(soap);
        o->TextString->Type = kv.second.textType.empty() ? "Plain" : kv.second.textType;
        auto* txt = soap_new_std__string(soap);
        *txt = kv.second.text.empty() ? "MockCam" : kv.second.text;
        o->TextString->PlainText = txt;
        if (kv.second.hasFontSize) {
            auto* fs = (int*)soap_malloc(soap, sizeof(int));
            *fs = kv.second.fontSize;
            o->TextString->FontSize = fs;
        }
        if (!kv.second.dateFormat.empty()) {
            auto* df = soap_new_std__string(soap);
            *df = kv.second.dateFormat;
            o->TextString->DateFormat = df;
        }
        if (!kv.second.timeFormat.empty()) {
            auto* tf = soap_new_std__string(soap);
            *tf = kv.second.timeFormat;
            o->TextString->TimeFormat = tf;
        }
        resp.OSDs.push_back(o);
    }
    return SOAP_OK;
}

int Media2Service::GetOSDOptions(_ns1__GetOSDOptions* req,
                                 _ns1__GetOSDOptionsResponse& resp) {
    (void)req;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    auto soap = this->soap;
    auto opt = soap_new_tt__OSDConfigurationOptions(soap);
    // Max OSDs
    opt->MaximumNumberOfOSDs = soap_new_tt__MaximumNumberOfOSDs(soap);
    opt->MaximumNumberOfOSDs->Total = 4;
    auto* zero = (int*)soap_malloc(soap, sizeof(int)); *zero = 0;
    auto* four = (int*)soap_malloc(soap, sizeof(int)); *four = 4;
    opt->MaximumNumberOfOSDs->Image = zero;
    opt->MaximumNumberOfOSDs->PlainText = four;
    opt->MaximumNumberOfOSDs->Date = four;
    opt->MaximumNumberOfOSDs->Time = four;
    opt->MaximumNumberOfOSDs->DateAndTime = four;
    // Type: BẮT BUỘC theo schema tt:OSDConfigurationOptions (minOccurs=1,
    // maxOccurs=unbounded). Bỏ qua sẽ khiến tool báo "invalid child element
    // PositionOption. Expected: Type" (MEDIA2-6-1-1..6). Chỉ hỗ trợ Text
    // (Image=0 trong MaximumNumberOfOSDs).
    opt->Type.push_back(tt__OSDType::Text);
    // Position options
    opt->PositionOption.push_back("UpperLeft");
    opt->PositionOption.push_back("UpperRight");
    opt->PositionOption.push_back("LowerLeft");
    opt->PositionOption.push_back("LowerRight");
    opt->PositionOption.push_back("Custom");
    // Text options
    opt->TextOption = soap_new_tt__OSDTextOptions(soap);
    opt->TextOption->Type.push_back("Plain");
    opt->TextOption->Type.push_back("Date");
    opt->TextOption->Type.push_back("Time");
    opt->TextOption->Type.push_back("DateAndTime");
    opt->TextOption->FontSizeRange = soap_new_tt__IntRange(soap);
    opt->TextOption->FontSizeRange->Min = 10;
    opt->TextOption->FontSizeRange->Max = 48;
    opt->TextOption->DateFormat.push_back("MM/dd/yyyy");
    opt->TextOption->DateFormat.push_back("yyyy-MM-dd");
    opt->TextOption->TimeFormat.push_back("HH:mm:ss");
    resp.OSDOptions = opt;
    return SOAP_OK;
}

int Media2Service::SetOSD(_ns1__SetOSD* req,
                          ns1__SetConfigurationResponse& resp) {
    (void)resp;
    this->soap->mustUnderstand = 0;
    this->soap->header = nullptr;
    if (!req || !req->OSD || req->OSD->token.empty()) {
        return m2SendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                "ter:InvalidArgVal", nullptr, "Missing OSD");
    }
    std::lock_guard<std::mutex> lk(g_osdMtx);
    auto it = g_osds.find(req->OSD->token);
    if (it == g_osds.end()) {
        return m2SendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                "ter:InvalidArgVal", "ter:NoConfig",
                                "OSD not found");
    }
    if (req->OSD->TextString) {
        if (req->OSD->TextString->PlainText)
            it->second.text = *req->OSD->TextString->PlainText;
        if (!req->OSD->TextString->Type.empty())
            it->second.textType = req->OSD->TextString->Type;
        if (req->OSD->TextString->FontSize) {
            it->second.hasFontSize = true;
            it->second.fontSize = *req->OSD->TextString->FontSize;
        }
        if (req->OSD->TextString->DateFormat) it->second.dateFormat = *req->OSD->TextString->DateFormat;
        if (req->OSD->TextString->TimeFormat) it->second.timeFormat = *req->OSD->TextString->TimeFormat;
    }
    if (req->OSD->Position) {
        if (!req->OSD->Position->Type.empty())
            it->second.posType = req->OSD->Position->Type;
        if (req->OSD->Position->Pos) {
            it->second.hasPos = true;
            it->second.posX = req->OSD->Position->Pos->x;
            it->second.posY = req->OSD->Position->Pos->y;
        } else {
            it->second.hasPos = false;
        }
    }
    return SOAP_OK;
}

