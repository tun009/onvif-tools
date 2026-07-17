#!/bin/bash
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
LOG_DIR="$ROOT_DIR/logs"; PID_DIR="$ROOT_DIR/run"
PID_FILE="$PID_DIR/stream_main.pid"
mkdir -p "$LOG_DIR" "$PID_DIR"
[ -f "$PID_FILE" ] && kill $(cat "$PID_FILE") 2>/dev/null; sleep 0.5
WIDTH="${WIDTH:-3840}"; HEIGHT="${HEIGHT:-2160}"; FPS="${FPS:-30}"; BITRATE="${BITRATE:-20000000}"
echo "[INFO] Starting main: ${WIDTH}x${HEIGHT} ${FPS}fps..."
if ! gst-inspect-1.0 rtspclientsink >/dev/null 2>&1 && command -v ffmpeg >/dev/null 2>&1; then
  echo "[INFO] rtspclientsink unavailable; using ffmpeg/libx264"
  ffmpeg -hide_banner -loglevel warning -re \
    -f lavfi -i "testsrc2=size=${WIDTH}x${HEIGHT}:rate=${FPS}" \
    -c:v libx264 -preset ultrafast -tune zerolatency -b:v "${BITRATE}" \
    -g "$((FPS * 2))" -pix_fmt yuv420p -f rtsp -rtsp_transport tcp \
    "rtsp://mock:mock123@127.0.0.1:8554/main" \
    > "$LOG_DIR/stream_main.log" 2>&1 &
  echo $! > "$PID_FILE"
  echo "[INFO] main started (pid=$(cat $PID_FILE))"
  exit 0
fi
if gst-inspect-1.0 nvv4l2h264enc >/dev/null 2>&1; then
  ENCODER_ARGS=(nvv4l2h264enc bitrate=${BITRATE} profile=High preset-level=1 insert-sps-pps=true iframeinterval=30)
else
  echo "[INFO] nvv4l2h264enc unavailable; using openh264enc"
  ENCODER_ARGS=(openh264enc bitrate=${BITRATE} rate-control=bitrate gop-size=30)
fi
gst-launch-1.0 -e \
  videotestsrc pattern=ball num-buffers=-1 \
  ! "video/x-raw,width=${WIDTH},height=${HEIGHT},framerate=${FPS}/1,format=I420" \
  ! "${ENCODER_ARGS[@]}" \
  ! "video/x-h264,stream-format=byte-stream" \
  ! h264parse \
  ! rtspclientsink location="rtsp://mock:mock123@127.0.0.1:8554/main" protocols=tcp \
  > "$LOG_DIR/stream_main.log" 2>&1 &
echo $! > "$PID_FILE"
echo "[INFO] main started (pid=$(cat $PID_FILE))"
