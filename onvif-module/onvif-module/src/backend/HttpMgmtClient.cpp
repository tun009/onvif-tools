#include "backend/HttpMgmtClient.h"
#include "utils/SimpleJson.h"
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdexcept>
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
}

HttpMgmtClient::HttpMgmtClient(MgmtClientConfig config) : config_(std::move(config)) {}

std::string HttpMgmtClient::get(const std::string& path) const {
    const auto endpoint = parseHttpUrl(config_.baseUrl); addrinfo hints{}; hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    if (getaddrinfo(endpoint.host.c_str(), endpoint.port.c_str(), &hints, &addresses) != 0) throw std::runtime_error("Cannot resolve MGMT host");
    int fd = -1;
    for (auto* address = addresses; address; address = address->ai_next) {
        fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol); if (fd < 0) continue;
        timeval connectTimeout{config_.connectTimeoutMs / 1000, (config_.connectTimeoutMs % 1000) * 1000};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &connectTimeout, sizeof(connectTimeout)); setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &connectTimeout, sizeof(connectTimeout));
        if (connect(fd, address->ai_addr, address->ai_addrlen) == 0) break; close(fd); fd = -1;
    }
    freeaddrinfo(addresses); if (fd < 0) throw std::runtime_error("Cannot connect to MGMT");
    timeval requestTimeout{config_.requestTimeoutMs / 1000, (config_.requestTimeoutMs % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &requestTimeout, sizeof(requestTimeout)); setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &requestTimeout, sizeof(requestTimeout));
    const std::string request = "GET " + endpoint.basePath + path + " HTTP/1.1\r\nHost: " + endpoint.host + "\r\nConnection: close\r\nAccept: application/json\r\n\r\n";
    if (send(fd, request.data(), request.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(request.size())) { close(fd); throw std::runtime_error("Failed to send MGMT request"); }
    std::string response; char buffer[4096];
    for (;;) { const auto count = recv(fd, buffer, sizeof(buffer), 0); if (count == 0) break; if (count < 0) { close(fd); throw std::runtime_error("Failed to read MGMT response"); } response.append(buffer, static_cast<size_t>(count)); }
    close(fd); const auto separator = response.find("\r\n\r\n");
    if (separator == std::string::npos || (response.rfind("HTTP/1.1 200", 0) != 0 && response.rfind("HTTP/1.0 200", 0) != 0)) throw std::runtime_error("MGMT returned invalid HTTP response");
    return response.substr(separator + 4);
}

DeviceInfo HttpMgmtClient::getDeviceInformation() {
    const std::string json = get("/mgmt/v1/Config/DeviceInformation");
    const auto start = json.find("\"GetDeviceInformationResponse\"");
    if (start == std::string::npos || SimpleJson::getInt(json, "result", 0) != 1) throw std::runtime_error("MGMT returned invalid DeviceInformation payload");
    const auto body = json.substr(start); DeviceInfo info;
    info.manufacturer = SimpleJson::getString(body, "Manufacturer"); info.model = SimpleJson::getString(body, "Model");
    info.firmwareVersion = SimpleJson::getString(body, "FirmwareVersion"); info.serialNumber = SimpleJson::getString(body, "SerialNumber"); info.hardwareId = SimpleJson::getString(body, "HardwareId");
    return info;
}
