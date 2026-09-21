#pragma once

#include <map>
#include <string>

enum class BackendMode { Mock, Hybrid, Production };
enum class CapabilityMode { Mock, Real };

struct RuntimeConfig {
    std::string deviceIp = "192.168.1.100";
    int httpPort = 8080;
    int rtspPort = 8554;
    std::string username = "admin";
    std::string password = "admin123";
    std::string ctrlSocket = "/tmp/mock-camera.sock";
    std::string evtSocket = "/tmp/mock-camera-evt.sock";
    std::string deviceUuid = "12345678-1234-1234-1234-123456789abc";
    BackendMode backendMode = BackendMode::Mock;
    std::string mgmtBaseUrl = "http://127.0.0.1:8086";
    std::string dvrBaseUrl = "http://127.0.0.1:8200";
    int connectTimeoutMs = 1000;
    int requestTimeoutMs = 3000;
    int retryIntervalMs = 2000;
    bool mockRequired = true;
    bool runSmokeTests = true;
    bool discoveryEnabled = true;
    std::map<std::string, CapabilityMode> capabilities;

    CapabilityMode capability(const std::string& name) const;
    bool requiresMockBackend() const;
};

RuntimeConfig loadRuntimeConfig(const std::string& path);
const char* toString(BackendMode mode);
