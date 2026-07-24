#!/usr/bin/env bash
# ============================================================================
# reset_conformance_baseline.sh
# ----------------------------------------------------------------------------
# CHẠY TRƯỚC MỖI LẦN chạy full ONVIF conformance (DTT) để đưa thiết bị mock về
# TRẠNG THÁI SẠCH (baseline), giống một thiết bị vừa power-cycle.
#
# VÌ SAO CẦN: mock là process chạy dài, GIỮ state giữa các lần chạy DTT:
#   - onvif-server: override config, removed-config marker, subscription, dyn
#     profile... tích tụ trong RAM → drift so với mặc định.
#   - mediamtx: SetVideoEncoderConfiguration(jpeg/sub2) gọi patchMediamtxPath
#     đổi runOnInit → stream jpeg/sub2 bị "kẹt" ở resolution của run TRƯỚC
#     (vd jpeg tụt còn 640x480) → RTSS JPEG "Frames waiting timeout".
# DTT mỗi lần giả định thiết bị MỚI → phải reset để ổn định, không phụ thuộc
# lịch sử các run trước hay uptime dài.
#
# Cách dùng (trên server):  bash scripts/reset_conformance_baseline.sh
# ============================================================================
set -u

MTX_API="http://127.0.0.1:19997/v3/config/paths"
BACKEND_DIR="$HOME/tungdt/onvif-tools/mock-camera-backend/mock-camera-backend"
ONVIF_DIR="$HOME/tungdt/onvif-tools/onvif-module/onvif-module"

echo "== 1) Reset stream jpeg + sub2 về default (2 path bị patchMediamtxPath làm drift) =="
patch_path() {  # $1=path  $2=ffmpeg-input-args  $3=extra-encoder-args
  curl -s -o /dev/null -w "   PATCH $1 -> %{http_code}\n" \
    -X PATCH -H 'Content-Type: application/json' \
    --data "{\"runOnInit\":\"ffmpeg -re -f lavfi -i $2 $3 -f rtsp -rtsp_transport tcp rtsp://127.0.0.1:8554/$1\",\"runOnInitRestart\":true}" \
    "$MTX_API/patch/$1"
}
# jpeg = MJPEG 1920x1080@30, Huffman chuẩn (RFC 2435) — RTSS-1-1-46/53.
patch_path jpeg "testsrc2=size=1920x1080:rate=30" "-c:v mjpeg -huffman default -pix_fmt yuvj420p -q:v 5"
# sub2 = H.264 640x480@10.
patch_path sub2 "testsrc2=size=640x480:rate=10" "-c:v libx264 -preset ultrafast -tune zerolatency"
sleep 5

echo "== 2) Restart backend + onvif (xóa state RAM: override/removed-marker/subscription) =="
# Kill theo PID (pkill -f đôi khi KHÔNG diệt được — bài học vận hành).
PIDS=$(pgrep -f 'onvif-server|mock-camera-server')
[ -n "$PIDS" ] && kill -9 $PIDS
sleep 3
# ĐÚNG THỨ TỰ: backend TRƯỚC (tạo /tmp/mock-camera.sock), onvif SAU (nối IPC).
cd "$BACKEND_DIR" && setsid nohup ./mock-camera-server config/mock.conf >/tmp/mock-backend.log 2>&1 </dev/null &
sleep 4
cd "$ONVIF_DIR"   && setsid nohup ./onvif-server   config/onvif.conf >/tmp/onvif-server.log  2>&1 </dev/null &
sleep 4

echo "== 3) Verify =="
ss -ltnp 2>/dev/null | grep ':8080 ' >/dev/null && echo "   onvif :8080 OK" || echo "   !! onvif :8080 CHƯA LÊN"
for p in main:3840 sub1:1280 sub2:640 jpeg:1920; do
  path="${p%%:*}"; exp="${p##*:}"
  got=$(timeout 7 ffprobe -rtsp_transport tcp -i "rtsp://127.0.0.1:8554/$path" \
        -show_entries stream=width -of csv=p=0 2>/dev/null | tail -1)
  [ "$got" = "$exp" ] && echo "   stream $path=$got OK" || echo "   !! stream $path=$got (expect $exp)"
done
echo "== DONE — giờ có thể chạy full conformance =="
