# Tổng quan ONVIF & Dự án Mock Server

> 📅 Cập nhật: 25/06/2026  
> 🎯 Đối tượng: Người mới, chưa có kinh nghiệm ONVIF

---

## 1. ONVIF là gì?

**ONVIF** (Open Network Video Interface Forum) là một **tiêu chuẩn giao tiếp mở** cho thiết bị camera IP và hệ thống giám sát video.

### Vấn đề ONVIF giải quyết

Trước ONVIF, mỗi hãng camera (Hikvision, Dahua, Axis...) có **giao thức riêng**.  
VMS (Video Management Software) muốn kết nối camera phải viết driver riêng cho từng hãng.

```
❌ Trước ONVIF:
  Camera Hikvision ──── Driver Hikvision ────┐
  Camera Dahua     ──── Driver Dahua     ────┼──── VMS
  Camera Axis      ──── Driver Axis      ────┘
  (mỗi hãng = 1 driver khác nhau)

✅ Với ONVIF:
  Camera Hikvision ──┐
  Camera Dahua     ──┼──── ONVIF Protocol ──── VMS
  Camera Axis      ──┘
  (1 chuẩn duy nhất cho tất cả)
```

---

## 2. ONVIF Profile là gì?

ONVIF chia thành các **Profile** (bộ tính năng). Mỗi profile = 1 tập hợp các tính năng bắt buộc.

| Profile | Tên | Mục đích |
|---------|-----|----------|
| **Profile S** | Streaming | Live streaming cơ bản (H.264, RTSP) |
| **Profile G** | Recording | Ghi video, playback |
| **Profile T** | Advanced Streaming | H.265, analytics, metadata streaming |
| **Profile M** | Metadata | AI analytics, object detection |
| **Profile C** | Access Control | Kiểm soát ra vào |
| **Profile A** | Access Control Adv | Nâng cao Profile C |

### Dự án này implement **Profile T** — gồm:
- ✅ Video streaming (H.264 + H.265)
- ✅ PTZ (Pan-Tilt-Zoom)
- ✅ Analytics (motion detection, object detection)
- ✅ Events (thông báo sự kiện)
- ✅ Imaging (cài đặt ảnh: độ sáng, tương phản...)
- ✅ WS-Security (xác thực)

---

## 3. Các giao thức ONVIF dùng

### 3.1 SOAP / WSDL — Lớp điều khiển

```
Client (VMS)                    Server (Camera)
     │                               │
     │  HTTP POST /onvif/device      │
     │  Content-Type: application/soap+xml
     │  <Envelope>                   │
     │    <Header>WS-Security</Header>│
     │    <Body>                     │
     │      <GetDeviceInformation/>  │
     │    </Body>                    │
     │  </Envelope>                  │
     │──────────────────────────────▶│
     │                               │
     │  <GetDeviceInformationResponse>│
     │    <Manufacturer>Hikvision</> │
     │    <Model>DS-2CD2T87...</>    │
     │  </GetDeviceInformationResponse>
     │◀──────────────────────────────│
```

- **SOAP**: Giao thức truyền XML qua HTTP
- **WSDL**: File mô tả "camera này có những lệnh gì, tham số ra sao"
- **gSOAP**: Thư viện C++ đọc WSDL và sinh code tự động

### 3.2 RTSP — Lớp video

```
Client                          RTSP Server (Camera)
     │                               │
     │  RTSP DESCRIBE rtsp://ip/main │
     │──────────────────────────────▶│
     │  200 OK + SDP info            │
     │◀──────────────────────────────│
     │                               │
     │  RTSP SETUP (chọn cổng)      │
     │──────────────────────────────▶│
     │  RTSP PLAY                    │
     │──────────────────────────────▶│
     │                               │
     │◀═══════ RTP video packets ════│  (stream liên tục)
```

- **RTSP** (Real-Time Streaming Protocol): Giao thức điều khiển stream
- **RTP** (Real-Time Transport Protocol): Truyền gói video thực tế
- **SDP** (Session Description Protocol): Mô tả thông số stream (codec, resolution...)

### 3.3 WS-Discovery — Tìm camera trên mạng

```
Client (VMS)                    Camera
     │                               │
     │  UDP Multicast (239.255.255.250:3702)
     │  "Ai là camera ONVIF?"        │
     │──────────── Broadcast ───────▶│
     │                               │
     │  "Tôi là MockCam tại http://192.168.1.100:8080/onvif/device"
     │◀──────────────────────────────│
```

---

## 4. Kiến trúc tổng thể hệ thống

### 4.1 Hệ thống camera thật (production)

```
┌─────────────────────────────────────┐
│         Jetson Orin NX              │
│                                     │
│  ┌──────────────────┐               │
│  │   ONVIF Module   │               │
│  │  (SOAP server)   │               │
│  └────────┬─────────┘               │
│           │ gọi                     │
│  ┌────────▼─────────┐               │
│  │  Camera Hardware  │              │
│  │  - ISP (xử lý ảnh)│             │
│  │  - V4L2 (video)  │              │
│  │  - nvenc (encode) │              │
│  └──────────────────┘               │
└─────────────────────────────────────┘
          ▲
          │ ONVIF (SOAP + RTSP)
          │
┌─────────┴─────────┐
│   VMS / Test Tool  │
│  (ONVIF Device Mgr)│
└────────────────────┘
```

### 4.2 Hệ thống này (development với mock)

```
┌─────────────────────────────────────────────────────────┐
│                     Jetson Orin NX                      │
│                                                         │
│  ┌──────────────────┐    Unix Socket    ┌─────────────┐ │
│  │   ONVIF Module   │◀─────────────────▶│ Mock Server │ │
│  │  (SOAP server)   │                  │             │ │
│  │  Port: 8080      │                  │ Giả lập:    │ │
│  └──────────────────┘                  │ - Device info│ │
│                                        │ - PTZ state │ │
│  ┌──────────────────┐                  │ - Events    │ │
│  │   RTSP Server    │                  └─────────────┘ │
│  │  (MediaMTX)      │                                   │
│  │  Port: 8554      │  ◀── GStreamer pipelines          │
│  │  /main  (4K H264)│      (videotestsrc + nvenc)       │
│  │  /sub1 (1080p)   │                                   │
│  │  /sub2 (480p)    │                                   │
│  └──────────────────┘                                   │
└─────────────────────────────────────────────────────────┘
          ▲
          │ ONVIF (SOAP + RTSP)
          │
┌─────────┴─────────┐
│   ONVIF Test Tool  │
│   VLC / RTSP test  │
└────────────────────┘
```

---

## 5. Luồng hoạt động cụ thể

### Khi VMS/Test Tool kết nối camera ONVIF:

```
1. DISCOVERY (tìm camera)
   VMS gửi UDP multicast "Hello, ai là ONVIF camera?"
   Camera trả về: "Tôi ở http://192.168.1.100:8080/onvif/device"

2. AUTHENTICATION (xác thực)
   VMS gửi SOAP với WS-Security header
   Camera verify username/password

3. GET DEVICE INFO (lấy thông tin)
   VMS hỏi: GetDeviceInformation
   Camera trả: manufacturer, model, firmware, serial

4. GET CAPABILITIES (lấy khả năng)
   VMS hỏi: GetCapabilities
   Camera trả: URL của từng service (Media, PTZ, Events...)

5. GET PROFILES (lấy profile stream)
   VMS hỏi: GetProfiles
   Camera trả: danh sách profile (4K, 1080p, 480p)

6. GET STREAM URI (lấy link RTSP)
   VMS hỏi: GetStreamUri(profile_main)
   Camera trả: rtsp://192.168.1.100:8554/main

7. STREAM (xem video)
   VMS kết nối RTSP và nhận video
```

---

## 6. Các khái niệm quan trọng

### 6.1 Profile Token
Mỗi cấu hình stream có 1 **token** (ID duy nhất):
```
"profile_main"  → 4K H264 30fps 20Mbps
"profile_sub1"  → 1080p H264 15fps 4Mbps  
"profile_sub2"  → 480p H265 10fps 512kbps
```
VMS dùng token để chỉ định "tôi muốn stream nào".

### 6.2 Source Token
Nguồn video (sensor):
```
"src_main"  → sensor chính
"src_sub1"  → sensor phụ 1 (thường cùng sensor, encode khác)
```

### 6.3 WS-Security / UsernameToken
ONVIF dùng WS-Security để xác thực:
```xml
<wsse:Security>
  <wsse:UsernameToken>
    <wsse:Username>admin</wsse:Username>
    <wsse:Password Type="PasswordDigest">
      base64(SHA1(nonce + created + password))
    </wsse:Password>
    <wsse:Nonce>abc123==</wsse:Nonce>
    <wsu:Created>2024-01-01T00:00:00Z</wsu:Created>
  </wsse:UsernameToken>
</wsse:Security>
```

**PasswordDigest** = Hash của (nonce + timestamp + password) — an toàn hơn plain text.

### 6.4 gSOAP
Thư viện C++ tự động sinh code từ file WSDL:
```
WSDL (mô tả API)  ──[wsdl2h]──▶  onvif.h
onvif.h           ──[soapcpp2]──▶ soapC.cpp, soapH.h, service classes
```
Kết quả: ta chỉ cần implement logic, không cần tự parse XML.

### 6.5 IPC (Inter-Process Communication)
Cơ chế giao tiếp giữa ONVIF Module và Mock Server:
```
ONVIF Module                    Mock Server
(IPC Client)                    (IPC Server)
     │                               │
     │  Unix Domain Socket           │
     │  /tmp/mock-camera.sock        │
     │──── REQ_GET_DEVICE_INFO ─────▶│
     │◀─── {"manufacturer":"..."} ───│
```
Dùng Unix Socket vì nhanh, cùng máy, dễ debug.

### 6.6 GStreamer Pipeline
Chuỗi xử lý video:
```
videotestsrc  →  nvv4l2h264enc  →  rtspclientsink
(tạo ảnh test)   (encode H264      (đẩy lên MediaMTX)
                  dùng Jetson GPU)
```

---

## 7. Danh sách file tài liệu dự án

| File | Nội dung |
|------|---------|
| `01-ONVIF-tong-quan.md` | **File này** - Tổng quan, khái niệm |
| `02-kien-truc-du-an.md` | Kiến trúc chi tiết, sơ đồ component |
| `03-huong-dan-deploy.md` | Hướng dẫn build & deploy step-by-step |
| `04-onvif-services.md` | Chi tiết từng ONVIF service cần implement |
| `05-test-guide.md` | Hướng dẫn test với ONVIF Device Manager |

---

## 8. Tóm tắt mục đích dự án

```
MỤC ĐÍCH:
Phát triển ONVIF server chạy trên camera Jetson Orin NX
→ Camera có thể kết nối với bất kỳ VMS nào hỗ trợ ONVIF

VẤN ĐỀ:
Không thể test ONVIF module trực tiếp trên hardware thật
→ Hardware camera (ISP, sensor) chưa sẵn sàng
→ Muốn test code độc lập với hardware

GIẢI PHÁP:
Tạo Mock Server giả lập hardware camera
ONVIF Module ↔ Mock Server (qua IPC)
Thay Mock Server bằng Real Camera Backend sau khi xong

KẾT QUẢ MONG MUỐN:
ONVIF Test Tool kết nối được vào ONVIF Module
→ GetDeviceInformation OK
→ GetProfiles OK  
→ GetStreamUri OK → xem được RTSP stream
→ PTZ, Events, Analytics OK
→ Profile T conformance đạt
```
