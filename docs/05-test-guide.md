# Hướng dẫn Test với ONVIF Test Tools

> 📅 Cập nhật: 25/06/2026

---

## Các tool test ONVIF

### 1. ONVIF Device Manager (ODM) — Khuyến nghị cho người mới

**Link tải**: https://sourceforge.net/projects/onvifdm/  
**OS**: Windows  
**Chi phí**: Miễn phí  
**Tính năng**: Discovery, live view, PTZ, snapshot

### 2. VLC — Test RTSP stream

**Link tải**: https://www.videolan.org/  
**OS**: Windows/Mac/Linux  
**Chi phí**: Miễn phí  
**Tính năng**: Chỉ xem RTSP stream

### 3. Python onvif-zeep — Test programmatic

```bash
pip install onvif-zeep
```
**OS**: Windows/Mac/Linux  
**Chi phí**: Miễn phí  
**Tính năng**: Gọi từng ONVIF API, kiểm tra response

### 4. ONVIF Device Test Tool (Chính thức từ ONVIF)

Đây là công cụ kiểm thử chính thức từ ONVIF dùng để đánh giá độ tương thích (conformance). Giao diện chính của tool gồm các tab:
- **Discovery**: Quét thiết bị trong mạng qua WS-Discovery và kiểm tra thông tin nhanh.
- **Management**: Quản lý thiết bị.
- **Conformance Test**: Chạy các testcase kiểm thử chuẩn hóa để lấy chứng chỉ.
- **Diagnostics & Debug**: Chẩn đoán lỗi và gỡ lỗi chi tiết.

---

## Test 0: Sử dụng ONVIF Device Test Tool (Chính thức)

### Bước 1: Chọn đúng Card mạng (NIC)
Trong mục **NIC**, hãy chọn đúng card mạng đang kết nối cùng mạng với Jetson (ví dụ card mạng ảo WSL, Ethernet hoặc Wifi có dải IP tương ứng).

### Bước 2: Quét thiết bị (Discovery)
- Nhấn **Discover Devices** để quét tự động qua mạng. Thiết bị `MockCam` khi chạy tính năng WS-Discovery sẽ xuất hiện trong danh sách kèm **IP, UUID và Hardware ID**.
- Nếu thiết bị không tự xuất hiện (khác subnet/chặn multicast), nhập IP vào ô **Device IP** rồi nhấn **Probe** để quét trực tiếp.

### Bước 3: Điền thông tin kiểm tra (Device Under Test Information)
Sau khi Probe thành công hoặc chọn từ danh sách, điền thông tin:
- **Device Service Address**: Đường dẫn endpoint, ví dụ: `http://192.168.8.114:8080/onvif/device`
- **User Name / Password**: Nhập thông tin xác thực (`admin` / `admin123`).
- Nhấn nút **Check** để lấy thông tin chi tiết. 

Kết quả mong đợi ở góc dưới:
- **Brand**: MockCam Inc.
- **Model**: MockCam-4K
- **Serial Number**: MOCK-001-2024
- **Firmware Version**: 1.0.0

---

## Test 1: RTSP Stream với VLC

### Bước 1: Đảm bảo streams đang chạy

```bash
# Trên Jetson:
bash scripts/check_streams.sh
```

### Bước 2: Mở VLC

```
Media → Open Network Stream → Nhập URL:
rtsp://192.168.8.114:8554/main    (4K)
rtsp://192.168.8.114:8554/sub1    (1080p)
rtsp://192.168.8.114:8554/sub2    (480p)
```

> **Nếu không xem được**: Đảm bảo port 8554 không bị firewall block

---

## Test 2: ONVIF Device Manager

### Bước 1: Cài đặt ODM

Tải về và cài đặt từ sourceforge.

### Bước 2: Add device thủ công

Vào **Device → Add...** (không dùng auto-discovery nếu khác subnet):
```
URI: http://192.168.8.114:8080/onvif/device
Username: admin
Password: admin123
```

### Bước 3: Kiểm tra từng tính năng

| Test | Kết quả mong đợi |
|------|-----------------|
| Connect | Device xuất hiện trong danh sách |
| Device Info | Hiện: MockCam Inc., MockCam-4K, 1.0.0 |
| Profiles | Hiện 3 profiles: Main/Sub1/Sub2 |
| Live View | Xem được video test pattern |
| PTZ | Các nút Pan/Tilt/Zoom hoạt động |
| Events | Nhận được motion/tamper events |

---

## Test 3: Python Script

### Script test cơ bản

```python
#!/usr/bin/env python3
# test_onvif.py
# pip install onvif-zeep

from onvif import ONVIFCamera
import zeep

# ── Kết nối camera ────────────────────────────────────────────────
cam = ONVIFCamera(
    host='192.168.8.114',
    port=8080,
    user='admin',
    passwd='admin123'
)

print("=" * 50)
print("  ONVIF Test Script")
print("=" * 50)

# ── Test 1: GetSystemDateAndTime (không cần auth) ─────────────────
device_svc = cam.create_devicemgmt_service()

try:
    dt = device_svc.GetSystemDateAndTime()
    utc = dt.UTCDateTime
    print(f"\n[OK] GetSystemDateAndTime:")
    print(f"     {utc.Date.Year}-{utc.Date.Month:02d}-{utc.Date.Day:02d} "
          f"{utc.Time.Hour:02d}:{utc.Time.Minute:02d}:{utc.Time.Second:02d} UTC")
except Exception as e:
    print(f"[FAIL] GetSystemDateAndTime: {e}")

# ── Test 2: GetDeviceInformation ──────────────────────────────────
try:
    info = device_svc.GetDeviceInformation()
    print(f"\n[OK] GetDeviceInformation:")
    print(f"     Manufacturer : {info.Manufacturer}")
    print(f"     Model        : {info.Model}")
    print(f"     Firmware     : {info.FirmwareVersion}")
    print(f"     Serial       : {info.SerialNumber}")
except Exception as e:
    print(f"[FAIL] GetDeviceInformation: {e}")

# ── Test 3: GetCapabilities ───────────────────────────────────────
try:
    caps = device_svc.GetCapabilities({'Category': 'All'})
    print(f"\n[OK] GetCapabilities:")
    if caps.Media:
        print(f"     Media  XAddr : {caps.Media.XAddr}")
    if caps.PTZ:
        print(f"     PTZ    XAddr : {caps.PTZ.XAddr}")
    if caps.Events:
        print(f"     Events XAddr : {caps.Events.XAddr}")
except Exception as e:
    print(f"[FAIL] GetCapabilities: {e}")

# ── Test 4: GetServices ───────────────────────────────────────────
try:
    svcs = device_svc.GetServices({'IncludeCapability': False})
    print(f"\n[OK] GetServices: {len(svcs)} services")
    for s in svcs:
        print(f"     {s.Namespace} → {s.XAddr}")
except Exception as e:
    print(f"[FAIL] GetServices: {e}")

# ── Test 5: GetScopes ─────────────────────────────────────────────
try:
    scopes = device_svc.GetScopes()
    print(f"\n[OK] GetScopes: {len(scopes)} scopes")
    for s in scopes:
        print(f"     {s.ScopeItem}")
except Exception as e:
    print(f"[FAIL] GetScopes: {e}")

# ── Test 6: GetProfiles (Media2) ──────────────────────────────────
try:
    media_svc = cam.create_media2_service()
    profiles = media_svc.GetProfiles({'Type': ['All']})
    print(f"\n[OK] GetProfiles: {len(profiles)} profiles")
    for p in profiles:
        print(f"     [{p.token}] {p.Name}")
except Exception as e:
    print(f"[FAIL] GetProfiles: {e}")

# ── Test 7: GetStreamUri ──────────────────────────────────────────
try:
    profiles = media_svc.GetProfiles({'Type': ['All']})
    print(f"\n[OK] GetStreamUri:")
    for p in profiles:
        req = {'ProfileToken': p.token, 'Protocol': 'RTSP'}
        uri = media_svc.GetStreamUri(req)
        print(f"     [{p.token}] {uri.Uri}")
except Exception as e:
    print(f"[FAIL] GetStreamUri: {e}")

# ── Test 8: PTZ GetStatus ─────────────────────────────────────────
try:
    ptz_svc = cam.create_ptz_service()
    status = ptz_svc.GetStatus({'ProfileToken': 'profile_main'})
    pos = status.Position
    print(f"\n[OK] PTZ GetStatus:")
    print(f"     Pan={pos.PanTilt.x:.2f} Tilt={pos.PanTilt.y:.2f} "
          f"Zoom={pos.Zoom.x:.2f}")
except Exception as e:
    print(f"[FAIL] PTZ GetStatus: {e}")

print("\n" + "=" * 50)
print("  Test completed")
print("=" * 50)
```

### Chạy script:

```bash
# Trên máy Windows (có thể ping được Jetson):
pip install onvif-zeep
python test_onvif.py
```

---

## Kiểm tra SOAP thủ công bằng curl

### GetSystemDateAndTime (không cần auth)

```bash
curl -s -X POST http://192.168.8.114:8080/onvif/device \
  -H "Content-Type: application/soap+xml" \
  -d '<?xml version="1.0"?>
<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope"
            xmlns:tds="http://www.onvif.org/ver10/device/wsdl">
  <s:Body>
    <tds:GetSystemDateAndTime/>
  </s:Body>
</s:Envelope>'
```

### GetDeviceInformation (cần WS-Security)

> Để gửi WS-Security đúng chuẩn, dùng Python script hoặc ODM thay vì curl thuần.

```bash
# Dùng wscat hoặc SOAP UI để test có auth
# Hoặc dùng Python script ở trên
```

---

## Các lỗi thường gặp khi test

### Lỗi: `Connection refused`
```
Nguyên nhân: onvif-server chưa chạy hoặc sai port
Giải pháp  : Kiểm tra ./onvif-server đang chạy, port 8080 mở
Test       : curl http://192.168.8.114:8080/onvif/device
```

### Lỗi: `Authentication failed`
```
Nguyên nhân: Sai username/password hoặc WS-Security sai
Giải pháp  : Kiểm tra onvif.conf: username=admin password=admin123
Test       : Dùng ODM với admin/admin123
```

### Lỗi: `No profiles found`
```
Nguyên nhân: Media2Service chưa implement hoặc IPC bị lỗi
Giải pháp  : Kiểm tra mock-camera-server đang chạy
Test       : ./onvif-ipc-test config/onvif.conf
```

### Lỗi: `RTSP stream không load`
```
Nguyên nhân: GStreamer pipeline chưa chạy hoặc MediaMTX chưa start
Giải pháp  : bash scripts/start_streams.sh
Test       : bash scripts/check_streams.sh
```

### ODM hiện: `Device not found in discovery`
```
Nguyên nhân: WS-Discovery UDP không qua được switch/router
Giải pháp  : Add device thủ công bằng IP
             Hoặc đảm bảo cùng subnet và port 3702/UDP mở
```

---

## Checklist test đầy đủ Profile T

```
□ GetSystemDateAndTime         - Không cần auth
□ GetDeviceInformation         - Cần auth
□ GetCapabilities              - Cần auth
□ GetServices                  - Cần auth
□ GetServiceCapabilities       - Cần auth
□ GetScopes                    - Cần auth
□ GetNetworkInterfaces         - Cần auth
□ GetUsers                     - Cần auth
□ GetProfiles (Media2)         - Cần auth
□ GetStreamUri (Media2)        - Cần auth + RTSP stream hoạt động
□ GetSnapshotUri (Media2)      - Cần auth
□ PTZ GetStatus                - Cần auth
□ PTZ AbsoluteMove             - Cần auth + kiểm tra state thay đổi
□ PTZ RelativeMove             - Cần auth
□ PTZ ContinuousMove + Stop    - Cần auth
□ GetImagingSettings           - Cần auth
□ SetImagingSettings           - Cần auth + verify get lại
□ GetSupportedAnalyticsModules - Cần auth
□ Subscribe (Events)           - Cần auth + nhận được event push
□ WS-Discovery                 - Device xuất hiện khi scan network
```
