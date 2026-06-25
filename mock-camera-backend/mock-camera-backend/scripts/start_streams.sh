#!/bin/bash
set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
LOG_DIR="$ROOT_DIR/logs"
PID_DIR="$ROOT_DIR/run"
MEDIAMTX="$ROOT_DIR/bin/mediamtx"

mkdir -p "$LOG_DIR" "$PID_DIR"

echo "[INFO] Checking nvv4l2 plugins..."
gst-inspect-1.0 nvv4l2h264enc &>/dev/null || { echo "[ERROR] nvv4l2h264enc not found"; exit 1; }
gst-inspect-1.0 nvv4l2h265enc &>/dev/null || { echo "[ERROR] nvv4l2h265enc not found"; exit 1; }

echo "[INFO] Starting MediaMTX..."
if pgrep -x mediamtx > /dev/null; then
    echo "[INFO] MediaMTX already running"
else
    [ -f "$MEDIAMTX" ] || { echo "[ERROR] mediamtx not found: run scripts/install_deps.sh"; exit 1; }
    "$MEDIAMTX" "$ROOT_DIR/rtsp/mediamtx.yml" > "$LOG_DIR/mediamtx.log" 2>&1 &
    echo $! > "$PID_DIR/mediamtx.pid"
    sleep 2
    echo "[INFO] MediaMTX started (pid=$(cat $PID_DIR/mediamtx.pid))"
fi

bash "$SCRIPT_DIR/start_main.sh"
bash "$SCRIPT_DIR/start_sub1.sh"
bash "$SCRIPT_DIR/start_sub2.sh"

echo ""
echo "[INFO] All streams started:"
echo "  rtsp://127.0.0.1:8554/main  (4K   H264 30fps)"
echo "  rtsp://127.0.0.1:8554/sub1  (1080p H264 15fps)"
echo "  rtsp://127.0.0.1:8554/sub2  (480p  H265 10fps)"
