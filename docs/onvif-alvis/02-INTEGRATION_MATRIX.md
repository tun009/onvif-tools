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
| Network | S/T/M/G | MGMT | Internal REST/IPC | REAL_IN_PROGRESS | MGMT persist `network_protocols` trong `mgmt_network_config.json`; apply daemon/listener chưa hoàn thiện |
| Date/time/NTP | S/T/M/G | MGMT | Internal REST/IPC | REAL_IN_PROGRESS | MGMT đã có DateTime service |
| Discovery/scopes | S/T/M/G | MGMT desired state + onvif-module runtime | REST/IPC + WS-Discovery | REAL_IN_PROGRESS | MGMT persist Discovery Mode/scopes; onvif-module phải là WS-Discovery responder duy nhất và phục hồi state sau restart |
| ONVIF users/RBAC | S/T/M/G | MGMT/Security | Internal REST | REAL_IN_PROGRESS | Local đã nối WSSE PasswordDigest qua MGMT `users(type=onvif)`; mock mode giữ auth tĩnh, hybrid/production fail-closed và chưa có camera/DTT evidence; HTTP Digest/RBAC chưa nối |
| Media profiles | S/T/M | DVR | Internal API/IPC | REAL_IN_PROGRESS | Backend mới đã có stream/profile một phần; cần chốt contract/token |
| Live RTSP | S/T | DVR | RTSP | REAL_IN_PROGRESS | Cần xác minh URI/SDP/auth với ONVIF profile |
| Snapshot | S/T | DVR | HTTP/media API | MOCK | Cần xác nhận backend thật |
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
| GetDeviceInformation | Device | `IDeviceBackend::getDeviceInfo` | MGMT DeviceInformation | REAL_IN_PROGRESS | Local source route qua `HttpMgmtClient`; lỗi MGMT trả SOAP Receiver fault, chưa có camera runtime evidence |
| GetSystemDateAndTime | Device | `IDeviceBackend::getDateTime` | MGMT DateTime | REAL_IN_PROGRESS | Chưa ghi |
| GetNetworkInterfaces | Device | `IDeviceBackend::getNetwork` | MGMT Network | REAL_IN_PROGRESS | Chưa ghi |
| GetNetworkProtocols | Device | `IDeviceBackend::getNetworkProtocols` | MGMT Network + runtime config | REAL_IN_PROGRESS | SOAP chỉ trả HTTP/HTTPS/RTSP; cần map MGMT ONVIF port → SOAP HTTP port |
| SetNetworkProtocols | Device | `IDeviceBackend::setNetworkProtocols` | MGMT Network + service supervisor | REAL_IN_PROGRESS | MGMT hiện mới persist/log; chưa apply listener, cần partial update |
| GetScopes | Device | `IDeviceBackend::getScopes` | MGMT Discovery | REAL_IN_PROGRESS | Chưa ghi |
| GetDiscoveryMode | Device | `IDeviceBackend::getDiscoveryMode` | MGMT Discovery | REAL_IN_PROGRESS | Cần bỏ state memory-only và phục hồi persistent state khi restart |
| SetDiscoveryMode | Device | `IDeviceBackend::setDiscoveryMode` | MGMT Discovery + onvif-module `DiscoveryService` | REAL_IN_PROGRESS | MGMT persist; onvif-module apply Probe/Resolve behavior, không chạy `wsdd` song song |
| GetProfiles | Media1/2 | `IMediaBackend::getProfiles` | DVR | REAL_IN_PROGRESS | Chưa ghi |
| GetStreamUri | Media1/2 | `IMediaBackend::getStreamUri` | DVR RTSP | REAL_IN_PROGRESS | Chưa ghi |
| GetSnapshotUri | Media | `IMediaBackend::getSnapshotUri` | DVR | MOCK | Chưa ghi |
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
| MGMT internal client config/auth | ONVIF + MGMT | IN_PROGRESS — WSSE PasswordDigest contract đã nối local; HTTP Digest, service credential và runtime evidence còn thiếu |
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
