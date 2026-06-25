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
        profile->Configurations = nullptr;

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
    if (!validateAuth()) {
        return soap_sender_fault_subcode(this->soap, "ter:NotAuthorized", "Sender", "Not Authorized");
    }

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
    if (!validateAuth()) {
        return soap_sender_fault_subcode(this->soap, "ter:NotAuthorized", "Sender", "Not Authorized");
    }

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
