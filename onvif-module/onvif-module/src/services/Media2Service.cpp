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

// ── Authentication check ─────────────────────────────────────────────────────
bool Media2Service::validateAuth() {
    WsSecurityHandler handler(cfg_.username, cfg_.password);
    return handler.validate(this->soap);
}

// ── Helpers: map internal types to gSOAP generated types ────────────────────

static std::string codecName(Codec c) {
    return (c == Codec::H265) ? "H265" : "H264";
}

// ── GetProfiles ──────────────────────────────────────────────────────────────
// Per ONVIF spec, GetProfiles does NOT require authentication.
int Media2Service::GetProfiles(
    _tr2__GetProfiles *req,
    _tr2__GetProfilesResponse &resp)
{
    auto soap = this->soap;
    std::string filterToken;
    if (req && req->Token) filterToken = *req->Token;

    std::vector<StreamProfile> profiles;
    try {
        profiles = backend_->getProfiles();
    } catch (const std::exception& e) {
        std::cerr << "[Media2Service] GetProfiles backend error: " << e.what() << std::endl;
        return soap_receiver_fault(soap, "Internal error", nullptr);
    }

    for (const auto& p : profiles) {
        // Filter by token if requested
        if (!filterToken.empty() && p.token != filterToken) continue;

        auto profile = soap_new_tr2__MediaProfile(soap);
        profile->token = p.token;
        profile->Name  = soap_new_std__string(soap);
        *profile->Name = p.name;
        profile->fixed = soap_new_bool(soap);
        *profile->fixed = false;

        // ── VideoSourceConfiguration ────────────────────────────────────────
        auto vsc = soap_new_tt__VideoSourceConfiguration(soap);
        vsc->token       = p.sourceToken;
        vsc->Name        = p.sourceToken;
        vsc->UseCount    = 1;
        vsc->SourceToken = p.sourceToken;
        // Bounds (full sensor area by default)
        vsc->Bounds = soap_new_tt__IntRectangle(soap);
        vsc->Bounds->x      = 0;
        vsc->Bounds->y      = 0;
        vsc->Bounds->width  = p.videoConfig.resolution.width;
        vsc->Bounds->height = p.videoConfig.resolution.height;
        profile->Configurations = soap_new_tr2__ConfigurationSet(soap);
        profile->Configurations->VideoSource = vsc;

        // ── VideoEncoderConfiguration ───────────────────────────────────────
        auto vec = soap_new_tt__VideoEncoder2Configuration(soap);
        vec->token    = p.token + "_enc";
        vec->Name     = p.name + " Encoder";
        vec->UseCount = 1;
        vec->Encoding = codecName(p.videoConfig.codec);

        vec->Resolution = soap_new_tt__VideoResolution2(soap);
        vec->Resolution->Width  = p.videoConfig.resolution.width;
        vec->Resolution->Height = p.videoConfig.resolution.height;

        vec->FrameRate = soap_new_xsd__decimal(soap);
        *vec->FrameRate = (double)p.videoConfig.framerate;

        vec->RateControl = soap_new_tt__VideoRateControl2(soap);
        vec->RateControl->FrameRateLimit  = (double)p.videoConfig.framerate;
        vec->RateControl->BitrateLimit    = p.videoConfig.bitrate;
        vec->RateControl->GuaranteedFrameRate = false;

        // H264 / H265 configuration
        vec->H264 = nullptr;
        vec->MPEG4 = nullptr;

        profile->Configurations->VideoEncoder = vec;

        resp.Profiles.push_back(profile);
    }

    std::cout << "[Media2Service] GetProfiles → " << resp.Profiles.size() << " profiles" << std::endl;
    return SOAP_OK;
}

// ── GetStreamUri ─────────────────────────────────────────────────────────────
int Media2Service::GetStreamUri(
    _tr2__GetStreamUri *req,
    _tr2__GetStreamUriResponse &resp)
{
    if (!validateAuth()) {
        return soap_sender_fault_subcode(this->soap, "ter:NotAuthorized", "Sender", "Not Authorized");
    }

    std::string profileToken;
    if (req && !req->ProfileToken.empty()) profileToken = req->ProfileToken;

    StreamUri u;
    try {
        u = backend_->getStreamUri(profileToken, StreamProtocol::RTSP);
    } catch (const std::exception& e) {
        std::cerr << "[Media2Service] GetStreamUri backend error: " << e.what() << std::endl;
        return soap_receiver_fault(this->soap, "Internal error", nullptr);
    }

    if (u.uri.empty()) {
        return soap_sender_fault(this->soap, "Invalid profile token", nullptr);
    }

    // Replace 127.0.0.1 with actual device IP so external clients can connect
    std::string uri = u.uri;
    const std::string loopback = "127.0.0.1";
    size_t pos = uri.find(loopback);
    if (pos != std::string::npos) {
        uri.replace(pos, loopback.size(), cfg_.deviceIp);
    }

    resp.Uri = uri;
    std::cout << "[Media2Service] GetStreamUri [" << profileToken << "] → " << uri << std::endl;
    return SOAP_OK;
}

// ── GetSnapshotUri ───────────────────────────────────────────────────────────
int Media2Service::GetSnapshotUri(
    _tr2__GetSnapshotUri *req,
    _tr2__GetSnapshotUriResponse &resp)
{
    if (!validateAuth()) {
        return soap_sender_fault_subcode(this->soap, "ter:NotAuthorized", "Sender", "Not Authorized");
    }

    std::string profileToken;
    if (req && !req->ProfileToken.empty()) profileToken = req->ProfileToken;

    SnapshotUri u;
    try {
        u = backend_->getSnapshotUri(profileToken);
    } catch (const std::exception& e) {
        std::cerr << "[Media2Service] GetSnapshotUri backend error: " << e.what() << std::endl;
        // Fallback to a simple HTTP snapshot URL
        u.uri = "http://" + cfg_.deviceIp + ":" + std::to_string(cfg_.httpPort)
                + "/snapshot?token=" + profileToken;
    }

    if (u.uri.empty()) {
        u.uri = "http://" + cfg_.deviceIp + ":" + std::to_string(cfg_.httpPort)
                + "/snapshot?token=" + profileToken;
    }

    // Replace 127.0.0.1 with actual device IP
    const std::string loopback = "127.0.0.1";
    size_t pos = u.uri.find(loopback);
    if (pos != std::string::npos) {
        u.uri.replace(pos, loopback.size(), cfg_.deviceIp);
    }

    resp.Uri = u.uri;
    std::cout << "[Media2Service] GetSnapshotUri [" << profileToken << "] → " << u.uri << std::endl;
    return SOAP_OK;
}

// ── GetVideoEncoderConfigurations ────────────────────────────────────────────
int Media2Service::GetVideoEncoderConfigurations(
    _tr2__GetConfiguration *req,
    _tr2__GetVideoEncoderConfigurationsResponse &resp)
{
    if (!validateAuth()) {
        return soap_sender_fault_subcode(this->soap, "ter:NotAuthorized", "Sender", "Not Authorized");
    }

    auto soap = this->soap;
    std::string filterToken;
    if (req && req->ConfigurationToken) filterToken = *req->ConfigurationToken;

    std::vector<StreamProfile> profiles;
    try {
        profiles = backend_->getProfiles();
    } catch (const std::exception& e) {
        std::cerr << "[Media2Service] GetVideoEncoderConfigurations error: " << e.what() << std::endl;
        return soap_receiver_fault(soap, "Internal error", nullptr);
    }

    for (const auto& p : profiles) {
        std::string encToken = p.token + "_enc";
        if (!filterToken.empty() && encToken != filterToken) continue;

        auto vec = soap_new_tt__VideoEncoder2Configuration(soap);
        vec->token    = encToken;
        vec->Name     = p.name + " Encoder";
        vec->UseCount = 1;
        vec->Encoding = codecName(p.videoConfig.codec);

        vec->Resolution = soap_new_tt__VideoResolution2(soap);
        vec->Resolution->Width  = p.videoConfig.resolution.width;
        vec->Resolution->Height = p.videoConfig.resolution.height;

        vec->FrameRate = soap_new_xsd__decimal(soap);
        *vec->FrameRate = (double)p.videoConfig.framerate;

        vec->RateControl = soap_new_tt__VideoRateControl2(soap);
        vec->RateControl->FrameRateLimit  = (double)p.videoConfig.framerate;
        vec->RateControl->BitrateLimit    = p.videoConfig.bitrate;
        vec->RateControl->GuaranteedFrameRate = false;

        resp.Configurations.push_back(vec);
    }

    std::cout << "[Media2Service] GetVideoEncoderConfigurations → "
              << resp.Configurations.size() << " configs" << std::endl;
    return SOAP_OK;
}

// ── SetVideoEncoderConfiguration ─────────────────────────────────────────────
int Media2Service::SetVideoEncoderConfiguration(
    _tr2__SetVideoEncoderConfiguration *req,
    _tr2__SetVideoEncoderConfigurationResponse &/*resp*/)
{
    if (!validateAuth()) {
        return soap_sender_fault_subcode(this->soap, "ter:NotAuthorized", "Sender", "Not Authorized");
    }

    if (!req || !req->Configuration) {
        return soap_sender_fault(this->soap, "Missing configuration", nullptr);
    }

    auto* cfg = req->Configuration;
    VideoEncoderConfig vCfg;
    vCfg.codec = (cfg->Encoding == "H265") ? Codec::H265 : Codec::H264;

    if (cfg->Resolution) {
        vCfg.resolution.width  = cfg->Resolution->Width;
        vCfg.resolution.height = cfg->Resolution->Height;
    }
    if (cfg->FrameRate) {
        vCfg.framerate = static_cast<int>(*cfg->FrameRate);
    }
    if (cfg->RateControl) {
        vCfg.bitrate = cfg->RateControl->BitrateLimit;
    }

    // Token is "profile_main_enc" → strip "_enc" to get profile token
    std::string token = cfg->token;
    const std::string suffix = "_enc";
    if (token.size() > suffix.size() &&
        token.compare(token.size() - suffix.size(), suffix.size(), suffix) == 0) {
        token = token.substr(0, token.size() - suffix.size());
    }

    bool ok = false;
    try {
        ok = backend_->setVideoEncoderConfig(token, vCfg);
    } catch (const std::exception& e) {
        std::cerr << "[Media2Service] SetVideoEncoderConfiguration error: " << e.what() << std::endl;
        return soap_receiver_fault(this->soap, "Internal error", nullptr);
    }

    if (!ok) {
        return soap_sender_fault(this->soap, "Invalid profile token", nullptr);
    }

    std::cout << "[Media2Service] SetVideoEncoderConfiguration [" << token << "] OK" << std::endl;
    return SOAP_OK;
}
