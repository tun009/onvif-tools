#include "mock/IpcServer.h"
#include "nlohmann/json.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <sstream>

using json = nlohmann::json;

// ── Helpers ───────────────────────────────────────────────────────────────

bool IpcServer::sendMessage(int fd, const ipc::Message& msg) {
    ssize_t n = ::send(fd, &msg.header, sizeof(msg.header), MSG_NOSIGNAL);
    if (n != (ssize_t)sizeof(msg.header)) return false;
    if (!msg.payload.empty()) {
        n = ::send(fd, msg.payload.data(), msg.payload.size(), MSG_NOSIGNAL);
        if (n != (ssize_t)msg.payload.size()) return false;
    }
    return true;
}

bool IpcServer::recvMessage(int fd, ipc::Message& msg) {
    ssize_t n = ::recv(fd, &msg.header, sizeof(msg.header), MSG_WAITALL);
    if (n != (ssize_t)sizeof(msg.header)) return false;
    if (msg.header.magic != ipc::MAGIC) return false;
    if (msg.header.payloadLen > 0) {
        msg.payload.resize(msg.header.payloadLen);
        n = ::recv(fd, msg.payload.data(), msg.header.payloadLen, MSG_WAITALL);
        if (n != (ssize_t)msg.header.payloadLen) return false;
    }
    return true;
}

ipc::Message IpcServer::makeOk(uint32_t reqId, const std::string& body) {
    ipc::Message m;
    m.header.magic      = ipc::MAGIC;
    m.header.version    = ipc::VERSION;
    m.header.msgType    = (uint8_t)ipc::MsgType::RESP_OK;
    m.header.flags      = 0;
    m.header.requestId  = reqId;
    m.header.payloadLen = (uint32_t)body.size();
    m.payload.assign(body.begin(), body.end());
    return m;
}

ipc::Message IpcServer::makeErr(uint32_t reqId, const std::string& msg) {
    json j = {{"error", msg}};
    std::string body = j.dump();
    ipc::Message m;
    m.header.magic      = ipc::MAGIC;
    m.header.version    = ipc::VERSION;
    m.header.msgType    = (uint8_t)ipc::MsgType::RESP_ERROR;
    m.header.flags      = 0;
    m.header.requestId  = reqId;
    m.header.payloadLen = (uint32_t)body.size();
    m.payload.assign(body.begin(), body.end());
    return m;
}

// ── Constructor / Lifecycle ───────────────────────────────────────────────

IpcServer::IpcServer(const std::string& ctrl,
                     const std::string& evt,
                     std::shared_ptr<ICameraBackend> backend)
    : ctrlSockPath_(ctrl), evtSockPath_(evt), backend_(std::move(backend))
{}

IpcServer::~IpcServer() { stop(); }

bool IpcServer::start() {
    // ── Control socket ────────────────────────────────────────────
    ::unlink(ctrlSockPath_.c_str());
    ctrlFd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (ctrlFd_ < 0) { perror("ctrl socket"); return false; }

    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, ctrlSockPath_.c_str(),
            sizeof(addr.sun_path) - 1);

    if (::bind(ctrlFd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("ctrl bind"); return false;
    }
    ::listen(ctrlFd_, 5);

    // ── Event socket ──────────────────────────────────────────────
    ::unlink(evtSockPath_.c_str());
    evtFd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (evtFd_ < 0) { perror("evt socket"); return false; }

    sockaddr_un evtAddr{};
    evtAddr.sun_family = AF_UNIX;
    strncpy(evtAddr.sun_path, evtSockPath_.c_str(),
            sizeof(evtAddr.sun_path) - 1);

    if (::bind(evtFd_, (sockaddr*)&evtAddr, sizeof(evtAddr)) < 0) {
        perror("evt bind"); return false;
    }
    ::listen(evtFd_, 2);

    running_ = true;
    acceptThread_    = std::thread(&IpcServer::acceptLoop, this);
    evtAcceptThread_ = std::thread(&IpcServer::eventAcceptLoop, this);

    printf("[IpcServer] listening ctrl=%s evt=%s\n",
           ctrlSockPath_.c_str(), evtSockPath_.c_str());
    return true;
}

void IpcServer::stop() {
    running_ = false;
    if (ctrlFd_ >= 0) { ::close(ctrlFd_);  ctrlFd_  = -1; }
    if (evtFd_  >= 0) { ::close(evtFd_);   evtFd_   = -1; }
    {
        std::lock_guard<std::mutex> lk(evtClientMutex_);
        if (evtClient_ >= 0) { ::close(evtClient_); evtClient_ = -1; }
    }
    if (acceptThread_.joinable())    acceptThread_.join();
    if (evtAcceptThread_.joinable()) evtAcceptThread_.join();
    ::unlink(ctrlSockPath_.c_str());
    ::unlink(evtSockPath_.c_str());
}

void IpcServer::acceptLoop() {
    while (running_) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(ctrlFd_, &fds);
        timeval tv{1, 0};
        if (select(ctrlFd_ + 1, &fds, nullptr, nullptr, &tv) <= 0)
            continue;

        sockaddr_un caddr{};
        socklen_t   clen = sizeof(caddr);
        int cfd = ::accept(ctrlFd_, (sockaddr*)&caddr, &clen);
        if (cfd < 0) continue;

        std::thread([this, cfd]() { handleClient(cfd); }).detach();
    }
}

void IpcServer::eventAcceptLoop() {
    while (running_) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(evtFd_, &fds);
        timeval tv{1, 0};
        if (select(evtFd_ + 1, &fds, nullptr, nullptr, &tv) <= 0)
            continue;

        sockaddr_un caddr{};
        socklen_t   clen = sizeof(caddr);
        int cfd = ::accept(evtFd_, (sockaddr*)&caddr, &clen);
        if (cfd < 0) continue;

        std::lock_guard<std::mutex> lk(evtClientMutex_);
        if (evtClient_ >= 0) ::close(evtClient_);
        evtClient_ = cfd;
        printf("[IpcServer] event client connected\n");
    }
}

void IpcServer::handleClient(int clientFd) {
    while (running_) {
        ipc::Message req;
        if (!recvMessage(clientFd, req)) break;

        ipc::Message resp = dispatch(req);
        if (!sendMessage(clientFd, resp)) break;
    }
    ::close(clientFd);
}

void IpcServer::pushEvent(ipc::MsgType type, const std::string& payload) {
    std::lock_guard<std::mutex> lk(evtClientMutex_);
    if (evtClient_ < 0) return;

    ipc::Message m;
    m.header.magic      = ipc::MAGIC;
    m.header.version    = ipc::VERSION;
    m.header.msgType    = (uint8_t)type;
    m.header.flags      = 0;
    m.header.requestId  = 0;
    m.header.payloadLen = (uint32_t)payload.size();
    m.payload.assign(payload.begin(), payload.end());

    if (!sendMessage(evtClient_, m)) {
        ::close(evtClient_);
        evtClient_ = -1;
    }
}

// ── Dispatch ──────────────────────────────────────────────────────────────

ipc::Message IpcServer::dispatch(const ipc::Message& req) {
    std::string body(req.payload.begin(), req.payload.end());
    uint32_t    rid = req.header.requestId;

    switch ((ipc::MsgType)req.header.msgType) {
        case ipc::MsgType::REQ_GET_DEVICE_INFO:      return handleGetDeviceInfo(body);
        case ipc::MsgType::REQ_GET_NETWORK_CONFIG:   return handleGetNetworkConfig(body);
        case ipc::MsgType::REQ_GET_DATETIME:         return handleGetDatetime(body);
        case ipc::MsgType::REQ_SET_DATETIME:         return handleSetDatetime(body);
        case ipc::MsgType::REQ_REBOOT:               return handleReboot(body);
        case ipc::MsgType::REQ_GET_PROFILES:         return handleGetProfiles(body);
        case ipc::MsgType::REQ_GET_STREAM_URI:       return handleGetStreamUri(body);
        case ipc::MsgType::REQ_SET_VIDEO_CONFIG:     return handleSetVideoConfig(body);
        case ipc::MsgType::REQ_GET_SNAPSHOT_URI:     return handleGetSnapshotUri(body);
        case ipc::MsgType::REQ_PTZ_ABSOLUTE_MOVE:    return handlePtzAbsoluteMove(body);
        case ipc::MsgType::REQ_PTZ_RELATIVE_MOVE:    return handlePtzRelativeMove(body);
        case ipc::MsgType::REQ_PTZ_CONTINUOUS_MOVE:  return handlePtzContinuousMove(body);
        case ipc::MsgType::REQ_PTZ_STOP:             return handlePtzStop(body);
        case ipc::MsgType::REQ_PTZ_GET_STATUS:       return handlePtzGetStatus(body);
        case ipc::MsgType::REQ_GET_IMAGING_SETTINGS: return handleGetImagingSettings(body);
        case ipc::MsgType::REQ_SET_IMAGING_SETTINGS: return handleSetImagingSettings(body);
        case ipc::MsgType::REQ_GET_ANALYTICS_MODULES:return handleGetAnalyticsModules(body);
        case ipc::MsgType::REQ_GET_ANALYTICS_RULES:  return handleGetAnalyticsRules(body);
        case ipc::MsgType::REQ_SUBSCRIBE_EVENTS:     return handleSubscribeEvents(body);
        case ipc::MsgType::REQ_UNSUBSCRIBE_EVENTS:   return handleUnsubscribeEvents(body);
        default:
            return makeErr(rid, "Unknown message type");
    }
}

// ── Handlers ──────────────────────────────────────────────────────────────

ipc::Message IpcServer::handleGetDeviceInfo(const std::string& /*body*/) {
    auto d  = backend_->getDeviceInfo();
    json j  = {
        {"manufacturer",    d.manufacturer},
        {"model",           d.model},
        {"firmwareVersion", d.firmwareVersion},
        {"serialNumber",    d.serialNumber},
        {"hardwareId",      d.hardwareId}
    };
    return makeOk(0, j.dump());
}

ipc::Message IpcServer::handleGetNetworkConfig(const std::string& /*body*/) {
    auto c = backend_->getNetworkConfig();
    json j = {
        {"ipAddress",  c.ipAddress},
        {"subnetMask", c.subnetMask},
        {"gateway",    c.gateway},
        {"macAddress", c.macAddress},
        {"dhcp",       c.dhcp},
        {"httpPort",   c.httpPort},
        {"rtspPort",   c.rtspPort}
    };
    return makeOk(0, j.dump());
}

ipc::Message IpcServer::handleGetDatetime(const std::string& /*body*/) {
    auto dt = backend_->getSystemDateAndTime();
    json j  = {
        {"year",   dt.year}, {"month",  dt.month},  {"day",    dt.day},
        {"hour",   dt.hour}, {"minute", dt.minute},  {"second", dt.second}
    };
    return makeOk(0, j.dump());
}

ipc::Message IpcServer::handleSetDatetime(const std::string& body) {
    auto j = json::parse(body);
    SystemDateTime dt;
    dt.year   = j.value("year",   2024);
    dt.month  = j.value("month",  1);
    dt.day    = j.value("day",    1);
    dt.hour   = j.value("hour",   0);
    dt.minute = j.value("minute", 0);
    dt.second = j.value("second", 0);
    backend_->setSystemDateAndTime(dt);
    return makeOk(0, "{}");
}

ipc::Message IpcServer::handleReboot(const std::string& /*body*/) {
    backend_->reboot();
    return makeOk(0, "{}");
}

ipc::Message IpcServer::handleGetProfiles(const std::string& /*body*/) {
    auto profiles = backend_->getProfiles();
    json arr = json::array();
    for (auto& p : profiles) {
        arr.push_back({
            {"token",       p.token},
            {"name",        p.name},
            {"streamType",  (int)p.streamType},
            {"sourceToken", p.sourceToken},
            {"videoConfig", {
                {"codec",     (int)p.videoConfig.codec},
                {"width",     p.videoConfig.resolution.width},
                {"height",    p.videoConfig.resolution.height},
                {"framerate", p.videoConfig.framerate},
                {"bitrate",   p.videoConfig.bitrate},
                {"profile",   p.videoConfig.profile}
            }}
        });
    }
    return makeOk(0, json{{"profiles", arr}}.dump());
}

ipc::Message IpcServer::handleGetStreamUri(const std::string& body) {
    auto j     = json::parse(body);
    auto token = j.value("profileToken", "profile_main");
    auto proto = (StreamProtocol)j.value("protocol", 0);
    auto uri   = backend_->getStreamUri(token, proto);
    json resp  = {
        {"uri",                  uri.uri},
        {"invalidAfterConnect",  uri.invalidAfterConnect},
        {"invalidAfterReboot",   uri.invalidAfterReboot},
        {"timeoutSeconds",       uri.timeoutSeconds}
    };
    return makeOk(0, resp.dump());
}

ipc::Message IpcServer::handleSetVideoConfig(const std::string& body) {
    auto j     = json::parse(body);
    auto token = j.value("profileToken", "");
    VideoEncoderConfig cfg;
    cfg.codec              = (Codec)j.value("codec", 0);
    cfg.resolution.width   = j.value("width",  1920);
    cfg.resolution.height  = j.value("height", 1080);
    cfg.framerate          = j.value("framerate", 25);
    cfg.bitrate            = j.value("bitrate",  4000);
    cfg.profile            = j.value("profile", "Main");
    if (!backend_->setVideoEncoderConfig(token, cfg)) {
        return makeErr(0, "video publisher reconfiguration failed");
    }
    return makeOk(0, "{}");
}

ipc::Message IpcServer::handleGetSnapshotUri(const std::string& body) {
    auto j     = json::parse(body);
    auto token = j.value("profileToken", "profile_main");
    auto uri   = backend_->getSnapshotUri(token);
    return makeOk(0, json{{"uri", uri.uri}}.dump());
}

ipc::Message IpcServer::handlePtzAbsoluteMove(const std::string& body) {
    auto j     = json::parse(body);
    auto token = j.value("profileToken", "profile_main");
    PTZVector pos{j["pos"].value("pan",0.0f),
                  j["pos"].value("tilt",0.0f),
                  j["pos"].value("zoom",0.0f)};
    PTZVector spd{j["speed"].value("pan",1.0f),
                  j["speed"].value("tilt",1.0f),
                  j["speed"].value("zoom",1.0f)};
    backend_->ptzAbsoluteMove(token, pos, spd);
    return makeOk(0, "{}");
}

ipc::Message IpcServer::handlePtzRelativeMove(const std::string& body) {
    auto j     = json::parse(body);
    auto token = j.value("profileToken", "profile_main");
    PTZVector tr{j["translation"].value("pan",0.0f),
                 j["translation"].value("tilt",0.0f),
                 j["translation"].value("zoom",0.0f)};
    PTZVector spd{j["speed"].value("pan",1.0f),
                  j["speed"].value("tilt",1.0f),
                  j["speed"].value("zoom",1.0f)};
    backend_->ptzRelativeMove(token, tr, spd);
    return makeOk(0, "{}");
}

ipc::Message IpcServer::handlePtzContinuousMove(const std::string& body) {
    auto j     = json::parse(body);
    auto token = j.value("profileToken", "profile_main");
    PTZVector vel{j["velocity"].value("pan",0.0f),
                  j["velocity"].value("tilt",0.0f),
                  j["velocity"].value("zoom",0.0f)};
    backend_->ptzContinuousMove(token, vel);
    return makeOk(0, "{}");
}

ipc::Message IpcServer::handlePtzStop(const std::string& body) {
    auto j     = json::parse(body);
    auto token = j.value("profileToken", "profile_main");
    backend_->ptzStop(token, true, true);
    return makeOk(0, "{}");
}

ipc::Message IpcServer::handlePtzGetStatus(const std::string& body) {
    auto j     = json::parse(body);
    auto token = j.value("profileToken", "profile_main");
    auto st    = backend_->getPtzStatus(token);
    json resp  = {
        {"pan",  st.position.pan},
        {"tilt", st.position.tilt},
        {"zoom", st.position.zoom},
        {"moving", (int)st.moveStatus}
    };
    return makeOk(0, resp.dump());
}

ipc::Message IpcServer::handleGetImagingSettings(const std::string& body) {
    auto j   = json::parse(body);
    auto src = j.value("sourceToken", "src_main");
    auto s   = backend_->getImagingSettings(src);
    json resp = {
        {"brightness",    s.brightness},
        {"contrast",      s.contrast},
        {"saturation",    s.saturation},
        {"sharpness",     s.sharpness},
        {"backlightComp", s.backlightComp},
        {"wideDynRange",  s.wideDynRange}
    };
    return makeOk(0, resp.dump());
}

ipc::Message IpcServer::handleSetImagingSettings(const std::string& body) {
    auto j   = json::parse(body);
    auto src = j.value("sourceToken", "src_main");
    ImagingSettings s;
    s.brightness    = j.value("brightness",    50.0f);
    s.contrast      = j.value("contrast",      50.0f);
    s.saturation    = j.value("saturation",    50.0f);
    s.sharpness     = j.value("sharpness",     50.0f);
    s.backlightComp = j.value("backlightComp", false);
    s.wideDynRange  = j.value("wideDynRange",  false);
    backend_->setImagingSettings(src, s);
    return makeOk(0, "{}");
}

ipc::Message IpcServer::handleGetAnalyticsModules(const std::string& body) {
    auto j     = json::parse(body);
    auto token = j.value("configToken", "");
    auto mods  = backend_->getSupportedAnalyticsModules(token);
    json arr   = json::array();
    for (auto& m : mods)
        arr.push_back({{"type", m.type}, {"name", m.name}, {"version", m.version}});
    return makeOk(0, json{{"modules", arr}}.dump());
}

ipc::Message IpcServer::handleGetAnalyticsRules(const std::string& body) {
    auto j     = json::parse(body);
    auto token = j.value("configToken", "");
    auto rules = backend_->getAnalyticsRules(token);
    json arr   = json::array();
    for (auto& r : rules)
        arr.push_back({{"type", r.type}, {"name", r.name}});
    return makeOk(0, json{{"rules", arr}}.dump());
}

ipc::Message IpcServer::handleSubscribeEvents(const std::string& body) {
    auto j    = json::parse(body);
    auto id   = j.value("subscriptionId", "sub1");
    auto filt = j.value("filter", "");
    backend_->subscribe(id, filt, nullptr);
    return makeOk(0, "{}");
}

ipc::Message IpcServer::handleUnsubscribeEvents(const std::string& body) {
    auto j  = json::parse(body);
    auto id = j.value("subscriptionId", "");
    backend_->unsubscribe(id);
    return makeOk(0, "{}");
}
