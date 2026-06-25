#!/bin/bash
ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PID_DIR="$ROOT_DIR/run"

stop_pid() {
    local f="$PID_DIR/${1}.pid"
    [ -f "$f" ] && kill $(cat "$f") 2>/dev/null && rm -f "$f" \
                 && echo "[INFO] stopped $1"
}
stop_pid stream_main
stop_pid stream_sub1
stop_pid stream_sub2
stop_pid mediamtx
echo "[INFO] Done."
