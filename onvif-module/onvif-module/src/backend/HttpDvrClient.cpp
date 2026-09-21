#include "backend/HttpDvrClient.h"
#include "utils/SimpleJson.h"
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <sstream>
#include <utility>

// Cùng kỹ thuật parse HTTP/JSON thủ công như HttpMgmtClient.cpp (không dùng
// thư viện JSON ngoài) — copy lại vì các helper đó là file-local (anonymous
// namespace), không export qua header nào để tái sử dụng.
namespace {

struct Endpoint { std::string host; std::string port; std::string basePath; };

Endpoint parseHttpUrl(const std::string& url) {
    const std::string prefix = "http://";
    if (url.rfind(prefix, 0) != 0) throw std::runtime_error("DVR URL must use http://");
    const std::string rest = url.substr(prefix.size());
    const auto slash = rest.find('/');
    const std::string authority = rest.substr(0, slash);
    const auto colon = authority.rfind(':');
    Endpoint result{colon == std::string::npos ? authority : authority.substr(0, colon),
                    colon == std::string::npos ? "80" : authority.substr(colon + 1),
                    slash == std::string::npos ? "" : rest.substr(slash)};
    if (result.host.empty() || result.port.empty()) throw std::runtime_error("Invalid DVR URL");
    return result;
}

void sendAll(int fd, const std::string& data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t sent = send(fd, data.data() + offset, data.size() - offset, MSG_NOSIGNAL);
        if (sent <= 0) throw std::runtime_error("Failed to send DVR request");
        offset += static_cast<std::size_t>(sent);
    }
}

// Trích chuỗi con "{...}" ứng với 1 key, bracket-matching (bỏ qua {}/[] bên
// trong chuỗi JSON string). Giống hệt jsonObject() trong HttpMgmtClient.cpp.
std::string jsonObject(const std::string& json, const std::string& key) {
    const std::string marker = "\"" + key + "\":";
    auto start = json.find(marker);
    if (start == std::string::npos) return {};
    start = json.find('{', start + marker.size());
    if (start == std::string::npos) return {};
    int depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (std::size_t pos = start; pos < json.size(); ++pos) {
        const char ch = json[pos];
        if (quoted) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') quoted = false;
            continue;
        }
        if (ch == '"') quoted = true;
        else if (ch == '{') ++depth;
        else if (ch == '}' && --depth == 0) return json.substr(start, pos - start + 1);
    }
    return {};
}

std::string jsonArray(const std::string& json, const std::string& key) {
    const std::string marker = "\"" + key + "\":";
    auto start = json.find(marker);
    if (start == std::string::npos) return {};
    start = json.find('[', start + marker.size());
    if (start == std::string::npos) return {};
    int depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (std::size_t pos = start; pos < json.size(); ++pos) {
        const char ch = json[pos];
        if (quoted) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') quoted = false;
            continue;
        }
        if (ch == '"') quoted = true;
        else if (ch == '[') ++depth;
        else if (ch == ']' && --depth == 0) return json.substr(start, pos - start + 1);
    }
    return {};
}

// Tách 1 mảng JSON thành các object con ở top-level (đủ dùng vì Profiles[]
// không lồng mảng object nào khác ở top-level ngoài object).
std::vector<std::string> jsonArrayObjects(const std::string& arrayJson) {
    std::vector<std::string> result;
    int depth = 0;
    bool quoted = false;
    bool escaped = false;
    std::size_t objStart = std::string::npos;
    for (std::size_t pos = 0; pos < arrayJson.size(); ++pos) {
        const char ch = arrayJson[pos];
        if (quoted) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') quoted = false;
            continue;
        }
        if (ch == '"') { quoted = true; continue; }
        if (ch == '{') { if (depth == 0) objStart = pos; ++depth; continue; }
        if (ch == '}') {
            if (--depth == 0 && objStart != std::string::npos) {
                result.push_back(arrayJson.substr(objStart, pos - objStart + 1));
                objStart = std::string::npos;
            }
        }
    }
    return result;
}

// DVR luôn trả URI với host nội bộ (127.0.0.1, vì onvif-module gọi qua
// loopback) — thay bằng IP thiết bị thật trước khi đưa vào response ONVIF,
// giữ nguyên port (khác nhau theo từng máy, không được hardcode).
std::string rewriteUriHost(const std::string& uri, const std::string& deviceIp) {
    if (deviceIp.empty()) return uri;
    const auto schemeEnd = uri.find("://");
    if (schemeEnd == std::string::npos) return uri;
    const auto hostStart = schemeEnd + 3;
    auto hostEnd = uri.find_first_of(":/", hostStart);
    if (hostEnd == std::string::npos) hostEnd = uri.size();
    return uri.substr(0, hostStart) + deviceIp + uri.substr(hostEnd);
}

StreamType parseStreamType(const std::string& type) {
    if (type == "main") return StreamType::MAIN;
    if (type == "sub") return StreamType::SUB1;
    // "third"/"fourth" (khi DVR_EXTRA_STREAMS>0) không có enum riêng trong
    // StreamType (chỉ MAIN/SUB1/SUB2) — dồn về SUB2 làm fallback hợp lý nhất.
    return StreamType::SUB2;
}

Codec parseCodec(const std::string& encoding) {
    if (encoding == "H265") return Codec::H265;
    return Codec::H264;
}

// DVR hiện luôn gửi EncodingProfile=1 (chưa phải enum có ý nghĩa thay đổi
// thật) — "Main" là baseline an toàn nhất cho cả H.264/H.265 lúc này.
std::string encodingProfileToString(int) { return "Main"; }

} // namespace

HttpDvrClient::HttpDvrClient(DvrClientConfig config) : config_(std::move(config)) {}

HttpDvrClient::HttpResponse HttpDvrClient::request(
    const std::string& method, const std::string& path, const std::string& body) const {
    const auto endpoint = parseHttpUrl(config_.baseUrl);
    addrinfo hints{};
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    if (getaddrinfo(endpoint.host.c_str(), endpoint.port.c_str(), &hints, &addresses) != 0)
        throw std::runtime_error("Cannot resolve DVR host");
    int fd = -1;
    for (auto* address = addresses; address; address = address->ai_next) {
        fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) continue;
        timeval connectTimeout{config_.connectTimeoutMs / 1000, (config_.connectTimeoutMs % 1000) * 1000};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &connectTimeout, sizeof(connectTimeout));
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &connectTimeout, sizeof(connectTimeout));
        if (connect(fd, address->ai_addr, address->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(addresses);
    if (fd < 0) throw std::runtime_error("Cannot connect to DVR");
    timeval requestTimeout{config_.requestTimeoutMs / 1000, (config_.requestTimeoutMs % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &requestTimeout, sizeof(requestTimeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &requestTimeout, sizeof(requestTimeout));
    std::string payload = method + " " + endpoint.basePath + path +
        " HTTP/1.1\r\nHost: " + endpoint.host + "\r\nConnection: close\r\nAccept: application/json\r\n";
    if (!body.empty()) {
        payload += "Content-Type: application/json\r\nContent-Length: " +
                   std::to_string(body.size()) + "\r\n";
    }
    payload += "\r\n" + body;
    try { sendAll(fd, payload); }
    catch (...) { close(fd); throw; }
    std::string response;
    char buffer[4096];
    for (;;) {
        const auto count = recv(fd, buffer, sizeof(buffer), 0);
        if (count == 0) break;
        if (count < 0) { close(fd); throw std::runtime_error("Failed to read DVR response"); }
        response.append(buffer, static_cast<size_t>(count));
    }
    close(fd);
    const auto separator = response.find("\r\n\r\n");
    if (separator == std::string::npos) throw std::runtime_error("DVR returned invalid HTTP response");
    const auto statusStart = response.find(' ');
    if (statusStart == std::string::npos || statusStart + 4 > response.size())
        throw std::runtime_error("DVR returned invalid HTTP status");
    const int status = std::atoi(response.c_str() + statusStart + 1);
    if (status < 100 || status > 599) throw std::runtime_error("DVR returned invalid HTTP status");
    return {status, response.substr(separator + 4)};
}

std::vector<StreamProfile> HttpDvrClient::getProfiles() {
    const HttpResponse response = request("GET", "/dvr/v3.0/GetProfiles");
    if (response.status != 200)
        throw std::runtime_error("DVR GetProfiles failed (status=" + std::to_string(response.status) + ")");
    const std::string& json = response.body;
    if (SimpleJson::getInt(json, "result", 0) != 1)
        throw std::runtime_error("DVR GetProfiles returned result != 1: " + json);

    const std::string root = jsonObject(json, "GetProfilesResponse");
    const auto items = jsonArrayObjects(jsonArray(root, "Profiles"));

    std::vector<StreamProfile> profiles;
    profiles.reserve(items.size());
    for (const auto& item : items) {
        StreamProfile p;
        p.token = SimpleJson::getString(item, "token");
        if (p.token.empty()) continue;
        p.name = SimpleJson::getString(item, "Name");
        p.streamType = parseStreamType(SimpleJson::getString(item, "Type"));

        const std::string vsc = jsonObject(item, "VideoSourceConfiguration");
        p.sourceToken = SimpleJson::getString(vsc, "token");

        const std::string vec = jsonObject(item, "VideoEncoderConfiguration");
        p.videoConfig.codec = parseCodec(SimpleJson::getString(vec, "Encoding"));

        const std::string res = jsonObject(vec, "Resolution");
        p.videoConfig.resolution.width  = SimpleJson::getInt(res, "Width", RES_1080P.width);
        p.videoConfig.resolution.height = SimpleJson::getInt(res, "Height", RES_1080P.height);

        const std::string rc = jsonObject(vec, "RateControl");
        p.videoConfig.framerate = SimpleJson::getInt(rc, "FrameRateLimit", 25);
        p.videoConfig.bitrate   = SimpleJson::getInt(rc, "BitrateTarget", 4000);
        p.videoConfig.profile   = encodingProfileToString(SimpleJson::getInt(rc, "EncodingProfile", 1));

        profiles.push_back(std::move(p));
    }
    return profiles;
}

StreamUri HttpDvrClient::getStreamUri(const std::string& profileToken, StreamProtocol protocol) {
    // Media2Service::GetStreamUri luôn gọi với StreamProtocol::RTSP (protocol
    // request của client chỉ dùng riêng cho nhánh metadata stream, không đi
    // tới đây) — DVR cũng chỉ có đúng 1 loại URI RTSP thật cho video.
    (void)protocol;

    const HttpResponse response = request("GET", "/dvr/v3.0/GetProfiles");
    if (response.status != 200)
        throw std::runtime_error("DVR GetProfiles failed (status=" + std::to_string(response.status) + ")");
    const std::string& json = response.body;
    const std::string root = jsonObject(json, "GetProfilesResponse");
    const auto items = jsonArrayObjects(jsonArray(root, "Profiles"));

    for (const auto& item : items) {
        if (SimpleJson::getString(item, "token") != profileToken) continue;
        StreamUri u;
        u.uri = rewriteUriHost(SimpleJson::getString(item, "StreamUri"), config_.deviceIp);
        return u;
    }
    // Token không tồn tại trong DVR (ví dụ dynamic profile do DTT tự tạo qua
    // CreateProfile) — trả uri rỗng để Media2Service tự dựng fallback, đúng
    // hợp đồng đã có với BackendConnector (mock).
    return {};
}

SnapshotUri HttpDvrClient::getSnapshotUri(const std::string& profileToken) {
    // DVR GetSnapshot chỉ phân biệt theo video source (channel vật lý), không
    // theo profile con — tách phần số đứng đầu token ("0"/"0_sub" -> "0").
    std::size_t i = 0;
    while (i < profileToken.size() && std::isdigit(static_cast<unsigned char>(profileToken[i]))) ++i;
    if (i == 0) return {}; // token không bắt đầu bằng số -> không map được sang DVR channel

    const std::string channelId = profileToken.substr(0, i);
    const auto endpoint = parseHttpUrl(config_.baseUrl);

    SnapshotUri u;
    u.uri = "http://" + config_.deviceIp + ":" + endpoint.port +
            "/dvr/v1.0/GetSnapshot?profile=" + channelId;
    return u;
}
