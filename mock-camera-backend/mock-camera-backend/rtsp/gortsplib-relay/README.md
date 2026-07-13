# gortsplib RTSP relay

Relay ổn định các stream MediaMTX sang RTSP-over-HTTP/TCP trên port `8555` và phát metadata ONVIF trên `/metadata`.

## Build

```bash
cd ~/onvif-tools/mock-camera-backend/rtsp/gortsplib-relay
/home/tomotech/tools/go1.26.5/bin/go mod tidy
/home/tomotech/tools/go1.26.5/bin/go build -o mock-rtsp-metadata-poc .
```

## Chạy

MediaMTX phải chạy trước trên port `8554`:

```bash
nohup ./mock-rtsp-metadata-poc > /tmp/mock-rtsp-metadata-poc.log 2>&1 < /dev/null &
```

Kiểm tra:

```bash
ss -lntp | grep ':8555'
pgrep -af mock-rtsp-metadata-poc
tail -f /tmp/mock-rtsp-metadata-poc.log
```

Các path: `/main`, `/jpeg`, `/sub1`, `/sub2`, `/metadata`.

Relay tự reconnect upstream sau `EOF`. Không chạy nhiều instance cùng lúc vì port `8555`, UDP `8050` và `8051` chỉ được bind một lần.
