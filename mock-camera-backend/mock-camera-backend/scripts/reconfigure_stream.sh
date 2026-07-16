#!/bin/bash
set -eu

PROFILE="${1:-}"; WIDTH="${2:-}"; HEIGHT="${3:-}"; FPS="${4:-}"; BITRATE="${5:-}"
case "$PROFILE" in
  profile_main) SCRIPT="start_main.sh"; PID="stream_main.pid"; DEFAULT_B=20000000 ;;
  profile_sub1) SCRIPT="start_sub1.sh"; PID="stream_sub1.pid"; DEFAULT_B=4000000 ;;
  *) echo "[ERROR] unsupported reconfigure profile: $PROFILE" >&2; exit 2 ;;
esac
case "$WIDTH:$HEIGHT:$FPS:$BITRATE" in
  *[!0-9:]*|:*|*::*) echo "[ERROR] invalid numeric stream configuration" >&2; exit 2 ;;
esac
ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
PID_FILE="$ROOT_DIR/../run/$PID"
if [ -s "$PID_FILE" ]; then
  kill "$(cat "$PID_FILE")" 2>/dev/null || true
  for _ in 1 2 3 4 5; do kill -0 "$(cat "$PID_FILE")" 2>/dev/null || break; sleep 0.2; done
fi
BITRATE="${BITRATE:-$DEFAULT_B}"
export WIDTH HEIGHT FPS BITRATE
exec bash "$ROOT_DIR/$SCRIPT"
