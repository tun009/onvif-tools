# mock-camera-backend

Mock server giả lập camera hardware cho ONVIF module development.

## Architecture

```
Video path   : gst-launch → MediaMTX RTSP server
Control path : Unix Domain Socket IPC server (C++)
Events       : Periodic fake events (motion/object/tamper)
```

## Quick Start

```bash
# 1. Install dependencies
bash scripts/install_deps.sh

# 2. Build IPC server
make

# 3. Start IPC server
./mock-camera-server config/mock.conf

# 4. Start video streams (another terminal)
bash scripts/start_streams.sh

# 5. Verify
bash scripts/check_streams.sh
```

## Streams

| Stream | URL                          | Codec | Resolution | FPS | Bitrate |
|--------|------------------------------|-------|------------|-----|---------|
| main   | rtsp://127.0.0.1:8554/main   | H264  | 3840x2160  | 30  | 20Mbps  |
| sub1   | rtsp://127.0.0.1:8554/sub1   | H264  | 1920x1080  | 15  | 4Mbps   |
| sub2   | rtsp://127.0.0.1:8554/sub2   | H265  | 640x480    | 10  | 512kbps |

## IPC Sockets

| Socket                    | Purpose              |
|---------------------------|----------------------|
| /tmp/mock-camera.sock     | Control (req/resp)   |
| /tmp/mock-camera-evt.sock | Events (server push) |

## Requirements

- Ubuntu 22.04
- JetPack 5 (for nvv4l2h264enc / nvv4l2h265enc)
- g++ 11+
- nlohmann-json3-dev
- mediamtx (arm64, installed by install_deps.sh)
