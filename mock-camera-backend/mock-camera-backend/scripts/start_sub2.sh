#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
LOG_DIR="$ROOT_DIR/logs"; PID_DIR="$ROOT_DIR/run"
PID_FILE="$PID_DIR/stream_sub2.pid"
mkdir -p "$LOG_DIR" "$PID_DIR"
[ -f "$PID_FILE" ] && kill $(cat "$PID_FILE") 2>/dev/null; sleep 0.5
echo "[INFO] Starting sub2: 480p H265 10fps..."
gst-launch-1.0 -e \
  videotestsrc pattern=circular num-buffers=-1 \
  ! "video/x-raw,width=640,height=480,framerate=10/1,format=I420" \
  ! nvv4l2h265enc bitrate=512000 preset-level=1 \
    insert-sps-pps=true iframeinterval=30 \
  ! "video/x-h265,stream-format=byte-stream" \
  ! h265parse \
  ! rtspclientsink location="rtsp://mock:mock123@127.0.0.1:8554/sub2" protocols=tcp \
  > "$LOG_DIR/stream_sub2.log" 2>&1 &
echo $! > "$PID_FILE"
echo "[INFO] sub2 started (pid=$(cat $PID_FILE))"
