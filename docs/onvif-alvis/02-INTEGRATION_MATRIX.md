# ONVIF–Camera-alvis Integration Matrix

## 1. Cách sử dụng

Đây là bảng trạng thái sống. Mỗi developer/AI agent phải cập nhật khi chuyển một capability từ mock sang backend thật.

Trạng thái hợp lệ:

```text
MOCK              Chỉ có implementation giả.
REAL_IN_PROGRESS  Đang nối backend thật.
REAL_SMOKE        Đã chạy smoke/integration test cơ bản.
REAL_DTT          Đã pass DTT test liên quan.
REAL_VERIFIED     Đã pass DTT + VMS + restart/failure test.
UNSUPPORTED       Sản phẩm quyết định không hỗ trợ và không advertise.
```

## 2. Matrix cấp domain

| Domain | Profile | Real owner | Transport dự kiến | Trạng thái | Evidence/ghi chú |
|---|---|---|---|---|---|
| Device information | S/T/M/G | MGMT | Internal REST/IPC | REAL_IN_PROGRESS | Baseline ONVIF build pass trên camera 2026-09-07; local đã có vertical-slice config HTTP 8001 + MGMT 8086, mock optional, Discovery off và không fallback identity giả; chưa sync-build-runtime-test trên camera |
| Network | S/T/M/G | MGMT | Internal REST/IPC | REAL_IN_PROGRESS | Đã viết xong source (2026-09-16) cho Hostname/DNS/NetworkInterfaces/Gateway/NetworkProtocols, chỉ IPv4 (không mandatory IPv6, xem 01-IMPLEMENTATION_PLAN.md 2.1); gate qua `capabilities.network` (cần đặt `real` trong onvif.conf); chưa build/test trên camera |
| Date/time/NTP | S/T/M/G | MGMT | Internal REST/IPC | REAL_DTT | GetSystemDateAndTime + SetSystemDateAndTime (case Manual) đã nối MGMT thật, DTT pass trên camera `192.168.8.124` 2026-09-15 (`3-1-1/3-1-4/3-1-5/3-1-11`). NTP (GetNTP/SetNTP) không mandatory, source viết xong nhưng chưa build/test |
| Discovery/scopes | S/T/M/G | MGMT desired state + onvif-module runtime | REST/IPC + WS-Discovery | REAL_IN_PROGRESS | MGMT persist Discovery Mode/scopes; onvif-module phải là WS-Discovery responder duy nhất và phục hồi state sau restart |
| ONVIF users/RBAC | S/T/M/G | MGMT/Security | Internal REST | REAL_IN_PROGRESS | HTTP Digest `REAL_VERIFIED` bằng DTT trên camera `192.168.8.127` ngày 2026-09-15: user `type=onvif` từ SQLite MGMT xác thực qua `onvif-module:8001 -> MGMT:8086`, `GetDeviceInformation` thành công; WSSE runtime evidence và RBAC chưa hoàn tất |
| Media profiles | S/T/M | DVR | Internal API/IPC | REAL_DTT | Media1+Media2 GetProfiles/VideoSource/VideoEncoderConfig(Options) đã nối DVR thật, gồm cả profile MJPEG mới (`0_mjpeg`/`1_mjpeg`, Device MANDATORY Profile S). `r17.xml` (2026-09-24): 24/25 pass, chỉ còn `RTSS-1-1-48` fail (root cause: `1_sub` bị `enabled=false` trong DB DVR, không liên quan onvif-module — xem 01-IMPLEMENTATION_PLAN.md). Chưa test VMS/restart-failure nên chưa `REAL_VERIFIED` |
| Live RTSP | S/T | DVR | RTSP | REAL_DTT | Digest auth + port động + RtspOverHttp tunnel + MJPEG đều pass DTT thật (`r11.xml`, `r17.xml`) trên `.125`. `1_sub` (channel 1 sub-stream) hiện 404 do DB DVR, không phải RTSP layer |
| Snapshot | S/T | DVR | HTTP/media API | REAL_DTT | `GetSnapshotUri` build URI tĩnh trỏ thẳng `GET /dvr/v1.0/GetSnapshot` thật của DVR (JPEG thật từ `mjpeg_codec`, không phải mock). `MEDIA-6-1-1` pass trong `g13.xml`/`r12-r17` |
| Metadata configuration | M/T | DVR/VPU | API/IPC | MOCK | Cần canonical metadata profile |
| Imaging | S/T | MGMT + HAL | REST + control IPC | REAL_IN_PROGRESS | MGMT có ImagingSettings, phụ thuộc HAL capability |
| PTZ/zoom/focus | S/T | HAL | BUS control IPC | REAL_IN_PROGRESS | Zoom/focus đã có một phần; pan/tilt tùy hardware |
| Analytics metadata | M | VPU | BUS event + RTP metadata | MOCK | Chưa nối VPU result thật |
| Analytics rules/modules | M/T | VPU/Core | Internal API/BUS | MOCK | Cần map rule/application canonical |
| ONVIF Event/PullPoint | S/T/M/G | Core + BUS | Event bus | MOCK | SOAP behavior đã pass; nguồn event còn mock |
| Recording control | G | DVR | Internal API/IPC | REAL_IN_PROGRESS | Backend mới đã có record một phần; cần inventory operation |
| Recording search | G | DVR/Core | Internal API/IPC | REAL_IN_PROGRESS | Cần index/time/filter/token contract |
| Replay | G | DVR | API + RTSP replay | REAL_IN_PROGRESS | Backend mới đã có playback một phần; cần ONVIF Range/URI test |

## 3. Matrix operation ưu tiên

| ONVIF operation | Service | Backend method/adapter | Real endpoint/IPC | Trạng thái | Test evidence |
|---|---|---|---|---|---|
| GetDeviceInformation | Device | `IDeviceBackend::getDeviceInfo` | MGMT DeviceInformation | REAL_VERIFIED | DTT đọc thành công trên camera `192.168.8.127` ngày 2026-09-15 sau HTTP Digest bằng user SQLite `type=onvif`; route runtime `onvif-module:8001 -> MGMT:8086` |
| GetSystemDateAndTime | Device | `ICameraBackend::getSystemDateAndTime` (facade hiện tại) | MGMT DateTime | REAL_DTT | DTT trên camera `192.168.8.124` 2026-09-15: `DEVICE-3-1-1` PASS. Route `onvif-module:8001 -> MGMT:8086`, không mock/fallback system time |
| SetSystemDateAndTime | Device | `ICameraBackend::setSystemDateAndTime` (facade hiện tại) | MGMT DateTime | REAL_DTT | DTT trên camera `192.168.8.124` 2026-09-15: `DEVICE-3-1-4`/`3-1-5` (invalid timezone/date) và `DEVICE-3-1-11` (set thật) đều PASS. Case Manual đã nối MGMT thật; case NTP đọc/giữ NTPServer hiện có, chưa test (không mandatory) |
| GetNTP / SetNTP | Device | `ICameraBackend` (chưa có, gọi thẳng qua `SystemDateTime.ntpMode/ntpHost`) | MGMT DateTime (field `NTPServer`) | REAL_IN_PROGRESS | Source local đã viết (2026-09-15), CHƯA build/test trên camera. Không mandatory (Profile T spec 8.8 `Device CONDITIONAL`; `g13.xml` không chạy `DEVICE-3-1-12`) — không ưu tiên |
| SystemReboot | Device | chưa nối (mock) | MGMT `POST /mgmt/v1/Maintenance/Reboot` | MOCK | BLOCKED — team backend MGMT xác nhận 2026-09-15 API chưa làm xong dù thấy trong source. Chờ MGMT xác nhận hoàn thiện |
| SetSystemFactoryDefault | Device | chưa nối (mock) | MGMT `POST /mgmt/v1/Maintenance/RestoreDefault` (Soft) / `FactoryReset` (Hard) | MOCK | BLOCKED — cùng lý do trên. Hard reset sẽ xóa tài khoản ONVIF + đổi network về DHCP, cẩn thận khi test sau này |
| GetHostName | Device | `ICameraBackend::getHostname` | MGMT `GET /mgmt/v1/GetHostname` | REAL_IN_PROGRESS | Source viết xong 2026-09-16, chưa build/test camera |
| SetHostName | Device | `ICameraBackend::setHostname` | MGMT `POST /mgmt/v1/SetHostname` | REAL_IN_PROGRESS | Source viết xong 2026-09-16, chưa build/test camera |
| GetDNS | Device | `ICameraBackend::getDns` | MGMT `GET /mgmt/v1/GetDNS` | REAL_IN_PROGRESS | Chỉ IPv4; source viết xong 2026-09-16, chưa build/test camera |
| SetDNS | Device | `ICameraBackend::setDns` | MGMT `POST /mgmt/v1/SetDNS` | REAL_IN_PROGRESS | Chỉ IPv4; source viết xong 2026-09-16, chưa build/test camera |
| GetNetworkInterfaces | Device | `ICameraBackend::getNetworkInterface` | MGMT `GET /mgmt/v1/GetNetworkInterfaces` | REAL_IN_PROGRESS | Chỉ IPv4, 1 interface; source viết xong 2026-09-16, chưa build/test camera |
| SetNetworkInterfaces | Device | `ICameraBackend::setNetworkInterface` | MGMT `POST /mgmt/v1/SetNetworkInterfaces` | REAL_IN_PROGRESS | MGMT apply bất đồng bộ (background thread) — không xác nhận apply thành công đồng bộ được; source viết xong 2026-09-16, chưa build/test camera |
| GetNetworkDefaultGateway | Device | `ICameraBackend::getNetworkGateway` | MGMT `GET /mgmt/v1/GetNetworkDefaultGateway` | REAL_IN_PROGRESS | Chỉ IPv4; source viết xong 2026-09-16, chưa build/test camera |
| SetNetworkDefaultGateway | Device | `ICameraBackend::setNetworkGateway` | MGMT `POST /mgmt/v1/SetNetworkDefaultGateway` | REAL_IN_PROGRESS | Chỉ IPv4; source viết xong 2026-09-16, chưa build/test camera |
| GetNetworkProtocols | Device | `ICameraBackend::getNetworkProtocols` | MGMT `GET /mgmt/v1/GetNetworkProtocols` | REAL_IN_PROGRESS | Map MGMT ONVIF port → SOAP HTTP port (đã làm); HTTPS luôn disabled; source viết xong 2026-09-16, chưa build/test camera |
| SetNetworkProtocols | Device | `ICameraBackend::setNetworkProtocols` | MGMT `POST /mgmt/v1/SetNetworkProtocols` | REAL_IN_PROGRESS | Đổi port ONVIF chỉ persist, chưa tự rebind listener onvif-module (cần restart thủ công); source viết xong 2026-09-16, chưa build/test camera |
| GetScopes | Device | `IDeviceBackend::getScopes` | MGMT Discovery | REAL_IN_PROGRESS | Chưa ghi |
| GetDiscoveryMode | Device | `IDeviceBackend::getDiscoveryMode` | MGMT Discovery | REAL_IN_PROGRESS | Cần bỏ state memory-only và phục hồi persistent state khi restart |
| SetDiscoveryMode | Device | `IDeviceBackend::setDiscoveryMode` | MGMT Discovery + onvif-module `DiscoveryService` | REAL_IN_PROGRESS | MGMT persist; onvif-module apply Probe/Resolve behavior, không chạy `wsdd` song song |
| GetProfiles | Media1/2 | `IMediaBackend::getProfiles` | DVR | REAL_DTT | `r17.xml` (2026-09-24): PASS, gồm cả 2 profile MJPEG mới `0_mjpeg`/`1_mjpeg` |
| GetStreamUri | Media1/2 | `IMediaBackend::getStreamUri` | DVR RTSP | REAL_DTT | PASS cho mọi token trừ `1_sub` (404, DB DVR `enabled=false`, không phải bug onvif-module — xem 01-IMPLEMENTATION_PLAN.md mục RTSS-1-1-48) |
| GetSnapshotUri | Media | `IMediaBackend::getSnapshotUri` | DVR | REAL_DTT | `MEDIA-6-1-1` PASS (`g13.xml`, `r12-r17`), URI trỏ thẳng `GET /dvr/v1.0/GetSnapshot` thật |
| GetImagingSettings | Imaging | `IImagingBackend::getSettings` | MGMT/HAL | REAL_IN_PROGRESS | Chưa ghi |
| SetImagingSettings | Imaging | `IImagingBackend::setSettings` | MGMT/HAL | REAL_IN_PROGRESS | Chưa ghi |
| ContinuousMove/Stop | PTZ | `IPtzBackend` | HAL/BUS | REAL_IN_PROGRESS | Chưa ghi |
| GetSupportedMetadata | Analytics | `IAnalyticsBackend` | VPU | MOCK | Mock DTT baseline |
| PullMessages | Event | `IEventBackend` | Core/BUS | MOCK | Mock DTT baseline |
| GetRecordings | Recording | `IRecordingBackend` | DVR | REAL_IN_PROGRESS | Chưa ghi |
| FindRecordings | Search | `ISearchBackend` | DVR index | REAL_IN_PROGRESS | Chưa ghi |
| FindEvents | Search | `ISearchBackend` | DVR/Core metadata | REAL_IN_PROGRESS | Chưa ghi |
| GetReplayUri | Replay | `IReplayBackend` | DVR playback | REAL_IN_PROGRESS | Chưa ghi |

## 4. Các contract cần chốt

| Contract | Owner cần tham gia | Trạng thái |
|---|---|---|
| Token registry Media/Metadata/Recording/Replay | ONVIF + DVR | OPEN |
| Public RTSP URI và SDP mapping | ONVIF + DVR | OPEN |
| MGMT internal client config/auth | ONVIF + MGMT | IN_PROGRESS — WSSE PasswordDigest và HTTP Digest contract đã nối local; service credential và runtime evidence còn thiếu |
| Network desired/applied/advertised state và port mapping | ONVIF + MGMT + DVR/supervisor | OPEN |
| Discovery persistence và quyền sở hữu WS-Discovery runtime | ONVIF + MGMT | OPEN |
| Imaging capability/range mapping | ONVIF + MGMT + HAL | OPEN |
| PTZ capability/coordinate mapping | ONVIF + HAL | OPEN |
| Detection metadata schema | ONVIF + VPU | OPEN |
| Alarm/Event schema và topic registry | ONVIF + Core | OPEN |
| Recording/Search/Replay API | ONVIF + DVR + Core | OPEN |
| ONVIF/RTSP shared identity | ONVIF + MGMT + Security + DVR | OPEN |

## 5. Evidence template

Khi đánh dấu trạng thái, ghi tối thiểu:

```text
Date:
Capability/operation:
onvif-module commit:
backend commit:
Runtime config:
DTT version/case/report:
VMS/client:
Restart/failure test:
Known limitations:
Owner:
```
