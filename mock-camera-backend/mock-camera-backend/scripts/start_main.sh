#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
LOG_DIR="$ROOT_DIR/logs"; PID_DIR="$ROOT_DIR/run"
PID_FILE="$PID_DIR/stream_main.pid"
mkdir -p "$LOG_DIR" "$PID_DIR"
[ -f "$PID_FILE" ] && kill $(cat "$PID_FILE") 2>/dev/null; sleep 0.5
echo "[INFO] Starting main: 4K H264 30fps..."
gst-launch-1.0 -e \
  videotestsrc pattern=ball num-buffers=-1 \
  ! "video/x-raw,width=3840,height=2160,framerate=30/1,format=I420" \
  ! nvv4l2h264enc bitrate=20000000 profile=High preset-level=1 \
    insert-sps-pps=true iframeinterval=30 \
  ! "video/x-h264,stream-format=byte-stream" \
  ! h264parse \
  ! rtspclientsink location="rtsp://mock:mock123@127.0.0.1:8554/main" protocols=tcp \
  > "$LOG_DIR/stream_main.log" 2>&1 &
echo $! > "$PID_FILE"
echo "[INFO] main started (pid=$(cat $PID_FILE))"
