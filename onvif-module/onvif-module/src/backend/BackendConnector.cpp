#include "backend/BackendConnector.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <stdexcept>
#include <sstream>

// Minimal JSON helpers (avoid dependency for onvif-module side)
#include "utils/SimpleJson.h"

// ── Wire helpers ──────────────────────────────────────────────────────────

bool BackendConnector::sendMessage(int fd, const ipc::Message& msg) {
    if (::send(fd, &msg.header, sizeof(msg.header), MSG_NOSIGNAL)
            != (ssize_t)sizeof(msg.header)) return false;
    if (!msg.payload.empty())
        if (::send(fd, msg.payload.data(), msg.payload.size(), MSG_NOSIGNAL)
                != (ssize_t)msg.payload.size()) return false;
    return true;
}

bool BackendConnector::recvMessage(int fd, ipc::Message& msg) {
    if (::recv(fd, &msg.header, sizeof(msg.header), MSG_WAITALL)
            != (ssize_t)sizeof(msg.header)) return false;
    if (msg.header.magic != ipc::MAGIC) return false;
    if (msg.header.payloadLen > 0) {
        msg.payload.resize(msg.header.payloadLen);
        if (::recv(fd, msg.payload.data(), msg.header.payloadLen, MSG_WAITALL)
                != (ssize_t)msg.header.payloadLen) return false;
    }
    return true;
}

ipc::Message BackendConnector::sendRequest(ipc::MsgType type,
                                            const std::string& json) {
    std::lock_guard<std::mutex> lock(requestMutex_);
    ipc::Message req;
    req.header.magic      = ipc::MAGIC;
    req.header.version    = ipc::VERSION;
    req.header.msgType    = (uint8_t)type;
    req.header.flags      = 0;
    req.header.requestId  = nextId();
    req.header.payloadLen = (uint32_t)json.size();
    req.payload.assign(json.begin(), json.end());

    if (!sendMessage(ctrlFd_, req))
        throw std::runtime_error("IPC send failed");

    ipc::Message resp;
    if (!recvMessage(ctrlFd_, resp))
        throw std::runtime_error("IPC recv failed");

    return resp;
}

// ── Connect / Disconnect ──────────────────────────────────────────────────

BackendConnector::BackendConnector(const std::string& ctrl,
                                    const std::string& evt)
    : ctrlPath_(ctrl), evtPath_(evt) {}

BackendConnector::~BackendConnector() { disconnect(); }

bool BackendConnector::connect() {
    auto makeConn = [](const std::string& path) -> int {
        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path)-1);
        if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
            ::close(fd); return -1;
        }
        return fd;
    };

    ctrlFd_ = makeConn(ctrlPath_);
    if (ctrlFd_ < 0) {
        fprintf(stderr, "[BackendConnector] Cannot connect ctrl: %s\n",
                ctrlPath_.c_str());
        return false;
    }

    evtFd_ = makeConn(evtPath_);
    if (evtFd_ < 0)
        fprintf(stderr, "[BackendConnector] Cannot connect evt: %s (non-fatal)\n",
                evtPath_.c_str());

    connected_  = true;
    evtRunning_ = true;
    evtThread_  = std::thread(&BackendConnector::eventListenerLoop, this);

    printf("[BackendConnector] connected ctrl=%s\n", ctrlPath_.c_str());
    return true;
}

void BackendConnector::disconnect() {
    connected_  = false;
    evtRunning_ = false;
    if (ctrlFd_ >= 0) {
        ::shutdown(ctrlFd_, SHUT_RDWR);
        ::close(ctrlFd_);
        ctrlFd_ = -1;
    }
    if (evtFd_  >= 0) {
        ::shutdown(evtFd_, SHUT_RDWR);
        ::close(evtFd_);
        evtFd_  = -1;
    }
    if (evtThread_.joinable()) evtThread_.join();
}

void BackendConnector::eventListenerLoop() {
    while (evtRunning_ && evtFd_ >= 0) {
        ipc::Message msg;
        if (!recvMessage(evtFd_, msg)) {
            if (evtRunning_)
                fprintf(stderr, "[BackendConnector] evt socket disconnected\n");
            break;
        }
        std::string payload(msg.payload.begin(), msg.payload.end());
        std::lock_guard<std::mutex> lk(cbMutex_);
        for (auto& [id, cb] : callbacks_)
            if (cb) cb((ipc::MsgType)msg.header.msgType, payload);
    }
}

// ── Device ────────────────────────────────────────────────────────────────

DeviceInfo BackendConnector::getDeviceInfo() {
    auto resp = sendRequest(ipc::MsgType::REQ_GET_DEVICE_INFO, "{}");
    std::string j(resp.payload.begin(), resp.payload.end());
    DeviceInfo d;
    d.manufacturer    = SimpleJson::getString(j, "manufacturer");
    d.model           = SimpleJson::getString(j, "model");
    d.firmwareVersion = SimpleJson::getString(j, "firmwareVersion");
    d.serialNumber    = SimpleJson::getString(j, "serialNumber");
    d.hardwareId      = SimpleJson::getString(j, "hardwareId");
    return d;
}

NetworkConfig BackendConnector::getNetworkConfig() {
    auto resp = sendRequest(ipc::MsgType::REQ_GET_NETWORK_CONFIG, "{}");
    std::string j(resp.payload.begin(), resp.payload.end());
    NetworkConfig c;
    c.ipAddress  = SimpleJson::getString(j, "ipAddress");
    c.macAddress = SimpleJson::getString(j, "macAddress");
    c.httpPort   = SimpleJson::getInt(j, "httpPort", 8080);
    c.rtspPort   = SimpleJson::getInt(j, "rtspPort", 8554);
    c.dhcp       = SimpleJson::getBool(j, "dhcp", false);
    return c;
}

bool BackendConnector::setNetworkConfig(const NetworkConfig& cfg) {
    std::string req = "{\"ipAddress\":\"" + cfg.ipAddress + "\"}";
    sendRequest(ipc::MsgType::REQ_SET_NETWORK_CONFIG, req);
    return true;
}

SystemDateTime BackendConnector::getSystemDateAndTime() {
    auto resp = sendRequest(ipc::MsgType::REQ_GET_DATETIME, "{}");
    std::string j(resp.payload.begin(), resp.payload.end());
    SystemDateTime dt;
    dt.year   = SimpleJson::getInt(j, "year",   2024);
    dt.month  = SimpleJson::getInt(j, "month",  1);
    dt.day    = SimpleJson::getInt(j, "day",    1);
    dt.hour   = SimpleJson::getInt(j, "hour",   0);
    dt.minute = SimpleJson::getInt(j, "minute", 0);
    dt.second = SimpleJson::getInt(j, "second", 0);
    return dt;
}

bool BackendConnector::setSystemDateAndTime(const SystemDateTime& dt) {
    std::ostringstream oss;
    oss << "{\"year\":" << dt.year << ",\"month\":" << dt.month
        << ",\"day\":" << dt.day  << ",\"hour\":" << dt.hour
        << ",\"minute\":" << dt.minute << ",\"second\":" << dt.second << "}";
    sendRequest(ipc::MsgType::REQ_SET_DATETIME, oss.str());
    return true;
}

bool BackendConnector::reboot() {
    sendRequest(ipc::MsgType::REQ_REBOOT, "{}");
    return true;
}

bool BackendConnector::factoryReset(bool hard) {
    sendRequest(ipc::MsgType::REQ_FACTORY_RESET,
                std::string("{\"hard\":") + (hard?"true":"false") + "}");
    return true;
}

// ── Media2 ────────────────────────────────────────────────────────────────

std::vector<StreamProfile> BackendConnector::getProfiles() {
    auto resp = sendRequest(ipc::MsgType::REQ_GET_PROFILES, "{}");
    std::string j(resp.payload.begin(), resp.payload.end());
    // Parse array - simplified: return 3 hardcoded profiles for now
    // TODO: implement full JSON array parser
    std::vector<StreamProfile> profiles;
    // Mock camera có 1 VideoSource vật lý duy nhất (src_main); 3 profile là
    // 3 encoding variants của cùng source. Nếu để mỗi profile 1 sourceToken
    // riêng, VideoSourceConfiguration sẽ inconsistent giữa GetProfiles và
    // GetVideoSourceConfigurations (MEDIA2-2-2-4).
    StreamProfile m; m.token="profile_main"; m.name="Main 4K";
    m.sourceToken="src_main"; m.streamType=StreamType::MAIN;
    m.videoConfig.codec=Codec::H264; m.videoConfig.resolution=RES_4K;
    m.videoConfig.framerate=30; m.videoConfig.bitrate=20000;
    profiles.push_back(m);
    StreamProfile s1; s1.token="profile_sub1"; s1.name="Sub1 1080p";
    s1.sourceToken="src_main"; s1.streamType=StreamType::SUB1;
    s1.videoConfig.codec=Codec::H264; s1.videoConfig.resolution=RES_1080P;
    s1.videoConfig.framerate=15; s1.videoConfig.bitrate=4000;
    profiles.push_back(s1);
    StreamProfile s2; s2.token="profile_sub2"; s2.name="Sub2 480p";
    s2.sourceToken="src_main"; s2.streamType=StreamType::SUB2;
    s2.videoConfig.codec=Codec::H264; s2.videoConfig.resolution=RES_480P;
    s2.videoConfig.framerate=10; s2.videoConfig.bitrate=512;
    profiles.push_back(s2);
    return profiles;
}

StreamUri BackendConnector::getStreamUri(const std::string& token,
                                          StreamProtocol proto) {
    std::ostringstream oss;
    oss << "{\"profileToken\":\"" << token << "\",\"protocol\":"
        << (int)proto << "}";
    auto resp = sendRequest(ipc::MsgType::REQ_GET_STREAM_URI, oss.str());
    std::string j(resp.payload.begin(), resp.payload.end());
    StreamUri u;
    u.uri            = SimpleJson::getString(j, "uri");
    u.timeoutSeconds = SimpleJson::getInt(j, "timeoutSeconds", 60);
    return u;
}

bool BackendConnector::setVideoEncoderConfig(const std::string& token,
                                              const VideoEncoderConfig& cfg) {
    std::ostringstream oss;
    oss << "{\"profileToken\":\"" << token << "\","
        << "\"codec\":" << (int)cfg.codec << ","
        << "\"width\":" << cfg.resolution.width << ","
        << "\"height\":" << cfg.resolution.height << ","
        << "\"framerate\":" << cfg.framerate << ","
        << "\"bitrate\":" << cfg.bitrate << "}";
    sendRequest(ipc::MsgType::REQ_SET_VIDEO_CONFIG, oss.str());
    return true;
}

SnapshotUri BackendConnector::getSnapshotUri(const std::string& token) {
    auto resp = sendRequest(ipc::MsgType::REQ_GET_SNAPSHOT_URI,
                            "{\"profileToken\":\"" + token + "\"}");
    std::string j(resp.payload.begin(), resp.payload.end());
    SnapshotUri u;
    u.uri = SimpleJson::getString(j, "uri");
    return u;
}

// ── PTZ ───────────────────────────────────────────────────────────────────

bool BackendConnector::ptzAbsoluteMove(const std::string& token,
                                        const PTZVector& pos,
                                        const PTZVector& speed) {
    std::ostringstream oss;
    oss << "{\"profileToken\":\"" << token << "\","
        << "\"pos\":{\"pan\":" << pos.pan << ",\"tilt\":" << pos.tilt
        << ",\"zoom\":" << pos.zoom << "},"
        << "\"speed\":{\"pan\":" << speed.pan << ",\"tilt\":" << speed.tilt
        << ",\"zoom\":" << speed.zoom << "}}";
    sendRequest(ipc::MsgType::REQ_PTZ_ABSOLUTE_MOVE, oss.str());
    return true;
}

bool BackendConnector::ptzRelativeMove(const std::string& token,
                                        const PTZVector& trans,
                                        const PTZVector& speed) {
    std::ostringstream oss;
    oss << "{\"profileToken\":\"" << token << "\","
        << "\"translation\":{\"pan\":" << trans.pan << ",\"tilt\":" << trans.tilt
        << ",\"zoom\":" << trans.zoom << "},"
        << "\"speed\":{\"pan\":" << speed.pan << ",\"tilt\":" << speed.tilt
        << ",\"zoom\":" << speed.zoom << "}}";
    sendRequest(ipc::MsgType::REQ_PTZ_RELATIVE_MOVE, oss.str());
    return true;
}

bool BackendConnector::ptzContinuousMove(const std::string& token,
                                          const PTZVector& vel) {
    std::ostringstream oss;
    oss << "{\"profileToken\":\"" << token << "\","
        << "\"velocity\":{\"pan\":" << vel.pan << ",\"tilt\":" << vel.tilt
        << ",\"zoom\":" << vel.zoom << "}}";
    sendRequest(ipc::MsgType::REQ_PTZ_CONTINUOUS_MOVE, oss.str());
    return true;
}

bool BackendConnector::ptzStop(const std::string& token,
                                bool /*panTilt*/, bool /*zoom*/) {
    sendRequest(ipc::MsgType::REQ_PTZ_STOP,
                "{\"profileToken\":\"" + token + "\"}");
    return true;
}

PTZStatus BackendConnector::getPtzStatus(const std::string& token) {
    auto resp = sendRequest(ipc::MsgType::REQ_PTZ_GET_STATUS,
                            "{\"profileToken\":\"" + token + "\"}");
    std::string j(resp.payload.begin(), resp.payload.end());
    PTZStatus st;
    st.position.pan  = SimpleJson::getFloat(j, "pan",  0.0f);
    st.position.tilt = SimpleJson::getFloat(j, "tilt", 0.0f);
    st.position.zoom = SimpleJson::getFloat(j, "zoom", 0.0f);
    return st;
}

bool BackendConnector::gotoHomePosition(const std::string& token) {
    sendRequest(ipc::MsgType::REQ_PTZ_GOTO_HOME,
                "{\"profileToken\":\"" + token + "\"}");
    return true;
}

bool BackendConnector::setHomePosition(const std::string& token) {
    sendRequest(ipc::MsgType::REQ_PTZ_SET_HOME,
                "{\"profileToken\":\"" + token + "\"}");
    return true;
}

// ── Imaging ───────────────────────────────────────────────────────────────

ImagingSettings BackendConnector::getImagingSettings(const std::string& src) {
    auto resp = sendRequest(ipc::MsgType::REQ_GET_IMAGING_SETTINGS,
                            "{\"sourceToken\":\"" + src + "\"}");
    std::string j(resp.payload.begin(), resp.payload.end());
    ImagingSettings s;
    s.brightness = SimpleJson::getFloat(j, "brightness", 50.0f);
    s.contrast   = SimpleJson::getFloat(j, "contrast",   50.0f);
    s.saturation = SimpleJson::getFloat(j, "saturation", 50.0f);
    s.sharpness  = SimpleJson::getFloat(j, "sharpness",  50.0f);
    return s;
}

bool BackendConnector::setImagingSettings(const std::string& src,
                                           const ImagingSettings& s) {
    std::ostringstream oss;
    oss << "{\"sourceToken\":\"" << src << "\","
        << "\"brightness\":" << s.brightness << ","
        << "\"contrast\":"   << s.contrast   << ","
        << "\"saturation\":" << s.saturation << ","
        << "\"sharpness\":"  << s.sharpness  << "}";
    sendRequest(ipc::MsgType::REQ_SET_IMAGING_SETTINGS, oss.str());
    return true;
}

ImagingStatus BackendConnector::getImagingStatus(const std::string& /*src*/) {
    return ImagingStatus{};
}

// ── Analytics ─────────────────────────────────────────────────────────────

std::vector<AnalyticsModule> BackendConnector::getSupportedAnalyticsModules(
        const std::string& token) {
    sendRequest(ipc::MsgType::REQ_GET_ANALYTICS_MODULES,
                "{\"configToken\":\"" + token + "\"}");
    return {
        {"tt:MotionDetector", "Motion Detection", "1.0"},
        {"tt:TamperDetector", "Tamper Detection", "1.0"},
        {"tt:ObjectDetector", "Object Detection", "1.0"},
    };
}

std::vector<AnalyticsRule> BackendConnector::getAnalyticsRules(
        const std::string& token) {
    sendRequest(ipc::MsgType::REQ_GET_ANALYTICS_RULES,
                "{\"configToken\":\"" + token + "\"}");
    return {};
}

bool BackendConnector::addAnalyticsRule(const std::string& token,
                                         const AnalyticsRule& rule) {
    std::ostringstream oss;
    oss << "{\"configToken\":\"" << token << "\","
        << "\"name\":\"" << rule.name << "\","
        << "\"type\":\"" << rule.type << "\"}";
    sendRequest(ipc::MsgType::REQ_ADD_ANALYTICS_RULE, oss.str());
    return true;
}

bool BackendConnector::deleteAnalyticsRule(const std::string& token,
                                            const std::string& name) {
    sendRequest(ipc::MsgType::REQ_DEL_ANALYTICS_RULE,
                "{\"configToken\":\"" + token + "\",\"name\":\"" + name + "\"}");
    return true;
}

// ── Events ────────────────────────────────────────────────────────────────

bool BackendConnector::subscribe(const std::string& id,
                                  const std::string& filter,
                                  EventCallback cb) {
    sendRequest(ipc::MsgType::REQ_SUBSCRIBE_EVENTS,
                "{\"subscriptionId\":\"" + id + "\","
                "\"filter\":\"" + filter + "\"}");
    if (cb) {
        std::lock_guard<std::mutex> lk(cbMutex_);
        callbacks_[id] = cb;
    }
    return true;
}

bool BackendConnector::unsubscribe(const std::string& id) {
    sendRequest(ipc::MsgType::REQ_UNSUBSCRIBE_EVENTS,
                "{\"subscriptionId\":\"" + id + "\"}");
    std::lock_guard<std::mutex> lk(cbMutex_);
    callbacks_.erase(id);
    return true;
}

bool BackendConnector::renewSubscription(const std::string& id, int sec) {
    sendRequest(ipc::MsgType::REQ_RENEW_SUBSCRIPTION,
                "{\"subscriptionId\":\"" + id + "\",\"timeout\":" +
                std::to_string(sec) + "}");
    return true;
}
