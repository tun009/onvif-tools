#include "config/RuntimeConfig.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace {
std::string trim(std::string value) {
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

BackendMode parseBackendMode(const std::string& value) {
    if (value == "hybrid") return BackendMode::Hybrid;
    if (value == "production") return BackendMode::Production;
    return BackendMode::Mock;
}

bool parseBool(std::string value, bool fallback) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (value == "true" || value == "yes" || value == "on" || value == "1") return true;
    if (value == "false" || value == "no" || value == "off" || value == "0") return false;
    return fallback;
}
}

CapabilityMode RuntimeConfig::capability(const std::string& name) const {
    const auto found = capabilities.find(name);
    return found == capabilities.end() ? CapabilityMode::Mock : found->second;
}

bool RuntimeConfig::requiresMockBackend() const {
    if (backendMode == BackendMode::Mock) return true;
    for (const auto& item : capabilities)
        if (item.second == CapabilityMode::Mock) return true;
    return false;
}

RuntimeConfig loadRuntimeConfig(const std::string& path) {
    RuntimeConfig config;
    FILE* file = fopen(path.c_str(), "r");
    if (!file) {
        fprintf(stderr, "[RuntimeConfig] Cannot read %s; using safe mock defaults\n", path.c_str());
        return config;
    }
    std::string section;
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        std::string text = trim(line);
        if (text.empty() || text[0] == '#' || text[0] == ';') continue;
        if (text.front() == '[' && text.back() == ']') { section = trim(text.substr(1, text.size() - 2)); continue; }
        const auto separator = text.find('=');
        if (separator == std::string::npos) continue;
        const std::string key = trim(text.substr(0, separator));
        const std::string value = trim(text.substr(separator + 1));
        if (section == "server") {
            if (key == "device_ip") config.deviceIp = value;
            else if (key == "http_port") config.httpPort = std::atoi(value.c_str());
            else if (key == "rtsp_port") config.rtspPort = std::atoi(value.c_str());
            else if (key == "device_uuid") config.deviceUuid = value;
        } else if (section == "auth") {
            if (key == "username") config.username = value;
            else if (key == "password") config.password = value;
        } else if (section == "backend") {
            if (key == "mode") config.backendMode = parseBackendMode(value);
            else if (key == "ctrl_socket") config.ctrlSocket = value;
            else if (key == "evt_socket") config.evtSocket = value;
            else if (key == "mgmt_base_url") config.mgmtBaseUrl = value;
            else if (key == "connect_timeout_ms") config.connectTimeoutMs = std::atoi(value.c_str());
            else if (key == "request_timeout_ms") config.requestTimeoutMs = std::atoi(value.c_str());
            else if (key == "retry_interval_ms") config.retryIntervalMs = std::atoi(value.c_str());
            else if (key == "mock_required") config.mockRequired = parseBool(value, config.mockRequired);
        } else if (section == "startup") {
            if (key == "run_smoke_tests") config.runSmokeTests = parseBool(value, config.runSmokeTests);
        } else if (section == "capabilities") {
            config.capabilities[key] = value == "real" ? CapabilityMode::Real : CapabilityMode::Mock;
        } else if (section == "discovery") {
            if (key == "enable") config.discoveryEnabled = parseBool(value, config.discoveryEnabled);
        }
    }
    fclose(file);
    return config;
}

const char* toString(BackendMode mode) {
    switch (mode) { case BackendMode::Mock: return "mock"; case BackendMode::Hybrid: return "hybrid"; case BackendMode::Production: return "production"; }
    return "unknown";
}
