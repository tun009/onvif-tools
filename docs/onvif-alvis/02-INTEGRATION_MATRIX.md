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
| Network | S/T/M/G | MGMT | Internal REST/IPC | REAL_IN_PROGRESS | Chỉ IPv4 (không mandatory IPv6, xem 01-IMPLEMENTATION_PLAN.md 2.1); `capabilities.network=real` đã bật trên `.194`. Build/test case đọc-an-toàn (2026-09-29, `r28.xml`): `GetNetworkDefaultGateway` REAL_DTT sạch; `GetDNS`/`GetNetworkInterfaces` code đã verify đúng nhưng dữ liệu BLOCKED do MGMT chưa provision "primary ethernet interface" cho `.194` (không phải bug onvif-module). Các case Set + case rủi ro cao vẫn chưa test |
| Date/time/NTP | S/T/M/G | MGMT | Internal REST/IPC | REAL_DTT | GetSystemDateAndTime + SetSystemDateAndTime (case Manual) đã nối MGMT thật, DTT pass trên `.194` 2026-09-29 (`3-1-1/3-1-4/3-1-5/3-1-11`, r26/r27). `parsePosixOffsetMinutes()` mở rộng hỗ trợ cú pháp POSIX TZ đầy đủ (tên std/dst quote `<...>` + rule DST) để tương thích ODM, không chỉ dạng rút gọn `UTC±H[:MM]` — vẫn giữ nguyên chính sách từ chối nếu offset yêu cầu khác offset hiện tại (không đoán IANA zone). NTP (GetNTP/SetNTP) không mandatory, source viết xong nhưng chưa build/test |
| Discovery/scopes | S/T/M/G | MGMT desired state + onvif-module runtime | REST/IPC + WS-Discovery | REAL_IN_PROGRESS | MGMT persist Discovery Mode/scopes; onvif-module phải là WS-Discovery responder duy nhất và phục hồi state sau restart |
| ONVIF users/RBAC | S/T/M/G | MGMT/Security | Internal REST | REAL_IN_PROGRESS | HTTP Digest `REAL_VERIFIED` bằng DTT trên camera `192.168.8.127` ngày 2026-09-15: user `type=onvif` từ SQLite MGMT xác thực qua `onvif-module:8001 -> MGMT:8086`, `GetDeviceInformation` thành công; WSSE runtime evidence và RBAC chưa hoàn tất |
| Media profiles | S/T/M | DVR | Internal API/IPC | REAL_DTT | Media1+Media2 GetProfiles/VideoSource/VideoEncoderConfig(Options) đã nối DVR thật, gồm cả profile MJPEG mới (`0_mjpeg`/`1_mjpeg`, Device MANDATORY Profile S). `r17.xml` (2026-09-24): 24/25 pass, chỉ còn `RTSS-1-1-48` fail (root cause: `1_sub` bị `enabled=false` trong DB DVR, không liên quan onvif-module — xem 01-IMPLEMENTATION_PLAN.md). Chưa test VMS/restart-failure nên chưa `REAL_VERIFIED` |
| Live RTSP | S/T | DVR | RTSP | REAL_DTT | Digest auth + port động + RtspOverHttp tunnel + MJPEG đều pass DTT thật (`r11.xml`, `r17.xml`) trên `.125`. `1_sub` (channel 1 sub-stream) hiện 404 do DB DVR, không phải RTSP layer |
| Snapshot | S/T | DVR | HTTP/media API | REAL_DTT | `GetSnapshotUri` build URI tĩnh trỏ thẳng `GET /dvr/v1.0/GetSnapshot` thật của DVR (JPEG thật từ `mjpeg_codec`, không phải mock). `MEDIA-6-1-1` pass trong `g13.xml`/`r12-r17` |
| Metadata configuration | M/T | DVR/VPU | API/IPC | MOCK | Cần canonical metadata profile |
| Imaging | S/T | MGMT + HAL | REST + control IPC | REAL_DTT | `imaging=real` đã bật. Brightness/Contrast/Saturation/Sharpness/BLC/WDR + Exposure (Mode/Time/Gain đầy đủ) + WhiteBalance (Mode/CrGain/CbGain) + IrCutFilter (dịch từ MGMT DayNight 2 trục) đều round-trip thật qua `HttpMgmtClient::get/setImagingSettings`. DTT `IMAGING-*` PASS r20-r25 trên `.194` (2026-09-28/29), gồm `IMAGING-1-1-14` persistence. Focus.AutoFocusMode + NearLimit/FarLimit vẫn local-cache (không có API MGMT tương ứng) |
| PTZ/zoom/focus | S/T | MGMT (`LensApiController`, không phải HAL/BUS trực tiếp) | Internal REST | REAL_DTT | `ptz=real` đã bật. `PtzService.cpp` dựng mới hoàn toàn, PTZ node chỉ có trục Zoom qua `AbsoluteMove`/`GetStatus`/`GetConfiguration(Options)` — xác nhận log MGMT `LensService HAL-DIRECT-OK set_zoom` (2026-09-28). Pan/Tilt không advertise (đúng theo Profile S/T/M/G, không profile nào mandatory PTZ — xem 01-IMPLEMENTATION_PLAN.md Phase 5). `ContinuousMove`/`Home Position`/`RelativeMove` KHÔNG implement (luôn fault) — quyết định hoãn có chủ đích của user (2026-09-28), DTT các test PTZ Service "Must" liên quan sẽ luôn fail cho tới khi làm (kể cả no-op) |
| Analytics metadata | M | VPU | BUS event + RTP metadata | MOCK | Chưa nối VPU result thật |
| Analytics rules/modules | M/T | VPU/Core | Internal API/BUS | MOCK | Cần map rule/application canonical |
| ONVIF Event/PullPoint | S/T/M/G | Core + BUS | Event bus | MOCK | SOAP behavior đã pass; nguồn event còn mock |
| Recording control | G | DVR (trạng thái ghi) + onvif-module (job, cấu hình) | Internal REST (`GetListVideoSourceRecorder`, `SetOnOffVideoRecorder`) | REAL_DTT | `recording=real` trên `.194` (2026-10-08): DTT Recording Control **22/22** (r33; lịch sử r29–r33 trong 01-IMPLEMENTATION_PLAN.md Phase 7), thử tay Happytime đạt, job lưu bền ở `/media/database/onvif_recording.dat` (có `fsync`) và nạp lại sau restart. Chưa `REAL_VERIFIED`: chưa kiểm DTT đường job quan sát được (4-1-4/5/7 chạy với danh sách rỗng), chưa kiểm bền qua restart DVR, chưa test VMS. Giới hạn: `MaximumRetentionTime` chỉ lưu không thực thi; chưa có event `JobState` khi bật/tắt ghi từ web; job quan sát được của ghi theo lịch không dừng được bằng ONVIF |
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
| GetDNS | Device | `ICameraBackend::getDns` | MGMT `GET /mgmt/v1/GetDNS` | REAL_DTT (code) / BLOCKED (data) | Đã build/test `.194` (`r28.xml`, 2026-09-29): `DEVICE-2-1-4` FAILED — nhưng root cause là MGMT trả `{"error":"Primary ethernet interface is not configured in database"}` (curl xác nhận trực tiếp `127.0.0.1:8086/mgmt/v1/GetDNS`), onvif-module map đúng thành SOAP Receiver fault `"DNS backend unavailable"` — không phải bug parse/mapping. Cần MGMT provision "primary ethernet interface" cho `.194` trước khi test lại được |
| SetDNS | Device | `ICameraBackend::setDns` | MGMT `POST /mgmt/v1/SetDNS` | REAL_IN_PROGRESS | Chỉ IPv4; source viết xong 2026-09-16, chưa test (rủi ro thấp — xem 01-IMPLEMENTATION_PLAN.md Phase 3). Nhiều khả năng cũng bị chặn bởi cùng gap "primary ethernet interface" như GetDNS |
| GetNetworkInterfaces | Device | `ICameraBackend::getNetworkInterface` | MGMT `GET /mgmt/v1/GetNetworkInterfaces` | REAL_DTT (code) / BLOCKED (data) | Đã build/test `.194` (`r28.xml`): `DEVICE-2-1-17` PASSED nhưng data trả về gần như rỗng (`Name=""`, `HwAddress="00:00:00:00:00:00"`, `Address=""`) — cùng root cause "primary ethernet interface" chưa provision trong MGMT DB khiến `network_service_->getInterfaceRuntime("")` không tra được gì; `DefaultGateway`/`PrimaryDNS` vẫn ra đúng vì lấy từ nguồn khác. Mapping field onvif-module ↔ MGMT JSON đã xác nhận đúng (khớp `network_interface_api_controller.cpp`), lỗi hoàn toàn ở dữ liệu MGMT, không phải code |
| SetNetworkInterfaces | Device | `ICameraBackend::setNetworkInterface` | MGMT `POST /mgmt/v1/SetNetworkInterfaces` | REAL_IN_PROGRESS | MGMT apply bất đồng bộ (background thread) — không xác nhận apply thành công đồng bộ được; source viết xong 2026-09-16, **rủi ro cao, chưa test** (cần cửa sổ bảo trì) |
| GetNetworkDefaultGateway | Device | `ICameraBackend::getNetworkGateway` | MGMT `GET /mgmt/v1/GetNetworkDefaultGateway` | REAL_DTT | Build/test `.194` (`r28.xml`, 2026-09-29): `DEVICE-2-1-25` PASSED, trả đúng `192.168.8.254` (giá trị thật, không rỗng) — không bị ảnh hưởng bởi gap "primary ethernet interface" như 2 operation trên |
| SetNetworkDefaultGateway | Device | `ICameraBackend::setNetworkGateway` | MGMT `POST /mgmt/v1/SetNetworkDefaultGateway` | REAL_IN_PROGRESS | Chỉ IPv4; source viết xong 2026-09-16, chưa build/test camera |
| GetNetworkProtocols | Device | `ICameraBackend::getNetworkProtocols` | MGMT `GET /mgmt/v1/GetNetworkProtocols` | REAL_IN_PROGRESS | Map MGMT ONVIF port → SOAP HTTP port (đã làm); HTTPS luôn disabled; source viết xong 2026-09-16, chưa build/test camera |
| SetNetworkProtocols | Device | `ICameraBackend::setNetworkProtocols` | MGMT `POST /mgmt/v1/SetNetworkProtocols` | REAL_IN_PROGRESS | Đổi port ONVIF chỉ persist, chưa tự rebind listener onvif-module (cần restart thủ công); source viết xong 2026-09-16, chưa build/test camera |
| GetScopes | Device | `IDeviceBackend::getScopes` | MGMT Discovery | REAL_IN_PROGRESS | Chưa ghi |
| GetDiscoveryMode | Device | `IDeviceBackend::getDiscoveryMode` | MGMT Discovery | REAL_IN_PROGRESS | Cần bỏ state memory-only và phục hồi persistent state khi restart |
| SetDiscoveryMode | Device | `IDeviceBackend::setDiscoveryMode` | MGMT Discovery + onvif-module `DiscoveryService` | REAL_IN_PROGRESS | MGMT persist; onvif-module apply Probe/Resolve behavior, không chạy `wsdd` song song |
| GetProfiles | Media1/2 | `IMediaBackend::getProfiles` | DVR | REAL_DTT | `r17.xml` (2026-09-24): PASS, gồm cả 2 profile MJPEG mới `0_mjpeg`/`1_mjpeg` |
| GetStreamUri | Media1/2 | `IMediaBackend::getStreamUri` | DVR RTSP | REAL_DTT | PASS cho mọi token trừ `1_sub` (404, DB DVR `enabled=false`, không phải bug onvif-module — xem 01-IMPLEMENTATION_PLAN.md mục RTSS-1-1-48) |
| GetSnapshotUri | Media | `IMediaBackend::getSnapshotUri` | DVR | REAL_DTT | `MEDIA-6-1-1` PASS (`g13.xml`, `r12-r17`), URI trỏ thẳng `GET /dvr/v1.0/GetSnapshot` thật |
| GetImagingSettings | Imaging | `HttpMgmtClient::getImagingSettings` | MGMT `GET /mgmt/v1/Config/ImagingSettings` | REAL_DTT | Đầy đủ field cơ bản + Exposure/WhiteBalance/IrCutFilter thật, DTT `IMAGING-*` pass r20-r25 trên `.194` |
| SetImagingSettings | Imaging | `HttpMgmtClient::setImagingSettings` | MGMT `PUT /mgmt/v1/Config/ImagingSettings` | REAL_DTT | Cùng evidence GetImagingSettings; `IMAGING-1-1-14` xác nhận persist round-trip |
| AbsoluteMove/GetStatus | PTZ | `PtzService` → `AlvisBackendFacade` (normalize `[0,1]` ↔ `[MinZoom,MaxZoom]`) | MGMT `LensApiController` (`/Config/ZoomFocus`) | REAL_DTT | Zoom-only, xác nhận log MGMT `LensService HAL-DIRECT-OK set_zoom` di chuyển lens thật (2026-09-28). Range validation zoom `[0,1]` trả `ter:InvalidPosition` nếu ngoài range |
| ContinuousMove/Stop/Home Position | PTZ | chưa implement (luôn fault) | — | UNSUPPORTED | Cố ý hoãn — DTT pre-filter chỉ theo `RequiredFeatures` cấp service (`PTZService`), không theo node capability, nên các test PTZ Service "Must" này luôn chạy và fail cho tới khi có no-op implementation. User quyết định tạm hoãn (2026-09-28) |
| GetSupportedMetadata | Analytics | `IAnalyticsBackend` | VPU | MOCK | Mock DTT baseline |
| PullMessages | Event | `IEventBackend` | Core/BUS | MOCK | Mock DTT baseline |
| GetRecordings / Get-SetRecordingConfiguration / Get-SetTrackConfiguration / GetRecordingOptions | Recording | `DvrRecordingService` (+ `IDvrClient::getRecorderSources`, `RecordingJobStore`) | DVR `GET /dvr/v1.0/GetListVideoSourceRecorder` + `GET /dvr/v3.0/GetProfiles` | REAL_DTT | r33 (2026-10-08): RECORDING-1-1-1/1-1-3/4-1-1/4-1-2/4-1-3/4-1-9/4-1-10/4-1-11, 5-1-3/5-1-4 PASS. Recording `rec_<VideoSourceId>`, track `VIDEO_main`/`VIDEO_sub`; nguồn ảo "Overlay" của DVR bị loại. Cấu hình do client đặt lưu ở onvif-module (DVR không có chỗ lưu) |
| CreateRecordingJob / DeleteRecordingJob / SetRecordingJobMode | Recording | `DvrRecordingService` + `IDvrClient::setManualRecord` | DVR `POST /dvr/v1.0/SetOnOffVideoRecorder` (ghi tay) | REAL_DTT | r33: RECORDING-2-1-28/29/30, 3-1-11 (bỏ qua: không có recording tạo được 2 job) PASS. Bật ghi phải đọc lại trạng thái để xác nhận (DVR trả thành công cả khi từ chối âm thầm). Ghi tay dùng chung với nút Record trên web |
| GetRecordingJobs / GetRecordingJobConfiguration / SetRecordingJobConfiguration / GetRecordingJobState | Recording | `DvrRecordingService` + `RecordingJobStore` | DVR (state) + file onvif-module (job) | REAL_DTT | r33: RECORDING-4-1-4/4-1-5/4-1-7/4-1-13/4-1-14, 5-1-18/19/20 PASS. Job quan sát được `auto_<rec>_<luồng>` (luồng ghi từ web chưa có job) chưa được DTT kiểm (danh sách rỗng lúc chạy); đã kiểm bằng thử tay. `Tracks`: cấu hình phẳng, trạng thái/event bọc `Track` |
| Event tns1:RecordingConfig/JobState (+ RecordingConfiguration, TrackConfiguration, RecordingJobConfiguration) | Event | `MockSubscriptionManager` + provider job thật | Nội bộ onvif-module | REAL_DTT | r33: RECORDING-5-1-3/4/18/19/20 PASS (Initialized và Changed). Chỉ bắn khi chính ONVIF đổi trạng thái, chưa bắn khi bật/tắt ghi từ web |
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
| Recording/Search/Replay API | ONVIF + DVR + Core | IN_PROGRESS: phần Recording Control chạy trên REST hiện có của DVR (không cần thay đổi DVR); phương án C (DVR giữ job) là đề xuất chưa quyết, xem 01-IMPLEMENTATION_PLAN.md Phase 7; Search/Replay chưa chốt (cần DVR sửa giờ file hoặc onvif-module tự tính từ `mtime − duration`; Replay cần server RTSP mới) |
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
