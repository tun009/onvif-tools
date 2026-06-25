# Kiến trúc chi tiết dự án ONVIF Mock

> 📅 Cập nhật: 25/06/2026

---

## 1. Tổng quan 2 repos

```
projects/
├── mock-camera-backend/     ← Repo 1: Giả lập phần cứng camera
│   └── mock-camera-backend/
│       ├── Makefile
│       ├── config/mock.conf
│       ├── include/
│       ├── src/
│       ├── scripts/         ← GStreamer + MediaMTX
│       └── rtsp/mediamtx.yml
│
└── onvif-module/            ← Repo 2: ONVIF server (SOAP)
    └── onvif-module/
        ├── Makefile
        ├── config/onvif.conf
        ├── external/gsoap/  ← gSOAP runtime + plugins
        ├── generated/       ← Code generate từ WSDL
        ├── include/
        │   ├── interface/   ← Shared types giữa 2 repos
        │   ├── services/    ← ONVIF service headers
        │   ├── auth/        ← WS-Security
        │   └── backend/     ← IPC client
        └── src/
```

---

## 2. Shared Interface — Hợp đồng giữa 2 repos

File `include/interface/ICameraBackend.h` là **interface trừu tượng** giống nhau ở cả 2 repos:

```cpp
class ICameraBackend {
public:
    // Device
    virtual DeviceInfo    getDeviceInfo()     = 0;
    virtual NetworkConfig getNetworkConfig()  = 0;

    // Media
    virtual std::vector<StreamProfile> getProfiles() = 0;
    virtual StreamUri getStreamUri(const std::string& token, 
                                   StreamProtocol proto) = 0;

    // PTZ
    virtual bool ptzAbsoluteMove(...)  = 0;
    virtual bool ptzRelativeMove(...)  = 0;
    virtual bool ptzContinuousMove(...)= 0;
    virtual bool ptzStop(...)          = 0;

    // Imaging
    virtual ImagingSettings getImagingSettings(...) = 0;
    virtual bool setImagingSettings(...)            = 0;

    // Events
    virtual bool subscribe(...)   = 0;
    virtual bool unsubscribe(...) = 0;
};
```

**MockCameraBackend** implement interface này với data cứng (hardcoded).  
**RealCameraBackend** (tương lai) sẽ implement với hardware thật.

---

## 3. Luồng dữ liệu đầy đủ

```
┌────────────────────────────────────────────────────────────────────┐
│  ONVIF Test Tool / VMS                                             │
│  (ONVIF Device Manager, onvif-zeep, ...)                           │
└──────────────────────┬─────────────────────────────────────────────┘
                       │
          ┌────────────▼───────────────────────────┐
          │  SOAP over HTTP (port 8080)             │
          │  e.g. POST http://192.168.1.100:8080/onvif/device
          └────────────┬───────────────────────────┘
                       │
┌──────────────────────▼─────────────────────────────────────────────┐
│  ONVIF Module (onvif-module)                                        │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  OnvifServer (gSOAP)                                        │   │
│  │  - Bind port 8080                                           │   │
│  │  - Accept HTTP connections                                  │   │
│  │  - Parse SOAP XML                                           │   │
│  │  - Validate WS-Security (UsernameToken)                     │   │
│  │  - Route to service by URL path:                            │   │
│  │      /onvif/device    → DeviceService                       │   │
│  │      /onvif/media     → Media2Service                       │   │
│  │      /onvif/ptz       → PtzService                          │   │
│  │      /onvif/imaging   → ImagingService                      │   │
│  │      /onvif/events    → EventService                        │   │
│  │      /onvif/analytics → AnalyticsService                    │   │
│  └────────────────────────────┬────────────────────────────────┘   │
│                                │ gọi ICameraBackend                 │
│  ┌─────────────────────────────▼──────────────────────────────┐   │
│  │  BackendConnector (IPC Client)                              │   │
│  │  - Implements ICameraBackend                                │   │
│  │  - Serialize request → JSON → Unix Socket                   │   │
│  │  - Deserialize response JSON → C++ types                    │   │
│  └─────────────────────────────┬──────────────────────────────┘   │
└─────────────────────────────────┼──────────────────────────────────┘
                                  │
          ┌───────────────────────▼────────────────────────────┐
          │  Unix Domain Socket  /tmp/mock-camera.sock          │
          │  Wire protocol: 16-byte header + JSON payload        │
          └───────────────────────┬────────────────────────────┘
                                  │
┌─────────────────────────────────▼──────────────────────────────────┐
│  Mock Camera Backend (mock-camera-backend)                          │
│                                                                      │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │  IpcServer                                                    │  │
│  │  - Listen Unix Socket                                         │  │
│  │  - Dispatch request → MockCameraBackend                       │  │
│  └───────────────────────────────┬────────────────────────────┘  │
│                                   │                                  │
│  ┌───────────────────────────────▼────────────────────────────┐  │
│  │  MockCameraBackend                                           │  │
│  │  - getDeviceInfo() → hardcoded                              │  │
│  │  - getProfiles()   → 3 profiles (main/sub1/sub2)            │  │
│  │  - getStreamUri()  → rtsp://127.0.0.1:8554/...             │  │
│  │  - PTZ state       → in-memory simulation                  │  │
│  │  - Events          → periodic fake (motion/tamper/object)   │  │
│  └─────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────┘

Video path (riêng biệt):
┌──────────────┐    gst-launch    ┌─────────────────┐    RTSP    ┌──────────┐
│ videotestsrc │ ───────────────▶ │ MediaMTX :8554  │ ─────────▶ │ VMS/VLC  │
│ + nvv4l2enc  │                  │ /main /sub1 /sub2│           └──────────┘
└──────────────┘                  └─────────────────┘
```

---

## 4. IPC Protocol chi tiết

### Wire format (16-byte header)

```
Byte 0-3  : MAGIC   = 0x4F4E5646 ("ONVF")
Byte 4    : VERSION = 0x01
Byte 5    : MSG_TYPE (xem bảng bên dưới)
Byte 6-7  : FLAGS   = 0x0000
Byte 8-11 : REQUEST_ID (tăng dần)
Byte 12-15: PAYLOAD_LEN (độ dài JSON)
Byte 16+  : JSON payload
```

### Message types

| Hex | Tên | Mô tả |
|-----|-----|-------|
| 0x01 | REQ_GET_DEVICE_INFO | Lấy thông tin thiết bị |
| 0x02 | REQ_GET_NETWORK_CONFIG | Lấy config mạng |
| 0x10 | REQ_GET_PROFILES | Lấy danh sách profile |
| 0x11 | REQ_GET_STREAM_URI | Lấy RTSP URI |
| 0x12 | REQ_SET_VIDEO_CONFIG | Đặt cấu hình encoder |
| 0x20 | REQ_PTZ_ABSOLUTE_MOVE | Di chuyển PTZ tuyệt đối |
| 0x21 | REQ_PTZ_RELATIVE_MOVE | Di chuyển PTZ tương đối |
| 0x22 | REQ_PTZ_CONTINUOUS_MOVE | Di chuyển PTZ liên tục |
| 0x23 | REQ_PTZ_STOP | Dừng PTZ |
| 0x30 | REQ_GET_IMAGING_SETTINGS | Lấy cài đặt ảnh |
| 0x50 | REQ_SUBSCRIBE_EVENTS | Đăng ký nhận events |
| 0xA0 | RESP_OK | Response thành công |
| 0xA1 | RESP_ERROR | Response lỗi |
| 0xB0 | EVT_MOTION_DETECTED | Event: phát hiện chuyển động |
| 0xB1 | EVT_TAMPER_DETECTED | Event: tamper |
| 0xB2 | EVT_OBJECT_DETECTED | Event: phát hiện vật thể |

### Ví dụ request/response

```json
// Request: REQ_GET_PROFILES
// Payload: {}

// Response: RESP_OK
{
  "profiles": [
    {
      "token": "profile_main",
      "name": "Main Stream 4K",
      "streamType": 0,
      "videoConfig": {
        "codec": 0,        // 0=H264, 1=H265
        "width": 3840, "height": 2160,
        "framerate": 30, "bitrate": 20000,
        "profile": "High"
      },
      "sourceToken": "src_main"
    },
    ...
  ]
}
```

---

## 5. GStreamer Pipelines

### Main stream (4K H264)
```bash
videotestsrc pattern=ball motion=sweep
  → video/x-raw, width=3840, height=2160, framerate=30/1
  → nvv4l2h264enc bitrate=20000000 profile=High
  → h264parse
  → rtspclientsink location=rtsp://mock:mock123@127.0.0.1:8554/main
```

### Sub1 (1080p H264)
```bash
videotestsrc pattern=smpte
  → video/x-raw, width=1920, height=1080, framerate=15/1
  → nvv4l2h264enc bitrate=4000000
  → h264parse
  → rtspclientsink location=rtsp://mock:mock123@127.0.0.1:8554/sub1
```

### Sub2 (480p H265)
```bash
videotestsrc pattern=circular
  → video/x-raw, width=640, height=480, framerate=10/1
  → nvv4l2h265enc bitrate=512000
  → h265parse
  → rtspclientsink location=rtsp://mock:mock123@127.0.0.1:8554/sub2
```

**videotestsrc patterns**: `ball` (quả bóng di chuyển), `smpte` (ô màu chuẩn), `circular` (sóng tròn)

---

## 6. Các URL endpoint quan trọng

| URL | Mô tả |
|-----|-------|
| `http://<ip>:8080/onvif/device` | Device Management Service |
| `http://<ip>:8080/onvif/media` | Media2 Service |
| `http://<ip>:8080/onvif/ptz` | PTZ Service |
| `http://<ip>:8080/onvif/imaging` | Imaging Service |
| `http://<ip>:8080/onvif/events` | Event Service |
| `http://<ip>:8080/onvif/analytics` | Analytics Service |
| `rtsp://<ip>:8554/main` | RTSP Main stream (4K) |
| `rtsp://<ip>:8554/sub1` | RTSP Sub stream 1 (1080p) |
| `rtsp://<ip>:8554/sub2` | RTSP Sub stream 2 (480p) |
| `udp:239.255.255.250:3702` | WS-Discovery multicast |

---

## 7. Stack công nghệ

| Thành phần | Công nghệ | Lý do chọn |
|-----------|-----------|------------|
| SOAP engine | gSOAP | Chuẩn nhất, generate code từ WSDL |
| RTSP server | MediaMTX | Nhẹ, zero-code, hỗ trợ nhiều protocol |
| Video encode | nvv4l2h264enc | Hardware encode Jetson |
| Video pipeline | GStreamer | Standard trên Linux, hỗ trợ Jetson |
| IPC | Unix Domain Socket | Nhanh, cùng máy |
| Serialization | JSON (nlohmann) | Dễ đọc, dễ debug |
| Auth | OpenSSL | WS-Security PasswordDigest |
| Language | C++17 | Performance, Jetson ecosystem |
| Build | Makefile | Đơn giản, không cần IDE |
