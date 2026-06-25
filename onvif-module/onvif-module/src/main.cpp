#include "backend/BackendConnector.h"

// gSOAP namespace table — required when compiled with -DWITH_NONAMESPACES.
// stdsoap2.h must come first to define struct Namespace.
#include "stdsoap2.h"
#include "onvif.nsmap"

#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdio>
#include <cstring>

static std::atomic<bool> g_running{true};

void signalHandler(int sig) {
    printf("\n[main] signal %d\n", sig);
    g_running = false;
}

// ── Config parser (simple) ────────────────────────────────────────────────
struct Config {
    std::string deviceIp    = "192.168.1.100";
    int         httpPort    = 8080;
    int         rtspPort    = 8554;
    std::string username    = "admin";
    std::string password    = "admin123";
    std::string ctrlSocket  = "/tmp/mock-camera.sock";
    std::string evtSocket   = "/tmp/mock-camera-evt.sock";
    std::string deviceUuid  = "12345678-1234-1234-1234-123456789abc";
};

Config loadConfig(const std::string& path) {
    Config cfg;
    FILE* f = fopen(path.c_str(), "r");
    if (!f) {
        printf("[WARN] Config not found: %s, using defaults\n", path.c_str());
        return cfg;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        char key[128], val[128];
        if (sscanf(line, " %127[^= ] = %127[^\n]", key, val) != 2) continue;
        if (!strcmp(key, "device_ip"))    cfg.deviceIp   = val;
        if (!strcmp(key, "http_port"))    cfg.httpPort   = atoi(val);
        if (!strcmp(key, "rtsp_port"))    cfg.rtspPort   = atoi(val);
        if (!strcmp(key, "username"))     cfg.username   = val;
        if (!strcmp(key, "password"))     cfg.password   = val;
        if (!strcmp(key, "ctrl_socket"))  cfg.ctrlSocket = val;
        if (!strcmp(key, "evt_socket"))   cfg.evtSocket  = val;
        if (!strcmp(key, "device_uuid"))  cfg.deviceUuid = val;
    }
    fclose(f);
    return cfg;
}

int main(int argc, char* argv[]) {
    std::string configPath = "config/onvif.conf";
    if (argc > 1) configPath = argv[1];

    signal(SIGINT,  signalHandler);
    signal(SIGTERM, signalHandler);

    Config cfg = loadConfig(configPath);

    printf("==============================================\n");
    printf("  ONVIF Module\n");
    printf("  Device : %s:%d\n", cfg.deviceIp.c_str(), cfg.httpPort);
    printf("  Backend: %s\n", cfg.ctrlSocket.c_str());
    printf("==============================================\n");

    // ── Connect to mock backend ───────────────────────────────────
    BackendConnector backend(cfg.ctrlSocket, cfg.evtSocket);

    printf("[main] Connecting to mock backend...\n");
    if (!backend.connect()) {
        fprintf(stderr, "[main] Failed to connect to backend!\n");
        fprintf(stderr, "  Make sure mock-camera-server is running:\n");
        fprintf(stderr, "  cd ../mock-camera-backend && ./mock-camera-server\n");
        return 1;
    }

    // ── Smoke test: query backend ─────────────────────────────────
    printf("[main] Backend connected. Running smoke test...\n\n");

    try {
        // Test DeviceInfo
        auto info = backend.getDeviceInfo();
        printf("[TEST] GetDeviceInfo:\n");
        printf("  Manufacturer : %s\n", info.manufacturer.c_str());
        printf("  Model        : %s\n", info.model.c_str());
        printf("  Firmware     : %s\n", info.firmwareVersion.c_str());
        printf("  Serial       : %s\n", info.serialNumber.c_str());

        // Test DateTime
        auto dt = backend.getSystemDateAndTime();
        printf("[TEST] GetSystemDateAndTime:\n");
        printf("  %d-%02d-%02d %02d:%02d:%02d UTC\n",
               dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);

        // Test Profiles
        auto profiles = backend.getProfiles();
        printf("[TEST] GetProfiles: %zu profiles\n", profiles.size());
        for (auto& p : profiles)
            printf("  [%s] %s\n", p.token.c_str(), p.name.c_str());

        // Test StreamUri
        for (auto& p : profiles) {
            auto uri = backend.getStreamUri(p.token, StreamProtocol::RTSP);
            printf("[TEST] StreamUri [%s]: %s\n",
                   p.token.c_str(), uri.uri.c_str());
        }

        // Test PTZ
        auto ptzStatus = backend.getPtzStatus("profile_main");
        printf("[TEST] PTZ Status: pan=%.2f tilt=%.2f zoom=%.2f\n",
               ptzStatus.position.pan,
               ptzStatus.position.tilt,
               ptzStatus.position.zoom);

        // Test Imaging
        auto imaging = backend.getImagingSettings("src_main");
        printf("[TEST] ImagingSettings: brightness=%.1f contrast=%.1f\n",
               imaging.brightness, imaging.contrast);

        printf("\n[TEST] All smoke tests PASSED\n");

    } catch (const std::exception& e) {
        fprintf(stderr, "[TEST] Exception: %s\n", e.what());
    }

    // ── TODO: Start ONVIF SOAP server here ────────────────────────
    // OnvifServer server(cfg, backend);
    // server.start();
    printf("\n[main] ONVIF SOAP server: TODO (gSOAP setup required)\n");
    printf("[main] Run 'make gen' first to generate gSOAP code\n");
    printf("[main] Press Ctrl+C to exit\n\n");

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    backend.disconnect();
    printf("[main] Done.\n");
    return 0;
}
