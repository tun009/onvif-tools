#pragma once
#include <string>
#include <vector>

enum class Codec           { H264 = 0, H265 = 1 };
enum class StreamProtocol  { RTSP = 0, RTP_UNICAST = 1, RTP_MULTICAST = 2 };
enum class StreamType      { MAIN = 0, SUB1 = 1, SUB2 = 2 };

struct Resolution {
    int width  = 1920;
    int height = 1080;
};

static const Resolution RES_4K    = {3840, 2160};
static const Resolution RES_1080P = {1920, 1080};
static const Resolution RES_720P  = {1280,  720};
static const Resolution RES_480P  = { 640,  480};

struct VideoEncoderConfig {
    Codec       codec      = Codec::H264;
    Resolution  resolution = RES_1080P;
    int         framerate  = 25;
    int         bitrate    = 4000;   // kbps
    std::string profile    = "Main"; // "Baseline","Main","High","Main10"
};

struct StreamProfile {
    std::string        token;
    std::string        name;
    StreamType         streamType  = StreamType::MAIN;
    std::string        sourceToken;
    VideoEncoderConfig videoConfig;
};

struct StreamUri {
    std::string uri;
    bool        invalidAfterConnect = false;
    bool        invalidAfterReboot  = true;
    int         timeoutSeconds      = 60;
};

struct SnapshotUri {
    std::string uri;
    bool        invalidAfterConnect = false;
    bool        invalidAfterReboot  = true;
};
