#include "mock/MockCameraBackend.h"
#include <chrono>
#include <ctime>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <sstream>

MockCameraBackend::MockCameraBackend(const std::string& configPath)
    : configPath_(configPath)
    , ptz_(std::make_unique<MockPtzController>())
    , eventGen_(std::make_unique<MockEventGenerator>())
{
    for (auto& p : buildProfiles())
        videoConfigs_[p.token] = p.videoConfig;

    ImagingSettings def{};
    for (auto& tok : {"src_main", "src_sub1", "src_sub2"})
        imagingSettings_[tok] = def;

    printf("[MockCameraBackend] initialized, config=%s\n",
           configPath_.c_str());
}

// ── Device ────────────────────────────────────────────────────────────────

DeviceInfo MockCameraBackend::buildDeviceInfo() const {
    DeviceInfo d;
    d.manufacturer    = "MockCam Inc.";
    d.model           = "MockCam-4K";
    d.firmwareVersion = "1.0.0";
    d.serialNumber    = "MOCK-001-2024";
    d.hardwareId      = "JetsonOrinNX-8GB";
    return d;
}

DeviceInfo MockCameraBackend::getDeviceInfo() {
    return buildDeviceInfo();
}

NetworkConfig MockCameraBackend::getNetworkConfig() {
    NetworkConfig c;
    c.ipAddress  = "192.168.1.100";
    c.subnetMask = "255.255.255.0";
    c.gateway    = "192.168.1.1";
    c.macAddress = "AA:BB:CC:DD:EE:FF";
    c.dhcp       = false;
    c.httpPort   = 8080;
    c.rtspPort   = 8554;
    return c;
}

bool MockCameraBackend::setNetworkConfig(const NetworkConfig& cfg) {
    printf("[MockCameraBackend] setNetworkConfig ip=%s (no-op)\n",
           cfg.ipAddress.c_str());
    return true;
}

SystemDateTime MockCameraBackend::getSystemDateAndTime() {
    SystemDateTime dt;
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    auto* tm = std::gmtime(&tt);
    dt.year   = tm->tm_year + 1900;
    dt.month  = tm->tm_mon  + 1;
    dt.day    = tm->tm_mday;
    dt.hour   = tm->tm_hour;
    dt.minute = tm->tm_min;
    dt.second = tm->tm_sec;
    return dt;
}

bool MockCameraBackend::setSystemDateAndTime(const SystemDateTime& dt) {
    printf("[MockCameraBackend] setSystemDateAndTime %d-%02d-%02d (no-op)\n",
           dt.year, dt.month, dt.day);
    return true;
}

bool MockCameraBackend::reboot() {
    printf("[MockCameraBackend] reboot (no-op)\n");
    return true;
}

bool MockCameraBackend::factoryReset(bool hard) {
    printf("[MockCameraBackend] factoryReset hard=%d (no-op)\n", hard);
    return true;
}

// ── Media2 ────────────────────────────────────────────────────────────────

std::vector<StreamProfile> MockCameraBackend::buildProfiles() const {
    std::vector<StreamProfile> v;

    StreamProfile main;
    main.token                  = "profile_main";
    main.name                   = "Main Stream 4K";
    main.streamType             = StreamType::MAIN;
    main.sourceToken            = "src_main";
    main.videoConfig.codec      = Codec::H264;
    main.videoConfig.resolution = RES_4K;
    main.videoConfig.framerate  = 30;
    main.videoConfig.bitrate    = 20000;
    main.videoConfig.profile    = "High";
    v.push_back(main);

    StreamProfile sub1;
    sub1.token                  = "profile_sub1";
    sub1.name                   = "Sub Stream 1080p";
    sub1.streamType             = StreamType::SUB1;
    sub1.sourceToken            = "src_sub1";
    sub1.videoConfig.codec      = Codec::H264;
    sub1.videoConfig.resolution = RES_1080P;
    sub1.videoConfig.framerate  = 15;
    sub1.videoConfig.bitrate    = 4000;
    sub1.videoConfig.profile    = "Main";
    v.push_back(sub1);

    StreamProfile sub2;
    sub2.token                  = "profile_sub2";
    sub2.name                   = "Sub Stream 480p";
    sub2.streamType             = StreamType::SUB2;
    sub2.sourceToken            = "src_sub2";
    sub2.videoConfig.codec      = Codec::H265;
    sub2.videoConfig.resolution = RES_480P;
    sub2.videoConfig.framerate  = 10;
    sub2.videoConfig.bitrate    = 512;
    sub2.videoConfig.profile    = "Main10";
    v.push_back(sub2);

    return v;
}

std::vector<StreamProfile> MockCameraBackend::getProfiles() {
    auto profiles = buildProfiles();
    for (auto& p : profiles) {
        auto it = videoConfigs_.find(p.token);
        if (it != videoConfigs_.end())
            p.videoConfig = it->second;
    }
    return profiles;
}

StreamUri MockCameraBackend::getStreamUri(const std::string& token,
                                           StreamProtocol /*proto*/) {
    StreamUri u;
    if      (token == "profile_main") u.uri = "rtsp://127.0.0.1:8554/main";
    else if (token == "profile_sub1") u.uri = "rtsp://127.0.0.1:8554/sub1";
    else if (token == "profile_sub2") u.uri = "rtsp://127.0.0.1:8554/sub2";
    else                              u.uri = "";
    u.invalidAfterConnect = false;
    u.invalidAfterReboot  = true;
    u.timeoutSeconds      = 60;
    return u;
}

bool MockCameraBackend::setVideoEncoderConfig(const std::string& token,
                                               const VideoEncoderConfig& cfg) {
    printf("[MockCameraBackend] setVideoEncoderConfig token=%s %dx%d\n",
           token.c_str(), cfg.resolution.width, cfg.resolution.height);
    if (token != "profile_main" && token != "profile_sub1" && token != "profile_sub2") {
        videoConfigs_[token] = cfg;
        return true;
    }

    // The RTSP publisher is a separate GStreamer process. Reconfigure it
    // before publishing the new SOAP-visible state, so DTT can verify the
    // negotiated RTP resolution after SetVideoEncoderConfiguration returns.
    const char* rootEnv = std::getenv("MOCK_CAMERA_ROOT");
    const std::string root = rootEnv ? rootEnv : ".";
    std::ostringstream cmd;
    cmd << "bash \"" << root << "/scripts/reconfigure_stream.sh\" "
        << token << " " << cfg.resolution.width << " "
        << cfg.resolution.height << " " << cfg.framerate << " "
        << (cfg.bitrate * 1000);
    const int rc = std::system(cmd.str().c_str());
    if (rc != 0) {
        printf("[MockCameraBackend] publisher reconfigure failed rc=%d\n", rc);
        return false;
    }
    videoConfigs_[token] = cfg;
    return true;
}

SnapshotUri MockCameraBackend::getSnapshotUri(const std::string& token) {
    SnapshotUri u;
    u.uri = "http://127.0.0.1:8080/snapshot?token=" + token;
    return u;
}

// ── PTZ ───────────────────────────────────────────────────────────────────

bool MockCameraBackend::ptzAbsoluteMove(const std::string& t,
                                         const PTZVector& pos,
                                         const PTZVector& spd) {
    return ptz_->absoluteMove(t, pos, spd);
}
bool MockCameraBackend::ptzRelativeMove(const std::string& t,
                                         const PTZVector& tr,
                                         const PTZVector& spd) {
    return ptz_->relativeMove(t, tr, spd);
}
bool MockCameraBackend::ptzContinuousMove(const std::string& t,
                                           const PTZVector& vel) {
    return ptz_->continuousMove(t, vel);
}
bool MockCameraBackend::ptzStop(const std::string& t,
                                 bool pt, bool z) {
    return ptz_->stop(t, pt, z);
}
PTZStatus MockCameraBackend::getPtzStatus(const std::string& t) {
    return ptz_->getStatus(t);
}
bool MockCameraBackend::gotoHomePosition(const std::string& t) {
    return ptz_->gotoHome(t);
}
bool MockCameraBackend::setHomePosition(const std::string& t) {
    return ptz_->setHome(t);
}

// ── Imaging ───────────────────────────────────────────────────────────────

ImagingSettings MockCameraBackend::getImagingSettings(const std::string& src) {
    auto it = imagingSettings_.find(src);
    return (it != imagingSettings_.end()) ? it->second : ImagingSettings{};
}
bool MockCameraBackend::setImagingSettings(const std::string& src,
                                            const ImagingSettings& s) {
    imagingSettings_[src] = s;
    return true;
}
ImagingStatus MockCameraBackend::getImagingStatus(const std::string& /*src*/) {
    ImagingStatus s;
    s.focusStatus   = FocusStatus::IDLE;
    s.focusPosition = 0.5f;
    return s;
}

// ── Analytics ─────────────────────────────────────────────────────────────

std::vector<AnalyticsModule> MockCameraBackend::getSupportedAnalyticsModules(
        const std::string& /*token*/) {
    return {
        {"tt:MotionDetector", "Motion Detection", "1.0"},
        {"tt:TamperDetector", "Tamper Detection", "1.0"},
        {"tt:ObjectDetector", "Object Detection", "1.0"},
    };
}
std::vector<AnalyticsRule> MockCameraBackend::getAnalyticsRules(
        const std::string& /*token*/) {
    std::vector<AnalyticsRule> v;
    for (auto& [k, r] : analyticsRules_) v.push_back(r);
    return v;
}
bool MockCameraBackend::addAnalyticsRule(const std::string& /*token*/,
                                          const AnalyticsRule& r) {
    analyticsRules_[r.name] = r;
    return true;
}
bool MockCameraBackend::deleteAnalyticsRule(const std::string& /*token*/,
                                             const std::string& name) {
    analyticsRules_.erase(name);
    return true;
}

// ── Events ────────────────────────────────────────────────────────────────

bool MockCameraBackend::subscribe(const std::string& id,
                                   const std::string& /*filter*/,
                                   EventCallback cb) {
    printf("[MockCameraBackend] subscribe id=%s\n", id.c_str());
    eventGen_->addSubscription(id, cb);
    return true;
}
bool MockCameraBackend::unsubscribe(const std::string& id) {
    eventGen_->removeSubscription(id);
    return true;
}
bool MockCameraBackend::renewSubscription(const std::string& id, int sec) {
    printf("[MockCameraBackend] renewSubscription id=%s sec=%d\n",
           id.c_str(), sec);
    return true;
}
