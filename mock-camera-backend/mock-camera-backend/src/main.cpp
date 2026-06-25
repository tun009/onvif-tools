#include "mock/MockCameraBackend.h"
#include "mock/IpcServer.h"
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>
#include <cstdio>
#include <memory>

static std::atomic<bool> g_running{true};

void signalHandler(int sig) {
    printf("\n[main] signal %d - shutting down...\n", sig);
    g_running = false;
}

int main(int argc, char* argv[]) {
    std::string configPath = "config/mock.conf";
    if (argc > 1) configPath = argv[1];

    signal(SIGINT,  signalHandler);
    signal(SIGTERM, signalHandler);

    printf("==============================================\n");
    printf("  Mock Camera Backend\n");
    printf("  Config: %s\n", configPath.c_str());
    printf("==============================================\n");

    auto backend = std::make_shared<MockCameraBackend>(configPath);

    IpcServer ipcServer(
        "/tmp/mock-camera.sock",
        "/tmp/mock-camera-evt.sock",
        backend
    );

    if (!ipcServer.start()) {
        fprintf(stderr, "[main] Failed to start IpcServer\n");
        return 1;
    }

    printf("[main] Mock server ready:\n");
    printf("  Control socket : /tmp/mock-camera.sock\n");
    printf("  Event socket   : /tmp/mock-camera-evt.sock\n");
    printf("  RTSP main      : rtsp://127.0.0.1:8554/main\n");
    printf("  RTSP sub1      : rtsp://127.0.0.1:8554/sub1\n");
    printf("  RTSP sub2      : rtsp://127.0.0.1:8554/sub2\n");
    printf("  (Run: bash scripts/start_streams.sh)\n");
    printf("Press Ctrl+C to stop.\n\n");

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    printf("[main] Stopping...\n");
    ipcServer.stop();
    printf("[main] Done.\n");
    return 0;
}
