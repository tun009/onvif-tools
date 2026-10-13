# PTZ cho ONVIF/VMS — nghiên cứu Preset và Di chuyển (2026-10-13)

> Ghi lại kết quả tìm hiểu để làm sau. **Chưa code gì.** Phạm vi: phía VMS yêu cầu gì, ONVIF
> định nghĩa gì, DVR cũ / HAL / MGMT mới đã có gì, camera Jetson hiện tại (`.102`, `.194`) làm
> được gì. Mục nào chưa kiểm chứng được ghi `[chưa verify]`.

## 1. Bối cảnh

- Task: tích hợp PTZ vào VMS nội bộ (tương tự Milestone XProtect). Phía VMS cho biết **chủ yếu dùng
  Preset và Di chuyển**.
- Hình XProtect (Smart Client, chế độ Setup) cho thấy:
  - Preset nằm ở **Device → PTZ presets**, theo từng camera, hiển thị dạng `tên[số]`. Ví dụ camera
    `PTZ.TOANCANH_41052_VHI…` có `home[1]`, `zoom ra CT[2]`, `zoom vao CT[3]`. Tức dùng PTZ chủ yếu để
    **đổi mức zoom**.
  - Mục **Overlay buttons → PTZ** là các hành động gán cho nút phủ lên hình: Move up/down/left/right
    và 4 hướng chéo (kèm "while pressing"), Move home, Zoom in/out, Focus near/far/stop, Activate
    focus auto, Iris/Gain/Shutter up/down/reset, Aux 1–8 on/off, Pause patrolling, Toggle digital zoom.
- Camera của mình **chỉ có motor zoom/focus/iris, không có pan/tilt**.

## 2. Tính năng Milestone (theo tài liệu Milestone)

- Preset (lưu/đổi tên/xóa/gọi), Patrolling profile (tuần tra ≥ 2 preset, mặc định dừng 5 s mỗi điểm),
  ưu tiên PTZ theo vai trò (1–32000, mặc định 3000), phiên PTZ dành riêng (reserved), phân quyền
  (Manual PTZ, Activate presets/patrolling, Manage presets/patrolling, Reserve PTZ sessions).
- **Ưu tiên, reserved session, phân quyền, patrolling, digital zoom là logic của VMS**, không nằm
  trong giao thức ONVIF; camera chỉ nhận lệnh.
- Bản Milestone miễn phí XProtect Essential+ đã ngừng từ 2025 R2 (theo trang so sánh được dẫn lại);
  chưa xác nhận chính thức có PTZ patrolling ở các bản rẻ `[chưa verify]`.

## 3. ONVIF định nghĩa gì

Nguồn: `ptz.wsdl` và `onvif.xsd` chính thức trên onvif.org. Danh sách thao tác ngoài các mục đọc được
nguyên văn lấy từ trí nhớ `[chưa verify nguyên văn spec]`.

### Preset (`tt:PTZPreset`)

| Trường | Kiểu | Bắt buộc |
|---|---|---|
| `token` (attribute) | `ReferenceToken` | có |
| `Name` | `tt:Name` | không |
| `PTZPosition` | `tt:PTZVector` (= `PanTilt` x,y và `Zoom` x, mỗi phần có thể kèm `space`; cả hai tùy chọn) | không |

**Spec không lưu thêm gì khác** (không focus, iris, ảnh, thời gian). Thao tác:
- `SetPreset`: không có `PresetToken` → tạo mới; có token → ghi đè; có `PresetName`. **Thất bại nếu
  thiết bị đang di chuyển.**
- `GetPresets`, `GotoPreset` (kèm `Speed` tùy chọn nếu node khai speed space), `RemovePreset`.
- Home: `GotoHomePosition` (cần `HomeSupported=true`), `SetHomePosition` (lỗi nếu home cố định).
- Mức node (`tt:PTZNode`): `MaximumNumberOfPresets`, `HomeSupported`, `FixedHomePosition`, `GeoMove`,
  `AuxiliaryCommands`, `SupportedPTZSpaces`.
- Preset là tùy chọn có điều kiện: các thao tác chỉ cần khi node hỗ trợ ≥ 1 preset.

### Di chuyển

- `AbsoluteMove` (vị trí tuyệt đối; bỏ `Speed` thì lấy mặc định của PTZConfiguration).
- `RelativeMove` (chỉ cần khi node có ≥ 1 relative space).
- `ContinuousMove` (chỉ cần khi node có ≥ 1 continuous space; `Velocity`, `Timeout` tùy chọn; thiết bị
  phải khai khoảng Timeout trong `GetConfigurationOptions`; cách xử lý khi hết Timeout spec trong
  nguồn đọc được không giải thích rõ → kiểm bằng DTT).
- `Stop` (pan/tilt và/hoặc zoom), `GetStatus`. Tùy chọn: `GeoMove`, `MoveAndStartTracking` (spec ≥ 2023),
  tour (`GetPresetTours`…), `SendAuxiliaryCommand`.
- Không profile nào (S/T/M/G) bắt buộc PTZ; ở S và T là "if supported" (Phase 5, xem
  `docs/onvif-alvis/01-IMPLEMENTATION_PLAN.md` mục 8).

### Ánh xạ hành động Milestone ↔ ONVIF

| Hành động VMS | ONVIF |
|---|---|
| Joystick, "Move … while pressing" | `ContinuousMove` + `Stop` khi nhả |
| "Move …" không "while pressing" | có thể là bước ngắn (`RelativeMove` hoặc `ContinuousMove` có `Timeout`) `[suy luận]` |
| Zoom in/out | `ContinuousMove` (vận tốc zoom) hoặc `RelativeMove` |
| Move home | `GotoHomePosition` |
| Preset | `GetPresets` / `SetPreset` / `GotoPreset` / `RemovePreset` |
| Focus near/far/stop, Activate focus auto | Imaging Service: `Move`/`Stop`, `Focus.AutoFocusMode` |
| Iris/Gain/Shutter | Imaging Service: `Exposure` (Iris, Gain, ExposureTime) |
| Aux 1–8 | `SendAuxiliaryCommand` |
| Pause patrolling, digital zoom, ưu tiên, reserved | logic VMS |

## 4. DVR cũ (`D:\Elcom\DVR\dvr` và `D:\Elcom\DVR\ptz-cv25`) — **đã có đủ**

File `src/dvr_worker/api_service_worker.cpp`:
- Di chuyển: `PTZ/ContinuousMove`, `PTZ/AbsoluteMove`, `PTZ/RelativeMove`, `SetPtzStop`, `GetNodes`.
  - `ContinuousMove` pan/tilt: đổi vận tốc x,y thành 8 hướng (`UP/DOWN/LEFT/RIGHT` và 4 hướng chéo) rồi
    `setPTZManualDirection(direction, zoom_pos)`.
  - `ContinuousMove` zoom: **không chạy liên tục**; tính vị trí đích = `velocity × 50 + zoom hiện tại` rồi
    đặt vị trí đó. Đây là cách giả lập vận tốc zoom bằng bước vị trí.
- Preset: `PtzGetPresets`, `PtzGotoPreset`, `PtzRemovePreset`, `PtzUpdatePreset`, `v2.0/SetPreset`,
  `getPresetsV2`, `gotoPresetV2`, `removePresetV2`. `handlePTZPreset`: 0 lưu, 1 gọi, 2 xóa một, 3 xóa tất
  cả, 5 patrol theo vị trí. Preset lưu **index (token), tên, `is_home`, pan, tilt, zoom, speed** và **ảnh
  xem trước** (`ptz_preset_N.png`); ghi DB qua `addPtzPreset`. Có tour (`stopPTZPresetTour`).
- `GetNodes` khai: `MaximumNumberOfPresets = 2`, `HomeSupported = true`, `FixedHomePosition = true`,
  `GeoMove = true`; dải pan/tilt và zoom tuyệt đối -63..63, vận tốc zoom -10..10, hệ tọa độ generic.
- Dành cho camera có đầu quay thật (B803, ETC all-in-one, board PTZ_CV25).
- Web MGMT cũ (`NewVersion/frontend/MGMT/src/web/ui/src/pages/ptz`) gọi các endpoint này (`ptzPresetHandle`,
  `getPtzPresets`, `ptzPantrollingHandler`, `getPtzPantrollingList`, `manualControlPTZ`, `pointingPtz`).
- **DVR mới trên camera không còn các endpoint này**: `getPtzPresets`, `getPtzPantrollingList` trả 404
  trên `.102`, binary không chứa chuỗi PTZ nào.

## 5. HAL và MGMT mới (`D:\Elcom\NewVersion\frontend\MGMT\src\backend`) — **chưa có preset/vận tốc**

Kiến trúc: ONVIF server → HTTP → **MGMT** (REST `/mgmt/v1/...`, SQLite `mgmt.db`) → `dlopen("libhal.so")`
→ **HAL** (`lib/hal`, driver theo board) → phần cứng. `lib/bus` có kênh Control IPC (có PTZ trong mô tả)
nhưng onvif-module hiện đi MGMT qua HTTP.

| Hạng mục | HAL | MGMT |
|---|---|---|
| Zoom/focus/iris tuyệt đối | có (`ILensDriver`: `setZoom/Focus/Iris`, `get…`, `isBusy`, `stop`, `autoFocus`, `calibrate`) | có (`GET/PUT /Config/ZoomFocus`, `Lens`, `LensInfo`, `SelectedLens`) |
| Vận tốc / di chuyển liên tục | không | không |
| Pan/tilt | có **giao diện** `IPTZMotorDriver` (`panLeft/Right`, `tiltUp/Down`, `stop`) và driver `PtzPelcoDUart` (Pelco-D qua UART), chỉ board `ptzcv25` | không |
| Preset | giao diện có `gotoPreset(id)`/`setPreset(id)` (preset lưu trong động cơ ngoài) | **không** ("MGMT-BE has no preset store", `handoff_lens_api_mapping.md`) |
| Điều phối lệnh PTZ ngoại vi | `ptz_control` trong `ExternalPeripheralHAL_amba.cpp` còn `return false; // TODO`; bản Jetson không có PTZ | chưa có API |

Camera Jetson (`.102`, `.194`): zoom/focus/iris là động cơ bước qua PCA9635; không có motor pan/tilt.

## 6. Hiện trạng onvif-module

- PTZ node **chỉ có trục Zoom**; thật 11/28 thao tác: `GetServiceCapabilities`, `GetNodes`, `GetNode`,
  `GetConfigurations`, `GetConfiguration`, `GetConfigurationOptions`, `SetConfiguration`,
  `GetCompatibleConfigurations`, `AbsoluteMove`, `Stop`, `GetStatus`. 17 thao tác còn lại (preset, tour,
  home, `ContinuousMove`, `RelativeMove`…) trả `ter:ActionNotSupported`.
- DTT (r25): 9/12 case PTZ đạt; 3 case fail có chủ ý: `PTZ-3-1-4`, `MEDIA2_PTZ-1-1-3` (`ContinuousMove`) và
  `MEDIA2_PTZ-3-1-3` (Home). DTT coi chúng là bắt buộc ngay khi có PTZ service.
- ODM không hiện bảng PTZ trên camera này (profile chỉ có Live video / Video streaming / Imaging /
  Profiles): giả thuyết là `GetProfiles` của DVR không kèm `PTZConfiguration` `[chưa verify]`.
- Lỗi phía MGMT/HAL đã báo team: MGMT treo đồng bộ vài giây sau `AbsoluteMove` zoom quãng xa.

## 7. Việc cần quyết khi làm

1. Task thuộc **phía VMS** (code gọi ONVIF) hay **phía camera** (onvif-module/MGMT)?
2. Preset của camera mình = (zoom, focus) đã lưu; **focus không nằm trong `PTZPreset` của ONVIF**, nên chỉ
   là hành vi nội bộ nếu muốn nhớ focus.
3. Lưu preset ở đâu: MGMT (chưa có chỗ lưu), onvif-module hay DVR mới. Token dạng số tăng dần để Milestone
   hiện `[1] [2] [3]`; cần quyết `MaximumNumberOfPresets` (DVR cũ chỉ 2), bền qua restart, home cố định hay không.
4. `ContinuousMove` cho zoom: HAL/MGMT chưa có vận tốc; có thể giả lập bằng bước vị trí như DVR cũ, nhưng
   cần tính đến việc MGMT từng treo sau lệnh zoom xa.
5. Có cần Imaging `Move`/`Stop` (focus) và Exposure (iris, gain, shutter) cho overlay buttons không.
6. Hỏi VMS: preset lấy từ camera (`GetPresets`) hay VMS tự lưu? Camera PTZ thật họ đang dùng có pan/tilt không?

## 8. Công cụ kiểm thử

- ODM (miễn phí, GUI; cần profile có `PTZConfiguration`), script ONVIF (python-onvif-zeep, go-onvif,
  `curl` gọi `/onvif/ptz` kèm Digest), DTT (nhóm `PTZ-*`, `MEDIA2_PTZ-*`).
- Các công cụ này chỉ kiểm giao thức; ưu tiên PTZ/reserved/patrolling kiểm bằng VMS thật.

## Nguồn

- ONVIF PTZ WSDL: https://www.onvif.org/ver20/ptz/wsdl/ptz.wsdl
- ONVIF schema: https://www.onvif.org/ver10/schema/onvif.xsd
- ONVIF PTZ Service Spec v2606: https://www.onvif.org/specs/2606/ONVIF-PTZ-Service-Spec-v2606.pdf
- Milestone — Configuring and Using Enhanced PTZ: https://learn.milestonesys.com/configurexp_ptz.htm
- Milestone — Reserved PTZ sessions: https://www.milestonesys.com/globalassets/techcomm/2018-r1/sc/english-united-states/56161.htm
- Milestone — Patrolling profiles: https://doc.milestonesys.com/2024R1/en-US/feature_flags/ff_extendedptz/sc_managepatrollingprofi_56591_.htm
