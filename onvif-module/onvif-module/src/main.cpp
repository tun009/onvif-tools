#include "backend/BackendConnector.h"
#include "backend/AlvisBackendFacade.h"
#include "backend/HttpMgmtClient.h"
#include "backend/HttpDvrClient.h"
#include "config/RuntimeConfig.h"
#include "OnvifServer.h"

// gSOAP namespace table — required when compiled with -DWITH_NONAMESPACES.
// stdsoap2.h must come first to define struct Namespace.
#include "stdsoap2.h"
#include "onvif.nsmap"

#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdio>

static std::atomic<bool> g_running{true};

void signalHandler(int sig) {
    printf("\n[main] signal %d\n", sig);
    g_running = false;
}

int main(int argc, char* argv[]) {
    std::string configPath = "config/onvif.conf";
    if (argc > 1) configPath = argv[1];

    signal(SIGINT,  signalHandler);
    signal(SIGTERM, signalHandler);

    RuntimeConfig cfg = loadRuntimeConfig(configPath);

    printf("==============================================\n");
    printf("  ONVIF Module\n");
    printf("  Device : %s:%d\n", cfg.deviceIp.c_str(), cfg.httpPort);
    printf("  Backend mode: %s\n", toString(cfg.backendMode));
    printf("  MGMT: %s\n", cfg.mgmtBaseUrl.c_str());
    printf("  DVR : %s\n", cfg.dvrBaseUrl.c_str());
    printf("  Startup smoke tests: %s\n", cfg.runSmokeTests ? "enabled" : "disabled");
    printf("  WS-Discovery: %s\n", cfg.discoveryEnabled ? "enabled" : "disabled");
    printf("==============================================\n");

    CameraBackendPtr mockBackend;
    std::shared_ptr<BackendConnector> mockConnector;
    if (cfg.requiresMockBackend()) {
        printf("[main] Connecting to mock backend...\n");
        mockConnector = std::make_shared<BackendConnector>(cfg.ctrlSocket, cfg.evtSocket);
        if (!mockConnector->connect()) {
            mockConnector.reset();
            if (cfg.mockRequired) {
                fprintf(stderr, "[main] Failed to connect to required mock backend!\n");
                return 1;
            }
            fprintf(stderr,
                    "[main] Mock backend unavailable; continuing because "
                    "backend.mock_required=false. Mock-routed operations will return SOAP faults.\n");
        } else {
            mockBackend = mockConnector;
        }
    }

    auto mgmtClient = std::make_shared<HttpMgmtClient>(MgmtClientConfig{
        cfg.mgmtBaseUrl, cfg.connectTimeoutMs, cfg.requestTimeoutMs});
    auto dvrClient = std::make_shared<HttpDvrClient>(DvrClientConfig{
        cfg.dvrBaseUrl, cfg.deviceIp, cfg.connectTimeoutMs, cfg.requestTimeoutMs});
    auto backend = std::make_shared<AlvisBackendFacade>(
        mockBackend, mgmtClient, dvrClient, cfg.backendMode, cfg.capabilities);

    // ── Optional startup smoke test ───────────────────────────────
    if (cfg.runSmokeTests) {
        printf("[main] Running startup smoke tests...\n\n");
        try {
            // Test DeviceInfo
            auto info = backend->getDeviceInfo();
            printf("[TEST] GetDeviceInfo:\n");
            printf("  Manufacturer : %s\n", info.manufacturer.c_str());
            printf("  Model        : %s\n", info.model.c_str());
            printf("  Firmware     : %s\n", info.firmwareVersion.c_str());
            printf("  Serial       : %s\n", info.serialNumber.c_str());

            // Test DateTime
            auto dt = backend->getSystemDateAndTime();
            printf("[TEST] GetSystemDateAndTime:\n");
            printf("  %d-%02d-%02d %02d:%02d:%02d UTC\n",
                   dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);

            // Test Profiles
            auto profiles = backend->getProfiles();
            printf("[TEST] GetProfiles: %zu profiles\n", profiles.size());
            for (auto& p : profiles)
                printf("  [%s] %s\n", p.token.c_str(), p.name.c_str());

            // Test StreamUri
            for (auto& p : profiles) {
                auto uri = backend->getStreamUri(p.token, StreamProtocol::RTSP);
                printf("[TEST] StreamUri [%s]: %s\n",
                       p.token.c_str(), uri.uri.c_str());
            }

            // Test PTZ
            auto ptzStatus = backend->getPtzStatus("profile_main");
            printf("[TEST] PTZ Status: pan=%.2f tilt=%.2f zoom=%.2f\n",
                   ptzStatus.position.pan,
                   ptzStatus.position.tilt,
                   ptzStatus.position.zoom);

            // Test Imaging
            auto imaging = backend->getImagingSettings("src_main");
            printf("[TEST] ImagingSettings: brightness=%.1f contrast=%.1f\n",
                   imaging.brightness, imaging.contrast);

            printf("\n[TEST] All smoke tests PASSED\n");

        } catch (const std::exception& e) {
            fprintf(stderr, "[TEST] Exception: %s\n", e.what());
        }
    } else {
        printf("[main] Startup smoke tests disabled by configuration.\n");
    }

    // ── Đọc port ONVIF/RTSP thật từ MGMT (ghi đè giá trị tĩnh onvif.conf) ──
    // MGMT sở hữu network config thật (docs/onvif-alvis/01-IMPLEMENTATION_PLAN.md,
    // mục Network configuration). Entry "ONVIF" = port SOAP web service này
    // (khác MGMT "HTTP" = port web UI MGMT); "RTSP" = port stream thật của
    // DVR. Lỗi/thiếu entry -> giữ nguyên giá trị đã đọc từ onvif.conf (fallback
    // đã chốt sẵn trong plan, không phải sự cố). Mock giữ nguyên cfg tĩnh vì
    // không có MGMT thật để tin cậy.
    if (cfg.backendMode != BackendMode::Mock) {
        try {
            const auto protocols = mgmtClient->getNetworkProtocols();
            for (const auto& p : protocols) {
                if (p.name == "ONVIF" && p.port > 0) cfg.httpPort = p.port;
                else if (p.name == "RTSP" && p.port > 0) cfg.rtspPort = p.port;
            }
            printf("[main] Port from MGMT: ONVIF=%d, RTSP=%d\n", cfg.httpPort, cfg.rtspPort);
        } catch (const std::exception& e) {
            printf("[main] MGMT GetNetworkProtocols unavailable (%s); using onvif.conf: ONVIF=%d, RTSP=%d\n",
                   e.what(), cfg.httpPort, cfg.rtspPort);
        }
    }

    // ── Start ONVIF SOAP server ───────────────────────────────────
    ServiceConfig svcCfg;
    svcCfg.deviceIp = cfg.deviceIp;
    svcCfg.httpPort = cfg.httpPort;
    svcCfg.rtspPort = cfg.rtspPort;
    svcCfg.deviceUuid = cfg.deviceUuid;
    svcCfg.username = cfg.username;
    svcCfg.password = cfg.password;
    svcCfg.useMockRtspRelay = cfg.capability("media") == CapabilityMode::Mock;

    // Mock giữ credential tĩnh để bảo toàn baseline. Hybrid/production dùng
    // MGMT làm nguồn xác thực duy nhất; không fallback admin/admin123.
    std::shared_ptr<IMgmtClient> authClient =
        cfg.backendMode == BackendMode::Mock ? nullptr : mgmtClient;
    OnvifServer server(svcCfg, backend, cfg.discoveryEnabled, authClient);
    
    printf("[main] Starting ONVIF SOAP server...\n");
    if (server.start()) {
        printf("[main] ONVIF SOAP server running on port %d\n", svcCfg.httpPort);
    } else {
        fprintf(stderr, "[main] Failed to start ONVIF SOAP server!\n");
    }

    printf("[main] Press Ctrl+C to exit\n\n");

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    printf("[main] Stopping ONVIF SOAP server...\n");
    server.stop();

    if (mockConnector) mockConnector->disconnect();
    printf("[main] Done.\n");
    return 0;
}
