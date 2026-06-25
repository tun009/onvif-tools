#!/bin/bash
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PID_DIR="$ROOT_DIR/run"

echo "=== Process Status ==="
for name in mediamtx stream_main stream_sub1 stream_sub2; do
    f="$PID_DIR/${name}.pid"
    if [ -f "$f" ]; then
        pid=$(cat "$f")
        kill -0 "$pid" 2>/dev/null \
            && echo "  OK  $name (pid=$pid)" \
            || echo "  DEAD $name (pid=$pid)"
    else
        echo "  --  $name (no pid file)"
    fi
done

echo ""
echo "=== RTSP Stream Check ==="
for path in main sub1 sub2; do
    url="rtsp://127.0.0.1:8554/$path"
    timeout 3 gst-launch-1.0 rtspsrc location="$url" protocols=tcp \
        latency=0 ! fakesink &>/dev/null
    rc=$?
    [ $rc -eq 0 ] || [ $rc -eq 124 ] \
        && echo "  OK  $url" \
        || echo "  FAIL $url"
done

echo ""
echo "=== IPC Socket ==="
[ -S /tmp/mock-camera.sock ]     && echo "  OK  /tmp/mock-camera.sock" \
                                 || echo "  MISSING /tmp/mock-camera.sock"
[ -S /tmp/mock-camera-evt.sock ] && echo "  OK  /tmp/mock-camera-evt.sock" \
                                 || echo "  MISSING /tmp/mock-camera-evt.sock"
