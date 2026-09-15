#include "backend/HttpMgmtClient.h"
#include "utils/SimpleJson.h"
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdexcept>
#include <sstream>
#include <utility>

namespace {
struct Endpoint { std::string host; std::string port; std::string basePath; };
Endpoint parseHttpUrl(const std::string& url) {
    const std::string prefix = "http://";
    if (url.rfind(prefix, 0) != 0) throw std::runtime_error("MGMT URL must use http://");
    const std::string rest = url.substr(prefix.size()); const auto slash = rest.find('/');
    const std::string authority = rest.substr(0, slash); const auto colon = authority.rfind(':');
    Endpoint result{colon == std::string::npos ? authority : authority.substr(0, colon),
                    colon == std::string::npos ? "80" : authority.substr(colon + 1),
                    slash == std::string::npos ? "" : rest.substr(slash)};
    if (result.host.empty() || result.port.empty()) throw std::runtime_error("Invalid MGMT URL");
    return result;
}

std::string escapeJson(const std::string& value) {
    std::string output;
    output.reserve(value.size());
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (ch < 0x20) throw std::runtime_error("Invalid control character in JSON value");
            output.push_back(static_cast<char>(ch));
        }
    }
    return output;
}

void sendAll(int fd, const std::string& data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t sent = send(fd, data.data() + offset, data.size() - offset,
                                  MSG_NOSIGNAL);
        if (sent <= 0) throw std::runtime_error("Failed to send MGMT request");
        offset += static_cast<std::size_t>(sent);
    }
}
}

HttpMgmtClient::HttpMgmtClient(MgmtClientConfig config) : config_(std::move(config)) {}

HttpMgmtClient::HttpResponse HttpMgmtClient::request(
    const std::string& method, const std::string& path, const std::string& body) const {
    const auto endpoint = parseHttpUrl(config_.baseUrl); addrinfo hints{}; hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    if (getaddrinfo(endpoint.host.c_str(), endpoint.port.c_str(), &hints, &addresses) != 0) throw std::runtime_error("Cannot resolve MGMT host");
    int fd = -1;
    for (auto* address = addresses; address; address = address->ai_next) {
        fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol); if (fd < 0) continue;
        timeval connectTimeout{config_.connectTimeoutMs / 1000, (config_.connectTimeoutMs % 1000) * 1000};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &connectTimeout, sizeof(connectTimeout)); setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &connectTimeout, sizeof(connectTimeout));
        if (connect(fd, address->ai_addr, address->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(addresses); if (fd < 0) throw std::runtime_error("Cannot connect to MGMT");
    timeval requestTimeout{config_.requestTimeoutMs / 1000, (config_.requestTimeoutMs % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &requestTimeout, sizeof(requestTimeout)); setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &requestTimeout, sizeof(requestTimeout));
    std::string payload = method + " " + endpoint.basePath + path +
        " HTTP/1.1\r\nHost: " + endpoint.host + "\r\nConnection: close\r\nAccept: application/json\r\n";
    if (!body.empty()) {
        payload += "Content-Type: application/json\r\nContent-Length: " +
                   std::to_string(body.size()) + "\r\n";
    }
    payload += "\r\n" + body;
    try { sendAll(fd, payload); }
    catch (...) { close(fd); throw; }
    std::string response; char buffer[4096];
    for (;;) { const auto count = recv(fd, buffer, sizeof(buffer), 0); if (count == 0) break; if (count < 0) { close(fd); throw std::runtime_error("Failed to read MGMT response"); } response.append(buffer, static_cast<size_t>(count)); }
    close(fd); const auto separator = response.find("\r\n\r\n");
    if (separator == std::string::npos) throw std::runtime_error("MGMT returned invalid HTTP response");
    const auto statusStart = response.find(' ');
    if (statusStart == std::string::npos || statusStart + 4 > response.size())
        throw std::runtime_error("MGMT returned invalid HTTP status");
    const int status = std::atoi(response.c_str() + statusStart + 1);
    if (status < 100 || status > 599) throw std::runtime_error("MGMT returned invalid HTTP status");
    return {status, response.substr(separator + 4)};
}

DeviceInfo HttpMgmtClient::getDeviceInformation() {
    const HttpResponse response = request("GET", "/mgmt/v1/Config/DeviceInformation");
    if (response.status != 200) throw std::runtime_error("MGMT rejected DeviceInformation request");
    const std::string& json = response.body;
    const auto start = json.find("\"GetDeviceInformationResponse\"");
    if (start == std::string::npos || SimpleJson::getInt(json, "result", 0) != 1) throw std::runtime_error("MGMT returned invalid DeviceInformation payload");
    const auto body = json.substr(start); DeviceInfo info;
    info.manufacturer = SimpleJson::getString(body, "Manufacturer"); info.model = SimpleJson::getString(body, "Model");
    info.firmwareVersion = SimpleJson::getString(body, "FirmwareVersion"); info.serialNumber = SimpleJson::getString(body, "SerialNumber"); info.hardwareId = SimpleJson::getString(body, "HardwareId");
    return info;
}

OnvifAuthenticationResult HttpMgmtClient::verifyWssePasswordDigest(
    const WssePasswordDigest& credential) {
    const std::string body =
        "{\"Username\":\"" + escapeJson(credential.username) +
        "\",\"Nonce\":\"" + escapeJson(credential.nonce) +
        "\",\"Created\":\"" + escapeJson(credential.created) +
        "\",\"PasswordDigest\":\"" + escapeJson(credential.passwordDigest) + "\"}";
    const HttpResponse response = request(
        "POST", "/internal/v1/auth/onvif/wsse-password-digest", body);
    if (response.status == 200 && SimpleJson::getInt(response.body, "result", 0) == 1 &&
        SimpleJson::getBool(response.body, "Authenticated", false)) {
        return {true, SimpleJson::getString(response.body, "UserLevel")};
    }
    if (response.status == 400 || response.status == 401 || response.status == 403)
        return {};
    throw std::runtime_error("MGMT ONVIF authentication service unavailable");
}

OnvifAuthenticationResult HttpMgmtClient::verifyHttpDigest(
    const HttpDigestCredential& credential) {
    const std::string body =
        "{\"Username\":\"" + escapeJson(credential.username) +
        "\",\"Realm\":\"" + escapeJson(credential.realm) +
        "\",\"Method\":\"" + escapeJson(credential.method) +
        "\",\"Uri\":\"" + escapeJson(credential.uri) +
        "\",\"Nonce\":\"" + escapeJson(credential.nonce) +
        "\",\"Qop\":\"" + escapeJson(credential.qop) +
        "\",\"Nc\":\"" + escapeJson(credential.nc) +
        "\",\"Cnonce\":\"" + escapeJson(credential.cnonce) +
        "\",\"Algorithm\":\"" + escapeJson(credential.algorithm) +
        "\",\"Response\":\"" + escapeJson(credential.response) + "\"}";
    const HttpResponse response = request(
        "POST", "/internal/v1/auth/onvif/http-digest", body);
    if (response.status == 200 && SimpleJson::getInt(response.body, "result", 0) == 1 &&
        SimpleJson::getBool(response.body, "Authenticated", false)) {
        return {true, SimpleJson::getString(response.body, "UserLevel")};
    }
    if (response.status == 400 || response.status == 401 || response.status == 403)
        return {};
    throw std::runtime_error("MGMT ONVIF HTTP Digest service unavailable");
}
