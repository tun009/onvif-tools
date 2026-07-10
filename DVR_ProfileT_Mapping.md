# ONVIF Profile T — Backend Mapping to 4-Service Architecture

**Repository**: `D:\Elcom\DVR\dvr` (new architecture — 4 services)
**ONVIF Module**: `D:\Elcom\Ovif-mock\projects\onvif-module` (204/210 pass với mock)
**Date**: 2026-07-07
**Author**: Daniel Tran

---

## 1. Kiến trúc 4 services

| Service | Owner | Responsibility |
|---|---|---|
| **dvr** | Ngọc | Capture · Encode · Record · RTSP |
| **vpu** | Tuấn Anh | PluginManager · Scheduler · Worker · Tracker · Publisher · plugin AI |
| **core** | Sơn | RuleEngine · AlarmManager · MetadataStore · Analytics |
| **gateway** | Quyền | SecurityCore · AdapterRegistry · OutputAdapters (egress + security) |

---

## 2. Mapping API mock backend → Profile T feature → Service

### 🎥 dvr (Ngọc)

| API mock (`ICameraBackend.h`) | Profile T feature | Trạng thái |
|---|---|---|
| `getProfiles()` | GetProfiles — list media profiles | Có |
| `getStreamUri(token, protocol)` | GetStreamUri — RTSP URL cho từng profile | Có |
| `setVideoEncoderConfig(profile, cfg)` | SetVideoEncoderConfiguration — resolution/fps/bitrate/GOP | Có |
| `getSnapshotUri(profile)` | GetSnapshotUri — URL snapshot | Có |
| **(cần thêm) Snapshot bytes HTTP** | GET `/snapshot?token=X` → JPEG binary | Thiếu |
| `getImagingSettings / setImagingSettings` | Imaging (brightness/contrast/exposure/WB/day-night) | Có |
| `getImagingStatus` | Imaging status (day/night, IR filter) | Có |
| **(cần thêm) OSD render** | CreateOSD / DeleteOSD → GStreamer textoverlay wire | Thiếu |
| **(cần thêm) Metadata RTP track** | Media2 metadata stream (RTP track song song video) | Thiếu |

---

### 🧠 vpu (Tuấn Anh)

| API mock | Profile T feature | Trạng thái |
|---|---|---|
| `getSupportedAnalyticsModules(config)` | GetSupportedAnalyticsModules — plugin catalog | Có |
| **(cần thêm) Motion detector plugin** | Emit `tns1:VideoSource/MotionAlarm` event | Thiếu |
| **(cần thêm) Tampering plugin** | 4 events: ImageTooBlurry / TooDark / TooBright / GlobalSceneChange | Thiếu |
| **(cần thêm) Analytics publisher** | Push AI results → core (MetadataStore) + dvr (RTP metadata) | Thiếu |
| **Object tracker output** | Bounding box, class, confidence → metadata stream | Có (framework) |

---

### 🗂️ core (Sơn)

| API mock | Profile T feature | Trạng thái |
|---|---|---|
| `getDeviceInfo()` | GetDeviceInformation — manufacturer/model/firmware/serial | Có |
| `getSystemDateAndTime / setSystemDateAndTime` | Get/SetSystemDateAndTime | Có |
| `reboot()` | SystemReboot | Có |
| `factoryReset(hard)` | SetSystemFactoryDefault | Có |
| `getAnalyticsRules / addAnalyticsRule / deleteAnalyticsRule` | Analytics rules CRUD (RuleEngine) | Có |
| `subscribe / unsubscribe / renewSubscription` | WS-BaseNotification / PullPoint subscription lifecycle | Có |
| **(cần thêm) publishEvent API** | AlarmManager nhận event từ vpu → distribute qua subscriptions | Thiếu |
| **MetadataStore query** | Lưu trữ + retrieve AI results (VMS query lịch sử) | Có (framework) |

---

### 🚪 gateway (Quyền)

| API mock | Profile T feature | Trạng thái |
|---|---|---|
| `getNetworkConfig / setNetworkConfig` | GetNetworkInterfaces / SetNetworkInterfaces / DNS / DefaultGateway | Có |
| **(cần thêm) User management** | GetUsers / CreateUsers / DeleteUsers / SetUser (SecurityCore auth DB) | Thiếu |
| **HTTP Digest / WS-Security store** | Credential store cho ONVIF module validate (module đã có logic, gateway lo persistence) | Thiếu |
| **(cần thêm) HTTPS / TLS termination** | HTTPS bindings (nếu tool test HTTPS) | Thiếu |
| **Discovery / Hostname** | WS-Discovery multicast responder + Get/SetHostname | Có (ONVIF module tự làm) |
| **OutputAdapters** | Webhook / MQTT / SIP alarm (không mandatory Profile T) | Có |

---

### ⚙️ PTZ (chưa gán chủ)

| API mock | Ghi chú |
|---|---|
| `ptzAbsoluteMove / RelativeMove / ContinuousMove / Stop` | Thường nằm **dvr** (control hardware qua serial/UART/PWM) |
| `getPtzStatus / gotoHomePosition / setHomePosition` | Profile T **KHÔNG mandatory PTZ** — chỉ cần nếu camera có PTZ head |

**Đề xuất**: gán PTZ vào **dvr** vì cần trực tiếp control hardware.

---

## 3. Sơ đồ luồng

### Motion detection event
```
[vpu]  AI plugin detect motion
   ↓ publish event
[core]  AlarmManager receive → route theo rule
   ↓ distribute
[ONVIF Module]  SubscriptionManager → PullPoint / BaseNotification
   ↓ SOAP
[VMS Client]
```

### Video stream
```
[dvr]  Capture → Encode → RTSP server (:554)
    ↑
[ONVIF Module]  GetStreamUri → return dvr's RTSP URL
    ↓ direct RTSP
[VMS Client]
```

### Metadata (AI analytics track)
```
[vpu]  AI results → Publisher
   ├→  [dvr]   RTP metadata track (song song video RTSP)
   └→  [core]  MetadataStore (persistence, query lịch sử)
```

### System management
```
[VMS Client]  →  SOAP request  →  [ONVIF Module]
                                       ↓ ICameraBackend
                                    [gateway / core]
                                       ↓
                                    [OS / Filesystem / DB]
```

---

## 4. Gap analysis theo service

| Service | Đã có sẵn (từ DVR v3.3-onvif-profile-S) | Cần bổ sung để pass Profile T |
|---|---|---|
| **dvr** | Capture, Encode (H.264/JPEG), RTSP, Imaging đầy đủ | Snapshot HTTP endpoint (P1), OSD render pipeline (P1), Metadata RTP track (P2) |
| **vpu** | Plugin framework (kế thừa `aox_ai_box`) | Motion plugin (P1), Tampering plugin (P2), Analytics Publisher (P2) |
| **core** | System info, DateTime, Reboot, Event framework | `publishEvent` API bridge (P1), AlarmManager wire vào SubscriptionManager (P1) |
| **gateway** | Network config, hostname | User management (P1), HTTP auth store (P1), HTTPS/TLS (P2) |

---

## 5. Implementation Roadmap

| Phase | Tasks | Effort | Dependency |
|---|---|---|---|
| **Phase 1: Adapter** | Viết `DvrBackend`, `VpuBackend`, `CoreBackend`, `GatewayBackend` implement `ICameraBackend`. Composite pattern hoặc single facade | 3-5 ngày | — |
| **Phase 2: IPC giữa services** | Định nghĩa gRPC / DBus / shared memory giữa ONVIF Module ↔ 4 services | 3-5 ngày | Phase 1 |
| **Phase 3: Bổ sung P1** | 1) Snapshot HTTP (dvr, 2 ngày). 2) OSD wire (dvr, 2-3 ngày). 3) publishEvent (core, 1 ngày). 4) Motion event (vpu, 3-4 ngày). 5) User mgmt (gateway, 2 ngày). | 10-12 ngày | Phase 2 |
| **Phase 4: Test integration** | Deploy 4 services + ONVIF module. Chạy DTT tool 24.12 với Errata 25.12 | 3-5 ngày | Phase 3 |
| **Phase 5: Bổ sung P2** | 1) Metadata RTP bridge (dvr+vpu). 2) Tampering plugin (vpu). 3) HTTPS (gateway) | 8-15 ngày | Phase 4 (optional) |
| **Phase 6: Certification** | Chuẩn bị hồ sơ ONVIF conformance | 3-5 ngày | Phase 4 hoặc 5 |

**Total effort**: ~4-6 tuần cho full Profile T + certification.

---

## 6. Ghi chú quan trọng

1. **DVR backend KHÔNG cần biết SOAP/XML** — chỉ implement `ICameraBackend` interface (C++ API). ONVIF Module tự lo SOAP layer.
2. **Bỏ `patchMediamtxPath()` trong `MediaLegacyHandler.cpp`** khi swap sang real DVR (mock-specific code — DVR đã có RTSP server thật).
3. **Không cần mediamtx** khi dùng real DVR — DVR đã có RTSP server (GStreamer) tự serve stream.
4. **Real camera có motion + tampering detection thực** → Profile T tree đầy đủ xanh, pass rate có thể ≥ 204/210 (hơn cả mock hiện tại).
5. **IPC giữa ONVIF module và 4 services**: cần chọn cơ chế (khuyến nghị gRPC hoặc DBus vì multi-language friendly + versioning tốt). Không dùng shared memory nếu 4 services chạy container riêng.
6. **`ICameraBackend` hiện là single interface** — với 4 services, có 2 approach:
   - **Approach A (facade)**: 1 backend adapter compose 4 IPC clients (dvr/vpu/core/gateway). ONVIF module không đổi.
   - **Approach B (multiple interfaces)**: Tách `ICameraBackend` thành `IStreamBackend`, `IAnalyticsBackend`, `ISystemBackend`, `INetworkBackend`. ONVIF module inject từng interface.

**Khuyến nghị Approach A** — ít invasive hơn cho ONVIF module đã hoạt động.

---

## 7. Feature status legend

- **Có sẵn** = DVR v3.3-onvif-profile-S đã implement, chỉ cần wire vào adapter
- **Có framework** = Data struct + skeleton có, cần fill logic thực
- **Thiếu** = Chưa có, cần implement từ đầu
- **P1** = Priority 1, bắt buộc để Profile T pass
- **P2** = Priority 2, nice to have (metadata streaming, tampering)
