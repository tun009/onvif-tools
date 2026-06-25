#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
LOG_DIR="$ROOT_DIR/logs"; PID_DIR="$ROOT_DIR/run"
PID_FILE="$PID_DIR/stream_sub1.pid"
mkdir -p "$LOG_DIR" "$PID_DIR"
[ -f "$PID_FILE" ] && kill $(cat "$PID_FILE") 2>/dev/null; sleep 0.5
echo "[INFO] Starting sub1: 1080p H264 15fps..."
gst-launch-1.0 -e \
  videotestsrc pattern=smpte num-buffers=-1 \
  ! "video/x-raw,width=1920,height=1080,framerate=15/1,format=I420" \
  ! nvv4l2h264enc bitrate=4000000 profile=Main preset-level=1 \
    insert-sps-pps=true iframeinterval=30 \
  ! "video/x-h264,stream-format=byte-stream" \
  ! h264parse \
  ! rtspclientsink location="rtsp://mock:mock123@127.0.0.1:8554/sub1" protocols=tcp \
  > "$LOG_DIR/stream_sub1.log" 2>&1 &
echo $! > "$PID_FILE"
echo "[INFO] sub1 started (pid=$(cat $PID_FILE))"
