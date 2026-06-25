#!/bin/bash
# scripts/test_ipc.sh
# Quick smoke test: verify IPC communication with mock backend

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
BIN="$ROOT_DIR/onvif-ipc-test"
MOCK_SOCK="/tmp/mock-camera.sock"

echo "=== ONVIF Module IPC Test ==="
echo ""

# Check binary
if [ ! -f "$BIN" ]; then
    echo "[ERROR] Binary not found: $BIN"
    echo "  Run: make ipc-test"
    exit 1
fi

# Check mock backend socket
if [ ! -S "$MOCK_SOCK" ]; then
    echo "[ERROR] Mock backend not running ($MOCK_SOCK not found)"
    echo "  Run in another terminal:"
    echo "    cd ../mock-camera-backend && ./mock-camera-server"
    exit 1
fi

echo "[OK] Mock backend socket found"
echo "[RUN] $BIN"
echo ""

"$BIN" "$ROOT_DIR/config/onvif.conf"
