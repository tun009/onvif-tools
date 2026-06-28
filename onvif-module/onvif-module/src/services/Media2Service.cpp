#include "services/Media2Service.h"
#include "auth/WsSecurityHandler.h"
#include <iostream>
#include <string>

Media2Service::Media2Service(struct soap* soap,
                             const ServiceConfig& cfg,
                             std::shared_ptr<ICameraBackend> backend)
    : Media2BindingService(soap), cfg_(cfg), backend_(std::move(backend)) {}

Media2BindingService* Media2Service::copy() {
    return new Media2Service(this->soap, cfg_, backend_);
}

bool Media2Service::validateAuth() {
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
        profile->Configurations = soap_new_ns1__ConfigurationSet(soap);

        if (profile->Configurations) {
            bool wantVideoSource = true;
            bool wantVideoEncoder = true;

            if (req && !req->Type.empty()) {
                wantVideoSource = false;
                wantVideoEncoder = false;
                for (const auto& t : req->Type) {
                    if (t == "VideoSource") wantVideoSource = true;
                    else if (t == "VideoEncoder") wantVideoEncoder = true;
                }
            }

            if (wantVideoSource) {
                auto vsc = soap_new_tt__VideoSourceConfiguration(soap);
                if (vsc) {
                    vsc->token = "video_source_config";
                    vsc->Name = "VideoSourceConfig";
                    vsc->SourceToken = p.sourceToken.empty() ? "video_source_token" : p.sourceToken;
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
    if (req) profileToken = req->ProfileToken;

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

    resp.Uri = uri;
    std::cout << "[Media2Service] GetStreamUri [" << profileToken << "] → " << uri << std::endl;
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
        vsc->SourceToken = "video_source_token";

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

    auto enc = soap_new_tt__VideoEncoder2Configuration(soap);
    if (enc) {
        enc->token = "video_encoder_config";
        enc->Name = "VideoEncoderConfig";
        enc->Encoding = "H264";
        enc->Quality = 50.0f;

        enc->Resolution = soap_new_tt__VideoResolution2(soap);
        if (enc->Resolution) {
            enc->Resolution->Width = 1920;
            enc->Resolution->Height = 1080;
        }

        enc->RateControl = soap_new_tt__VideoRateControl2(soap);
        if (enc->RateControl) {
            enc->RateControl->FrameRateLimit = 30.0f;
            enc->RateControl->BitrateLimit = 4096;
        }
        resp.Configurations.push_back(enc);
    }

    std::cout << "[Media2Service] GetVideoEncoderConfigurations → returned 1 configuration" << std::endl;
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

    auto opt = soap_new_tt__VideoSourceConfigurationOptions(soap);
    if (opt) {
        opt->VideoSourceTokensAvailable.push_back("video_source_token");
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

    auto opt = soap_new_tt__VideoEncoder2ConfigurationOptions(soap);
    if (opt) {
        opt->Encoding = "H264";
        
        opt->QualityRange = soap_new_tt__FloatRange(soap);
        if (opt->QualityRange) {
            opt->QualityRange->Min = 0.0f;
            opt->QualityRange->Max = 100.0f;
        }

        auto res = soap_new_tt__VideoResolution2(soap);
        if (res) {
            res->Width = 1920;
            res->Height = 1080;
            opt->ResolutionsAvailable.push_back(res);
        }

        opt->BitrateRange = soap_new_tt__IntRange(soap);
        if (opt->BitrateRange) {
            opt->BitrateRange->Min = 64;
            opt->BitrateRange->Max = 8192;
        }

        resp.Options.push_back(opt);
    }

    std::cout << "[Media2Service] GetVideoEncoderConfigurationOptions called" << std::endl;
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
    std::cout << "[Media2Service] SetVideoEncoderConfiguration called" << std::endl;
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



