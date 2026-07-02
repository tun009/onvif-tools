#include "services/Media2Service.h"
#include "auth/WsSecurityHandler.h"
#include <iostream>
#include <string>
#include <sstream>
#include <cstring>

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

    for (const auto& p : profiles) {
        if (!filterToken.empty() && p.token != filterToken) continue;

        auto profile = soap_new_ns1__MediaProfile(soap);
        if (!profile) continue;
        profile->token = p.token;
        profile->Name  = p.name;
        profile->fixed          = nullptr;

        // ONVIF Media2 spec: Type empty → KHÔNG trả Configurations (element phải
        // vắng mặt, không phải empty element — test MEDIA2-1-1-6). Type "All"
        // hoặc cụ thể → trả các Configurations tương ứng.
        bool wantVideoSource = false;
        bool wantVideoEncoder = false;
        if (req) {
            for (const auto& t : req->Type) {
                if (t == "All") {
                    wantVideoSource = true;
                    wantVideoEncoder = true;
                } else if (t == "VideoSource") wantVideoSource = true;
                else if (t == "VideoEncoder") wantVideoEncoder = true;
            }
        }

        if (wantVideoSource || wantVideoEncoder) {
            profile->Configurations = soap_new_ns1__ConfigurationSet(soap);
        } else {
            profile->Configurations = nullptr;
        }

        if (profile->Configurations) {

            if (wantVideoSource) {
                auto vsc = soap_new_tt__VideoSourceConfiguration(soap);
                if (vsc) {
                    vsc->token = "video_source_config";
                    vsc->Name = "VideoSourceConfig";
                    vsc->SourceToken = p.sourceToken.empty() ? "src_main" : p.sourceToken;
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

            if (wantVideoEncoder) {
                auto vec = soap_new_tt__VideoEncoder2Configuration(soap);
                if (vec) {
                    vec->token = p.token == "profile_main" ? "video_encoder_config" : ("video_encoder_config_" + p.token);
                    vec->Name = p.token == "profile_main" ? "VideoEncoderConfig" : ("VideoEncoderConfig_" + p.token);
                    vec->Encoding = p.videoConfig.codec == Codec::H265 ? "H265" : "H264";
                    vec->Quality = 50.0f;
                    
                    vec->Resolution = soap_new_tt__VideoResolution2(soap);
                    if (vec->Resolution) {
                        vec->Resolution->Width = p.videoConfig.resolution.width;
                        vec->Resolution->Height = p.videoConfig.resolution.height;
                    }

                    vec->RateControl = soap_new_tt__VideoRateControl2(soap);
                    if (vec->RateControl) {
                        vec->RateControl->FrameRateLimit = p.videoConfig.framerate;
                        vec->RateControl->BitrateLimit = p.videoConfig.bitrate;
                    }
                    profile->Configurations->VideoEncoder = vec;
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

    StreamUri u;
    try {
        u = backend_->getStreamUri(profileToken, StreamProtocol::RTSP);
    } catch (const std::exception& e) {
        std::cerr << "[Media2Service] GetStreamUri error: " << e.what() << std::endl;
        return soap_receiver_fault(this->soap, "Internal error", nullptr);
    }

    if (u.uri.empty()) {
        return soap_sender_fault(this->soap, "Invalid profile token", nullptr);
    }

    std::string uri = u.uri;
    size_t pos = uri.find("127.0.0.1");
    if (pos != std::string::npos) uri.replace(pos, 9, cfg_.deviceIp);

    // Hỗ trợ RTSP over HTTP tunneling theo yêu cầu của Test Tool
    if (protocol == "RtspOverHttp" || protocol == "RtspOverHttps") {
        std::string rtspPortStr = ":" + std::to_string(cfg_.rtspPort);
        size_t portPos = uri.find(rtspPortStr);
        if (portPos != std::string::npos) {
            std::string httpPortStr = ":" + std::to_string(cfg_.httpPort);
            uri.replace(portPos, rtspPortStr.length(), httpPortStr);
        }

        // Thay đổi scheme từ rtsp:// thành http:// hoặc https:// tương ứng
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

    auto vsc = soap_new_tt__VideoSourceConfiguration(soap);
    if (vsc) {
        vsc->token = "video_source_config";
        vsc->Name = "VideoSourceConfig";
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

    for (const auto& p : profiles) {
        // Filter theo ProfileToken (nếu có)
        if (!filterProfileToken.empty() && p.token != filterProfileToken) continue;

        std::string cfgToken = (p.token == "profile_main")
                             ? "video_encoder_config"
                             : "video_encoder_config_" + p.token;

        // Filter theo ConfigurationToken (nếu có) — MEDIA2-2-3-1 mong đúng 1
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

    std::cout << "[Media2Service] GetVideoEncoderConfigurations → returned " << resp.Configurations.size() << " configurations" << std::endl;
    return SOAP_OK;
}

// ── AddConfiguration ─────────────────────────────────────────────────────────
int Media2Service::AddConfiguration(
    _ns1__AddConfiguration *req,
    _ns1__AddConfigurationResponse &resp)
{
    this->soap->mustUnderstand = 0;
    if (!validateAuth()) {
        return soap_sender_fault_subcode(this->soap, "ter:NotAuthorized", "Sender", "Not Authorized");
    }
    this->soap->header = nullptr;
    std::cout << "[Media2Service] AddConfiguration called" << std::endl;
    return SOAP_OK;
}

// ── RemoveConfiguration ──────────────────────────────────────────────────────
int Media2Service::RemoveConfiguration(
    _ns1__RemoveConfiguration *req,
    _ns1__RemoveConfigurationResponse &resp)
{
    this->soap->mustUnderstand = 0;
    if (!validateAuth()) {
        return soap_sender_fault_subcode(this->soap, "ter:NotAuthorized", "Sender", "Not Authorized");
    }
    this->soap->header = nullptr;
    std::cout << "[Media2Service] RemoveConfiguration called" << std::endl;
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
    // đã tạo trong GetVideoEncoderConfigurations. Khác → fault ter:NoConfig.
    auto isKnownEncoderConfig = [](const std::string& t){
        return t == "video_encoder_config" ||
               t == "video_encoder_config_profile_sub1" ||
               t == "video_encoder_config_profile_sub2";
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
        std::vector<std::pair<int, int>> resolutions = {
            {3840, 2160},
            {1920, 1080},
            {640, 480}
        };
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

    // Luôn trả về cả tùy chọn H264 và H265 để Test Tool có thể chọn lựa linh hoạt
    auto optH264 = createOption("H264");
    if (optH264) resp.Options.push_back(optH264);

    auto optH265 = createOption("H265");
    if (optH265) resp.Options.push_back(optH265);

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
    caps->OSD = new bool(false);

    // Profile T requires RTP multicast/unicast/RTSP
    caps->ProfileCapabilities = soap_new_ns1__ProfileCapabilities(soap);
    if (caps->ProfileCapabilities) {
        caps->ProfileCapabilities->MaximumNumberOfProfiles = new int(3);
        // ConfigurationsSupported: list các config type device hỗ trợ.
        // Tối thiểu Profile T: VideoSource + VideoEncoder.
        caps->ProfileCapabilities->ConfigurationsSupported =
            soap_new_std__string(soap);
        *caps->ProfileCapabilities->ConfigurationsSupported =
            "VideoSource VideoEncoder";
    }

    // Streaming capabilities
    caps->StreamingCapabilities = soap_new_ns1__StreamingCapabilities(soap);
    if (caps->StreamingCapabilities) {
        // RTP over RTSP/TCP - supported
        auto rtpOverRtsp = new bool(true);
        caps->StreamingCapabilities->RTP_USCORERTSP_USCORETCP = rtpOverRtsp;

        // RTP multicast - not supported (requires multicast network setup)
        auto rtpMulticast = new bool(false);
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
    // Mock: sinh token từ Name (không thực tạo profile trong backend).
    resp.Token = "profile_" + req->Name;
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
    // Không xóa profile fixed từ backend — chỉ ack nếu token khớp danh sách.
    std::vector<StreamProfile> profiles;
    try { profiles = backend_->getProfiles(); } catch (...) {}
    bool found = false;
    for (const auto& p : profiles) {
        if (p.token == req->Token) { found = true; break; }
    }
    if (!found) {
        return m2SendOnvifFault(this->soap, "SOAP-ENV:Sender",
                                "ter:InvalidArgVal", "ter:NoProfile",
                                "Profile not found");
    }
    // Fixed profiles không xóa thực — trả OK để tool không dừng flow test.
    std::cout << "[Media2Service] DeleteProfile [" << req->Token
              << "] ack (fixed, no-op)" << std::endl;
    return SOAP_OK;
}


