# Chi tiết ONVIF Services cần implement

> 📅 Cập nhật: 25/06/2026  
> 🎯 Mục tiêu: Hiểu từng service và biết cần implement gì

> [!IMPORTANT]
> **PHẠM VI PHÁT TRIỂN HIỆN TẠI (CHỈ IMPLEMENT DEVICESERVICE)**
> Theo yêu cầu mới nhất, dự án sẽ chỉ tập trung hoàn thiện **DeviceService** trước để kiểm tra tính năng kết nối cơ bản, cấu hình thông tin thiết bị, mạng và xác thực.
> Tất cả các services khác như **Media2Service, PtzService, ImagingService, EventService, AnalyticsService** tạm thời được bỏ qua và đưa vào trạng thái trì hoãn (Postponed).

---

## Tổng quan các Services

```
Endpoint                    Service              Status
/onvif/device          →  DeviceService        🔵 Cần implement
/onvif/media           →  Media2Service        🔵 Cần implement
/onvif/ptz             →  PtzService           🔵 Cần implement
/onvif/imaging         →  ImagingService       🔵 Cần implement
/onvif/events          →  EventService         🔵 Cần implement (phức tạp)
/onvif/analytics       →  AnalyticsService     🔵 Cần implement
UDP :3702              →  DiscoveryService     🔵 Cần implement
```

---

## 1. DeviceService — Bắt đầu từ đây

**URL**: `http://<ip>:8080/onvif/device`  
**Namespace**: `http://www.onvif.org/ver10/device/wsdl`  
**File cần tạo**: `src/services/DeviceService.cpp`

### Các function cần implement theo thứ tự ưu tiên:

```
Priority 1: Test tool có thể detect device
├── GetSystemDateAndTime    ← Không cần auth, VMS gọi đầu tiên để sync time
├── GetDeviceInformation    ← Manufacturer, Model, Firmware, Serial
├── GetCapabilities         ← URLs của tất cả services
├── GetServices             ← Danh sách services (Profile T)
└── GetScopes               ← ONVIF scopes (type, profile, hardware...)

Priority 2: User management
├── GetUsers
├── CreateUsers
├── DeleteUsers
└── SetUser

Priority 3: Network info
├── GetNetworkInterfaces
├── GetNetworkProtocols
└── GetHostname

Priority 4: System ops
├── SystemReboot
└── SetSystemFactoryDefault
```

### Ví dụ implement GetDeviceInformation:

```cpp
int DeviceService::GetDeviceInformation(
        _tds__GetDeviceInformation*         req,
        _tds__GetDeviceInformationResponse* resp)
{
    // 1. Validate WS-Security auth
    if (!wsseHandler_->validate(soap))
        return soap_sender_fault(soap, "Not authorized", nullptr);

    // 2. Lấy data từ backend (qua IPC → MockCameraBackend)
    DeviceInfo info = backend_->getDeviceInfo();

    // 3. Map vào SOAP response struct
    resp->Manufacturer    = soap_strdup(soap, info.manufacturer.c_str());
    resp->Model           = soap_strdup(soap, info.model.c_str());
    resp->FirmwareVersion = soap_strdup(soap, info.firmwareVersion.c_str());
    resp->SerialNumber    = soap_strdup(soap, info.serialNumber.c_str());
    resp->HardwareId      = soap_strdup(soap, info.hardwareId.c_str());

    return SOAP_OK;
}
```

### SOAP response sẽ trông như thế này:

```xml
<tds:GetDeviceInformationResponse>
  <tds:Manufacturer>MockCam Inc.</tds:Manufacturer>
  <tds:Model>MockCam-4K</tds:Model>
  <tds:FirmwareVersion>1.0.0</tds:FirmwareVersion>
  <tds:SerialNumber>MOCK-001-2024</tds:SerialNumber>
  <tds:HardwareId>JetsonOrinNX-8GB</tds:HardwareId>
</tds:GetDeviceInformationResponse>
```

---

## 2. Media2Service — Quan trọng nhất cho streaming

**URL**: `http://<ip>:8080/onvif/media`  
**Namespace**: `http://www.onvif.org/ver20/media/wsdl`  
**File cần tạo**: `src/services/Media2Service.cpp`

### Các function cần implement:

```
GetProfiles                     ← Danh sách profiles (4K, 1080p, 480p)
GetStreamUri                    ← RTSP URL cho từng profile
GetSnapshotUri                  ← HTTP URL cho snapshot
GetVideoSourceConfigurations    ← Thông số nguồn video
GetVideoEncoderConfigurations   ← Thông số encoder
SetVideoEncoderConfiguration    ← Đặt bitrate, resolution...
GetCompatibleVideoEncoderConfigurations
```

### Luồng quan trọng nhất:

```
VMS hỏi GetProfiles
  → trả về: profile_main, profile_sub1, profile_sub2

VMS hỏi GetStreamUri(profileToken="profile_main")
  → trả về: rtsp://192.168.1.100:8554/main

VMS kết nối RTSP rtsp://192.168.1.100:8554/main
  → Xem được video test pattern từ GStreamer
```

### Ví dụ implement GetStreamUri:

```cpp
int Media2Service::GetStreamUri(
        _tr2__GetStreamUri*         req,
        _tr2__GetStreamUriResponse* resp)
{
    if (!wsseHandler_->validate(soap))
        return soap_sender_fault(soap, "Not authorized", nullptr);

    // Lấy URI từ backend
    StreamUri uri = backend_->getStreamUri(
        req->ProfileToken,
        StreamProtocol::RTSP
    );

    resp->Uri = soap_strdup(soap, uri.uri.c_str());
    return SOAP_OK;
}
```

---

## 3. PtzService — PTZ Control

**URL**: `http://<ip>:8080/onvif/ptz`  
**Namespace**: `http://www.onvif.org/ver20/ptz/wsdl`  
**File cần tạo**: `src/services/PtzService.cpp`

### Các function cần implement:

```
GetNodes            ← Mô tả khả năng PTZ (range pan/tilt/zoom)
GetConfigurations   ← Cấu hình PTZ đang dùng
AbsoluteMove        ← Di chuyển đến vị trí tuyệt đối
RelativeMove        ← Di chuyển tương đối
ContinuousMove      ← Di chuyển liên tục theo velocity
Stop                ← Dừng PTZ
GetStatus           ← Lấy vị trí hiện tại
GotoHomePosition    ← Về vị trí home
SetHomePosition     ← Đặt vị trí home
```

### Coordinate system của PTZ:

```
Pan  : -1.0 (trái) ←──── 0.0 ────→ +1.0 (phải)
Tilt : -1.0 (xuống) ←─── 0.0 ────→ +1.0 (lên)
Zoom :  0.0 (wide) ──────────────→ +1.0 (tele)
```

---

## 4. ImagingService — Cài đặt ảnh

**URL**: `http://<ip>:8080/onvif/imaging`  
**Namespace**: `http://www.onvif.org/ver20/imaging/wsdl`  
**File cần tạo**: `src/services/ImagingService.cpp`

### Các function cần implement:

```
GetImagingSettings  ← brightness, contrast, saturation, sharpness
SetImagingSettings  ← Đặt các thông số trên
GetStatus           ← Focus status, position
```

---

## 5. EventService — Events & Notifications (Phức tạp nhất)

**URL**: `http://<ip>:8080/onvif/events`  
**Namespace**: `http://www.onvif.org/ver10/events/wsdl`  
**File cần tạo**: `src/services/EventService.cpp`

### Cơ chế hoạt động:

ONVIF Events dùng **WS-Notification** — có 2 mode:
1. **Subscribe/Push**: Camera push event đến VMS (phức tạp)
2. **PullPoint**: VMS poll (kéo) event từ camera (đơn giản hơn)

```
Mode PullPoint (đơn giản):
VMS → CreatePullPointSubscription → Camera tạo queue
VMS → PullMessages (polling)      → Camera trả events từ queue
Camera phát hiện motion → đẩy vào queue

Mode Push (đầy đủ):
VMS gửi: Subscribe(consumerAddress="http://vms-ip:port/notify")
Camera phát hiện motion → POST đến http://vms-ip:port/notify
```

### Mock events đã có sẵn:

MockEventGenerator đã generate fake events định kỳ:
- Motion detected: mỗi 10 giây
- Object detected: mỗi 15 giây  
- Tamper detected: mỗi 30 giây

### Event format (WS-Topic):

```
tns1:VideoAnalytics/MotionAlarm       ← Motion detection
tns1:VideoAnalytics/ObjectInField     ← Object in field
tns1:VideoSource/Tampering            ← Camera tamper
```

---

## 6. AnalyticsService

**URL**: `http://<ip>:8080/onvif/analytics`  
**Namespace**: `http://www.onvif.org/ver20/analytics/wsdl`  
**File cần tạo**: `src/services/AnalyticsService.cpp`

### Các function:

```
GetSupportedAnalyticsModules  ← Liệt kê module AI (motion, tamper...)
GetAnalyticsModules           ← Module đang được enable
GetRules                      ← Rules cấu hình (vùng detection...)
AddRule                       ← Thêm rule
DeleteRule                    ← Xóa rule
```

---

## 7. DiscoveryService — WS-Discovery

**Protocol**: UDP Multicast  
**Port**: 3702 UDP  
**File cần tạo**: `src/services/DiscoveryService.cpp`

### Cơ chế:

```
1. VMS broadcast UDP đến 239.255.255.250:3702
   Content: <wsdd:Probe><Types>dn:NetworkVideoTransmitter</Types></wsdd:Probe>

2. Camera lắng nghe UDP, khi nhận Probe → reply:
   Content: <wsdd:ProbeMatches>
              <wsdd:ProbeMatch>
                <wsa5:EndpointReference>
                  <wsa5:Address>urn:uuid:12345678-...</wsa5:Address>
                </wsa5:EndpointReference>
                <wsdd:Types>dn:NetworkVideoTransmitter tds:Device</wsdd:Types>
                <wsdd:Scopes>onvif://www.onvif.org/Profile/T ...</wsdd:Scopes>
                <wsdd:XAddrs>http://192.168.1.100:8080/onvif/device</wsdd:XAddrs>
              </wsdd:ProbeMatch>
            </wsdd:ProbeMatches>
```

---

## Thứ tự implement được đề xuất

```
Week 1: Phase 1 - IPC test pass
  ✅ mock-camera-backend build OK
  ✅ onvif-ipc-test PASSED

Week 2: Phase 2 - gSOAP setup
  ✅ gSOAP installed + runtime files copied
  ✅ make gen thành công
  ✅ OnvifServer.cpp (SOAP accept loop)
  ✅ WsSecurityHandler.cpp (auth)

Week 3: Phase 3 - Core services
  ✅ DeviceService.cpp (GetDeviceInfo, GetCapabilities, GetServices, GetScopes, GetSystemDateAndTime)
  ✅ Media2Service.cpp (GetProfiles, GetStreamUri)
  → Test với ONVIF Device Manager lần đầu

Week 4: Phase 4 - Full services
  ✅ PtzService.cpp
  ✅ ImagingService.cpp
  ✅ DiscoveryService.cpp
  → WS-Discovery hoạt động

Week 5: Phase 5 - Events
  ✅ EventService.cpp (PullPoint mode trước)
  → Profile T conformance test
```

---

## gSOAP generated types - Naming convention

Khi gSOAP generate code từ WSDL, các type có prefix:

| Prefix | Ý nghĩa | Ví dụ |
|--------|---------|-------|
| `_tds__` | Device Service request/response | `_tds__GetDeviceInformation` |
| `_tr2__` | Media2 Service | `_tr2__GetProfiles` |
| `_tptz__` | PTZ Service | `_tptz__AbsoluteMove` |
| `_timg__` | Imaging Service | `_timg__GetImagingSettings` |
| `_tev__` | Event Service | `_tev__Subscribe` |
| `_tan__` | Analytics Service | `_tan__GetRules` |
| `tt__` | Common ONVIF types | `tt__Profile`, `tt__PTZVector` |

Ví dụ function signature sau khi generate:

```cpp
// DeviceBindingService là class base do gSOAP generate
class DeviceService : public DeviceBindingService {
public:
    // Function này override virtual function của gSOAP
    int GetDeviceInformation(
        _tds__GetDeviceInformation*         req,   // Input
        _tds__GetDeviceInformationResponse* resp   // Output
    ) override;
    
    int GetCapabilities(
        _tds__GetCapabilities*         req,
        _tds__GetCapabilitiesResponse* resp
    ) override;
    // ...
};
```
