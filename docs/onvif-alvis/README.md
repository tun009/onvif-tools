# ONVIF–Alvis Integration

## 1. Mục đích tài liệu

Thư mục này là điểm bắt đầu bắt buộc cho developer hoặc AI agent khi làm việc với việc tích hợp `onvif-module` vào backend Camera-alvis mới.

Mục tiêu của dự án là giữ nguyên tầng giao thức ONVIF/gSOAP đã đạt conformance Profile S, T, M và G, đồng thời thay thế dần `mock-camera-backend` bằng dữ liệu và hành vi thật từ các service Camera-alvis: MGMT, DVR, VPU, Core, HAL và BUS & IPC.

Dự án **không viết lại ONVIF stack** và **không chuyển toàn bộ backend Camera-alvis vào onvif-module**. Công việc chính là xây dựng các adapter ổn định phía sau interface backend của ONVIF.

## 2. Bối cảnh

Hệ thống cũ có một tiến trình DVR lớn, đồng thời cung cấp nhiều API nội bộ. ONVIF server cũ chạy như một module/process riêng và chuyển đổi SOAP ONVIF thành các lời gọi HTTP tới DVR.

### 2.1 ONVIF server legacy đã pass Profile S

Source legacy nằm tại:

```text
D:\Elcom\Ovif-mock\onvif_server
```

Đây là ONVIF server dùng gSOAP đã được tích hợp với DVR cũ và đã pass ONVIF Profile S. Nó là bằng chứng thực tế cho mô hình tách ONVIF thành repo/process riêng:

```text
VMS / ONVIF Test Tool
        │ SOAP/WS-Discovery
        ▼
legacy onvif_server
        │ HTTP/JSON nội bộ
        ▼
DVR cũ :8200
```

Source này phải được giữ làm tài liệu tham chiếu cho:

- Behavior Profile S đã từng pass với camera/DVR thật.
- Mapping SOAP operation sang DVR API legacy.
- WS-Discovery, WS-Security/Digest và Media/Profile behavior.
- So sánh regression khi thay DVR cũ bằng Camera-alvis mới.

Không copy nguyên kiến trúc phụ thuộc hoặc endpoint hardcode của legacy server sang production mới. Tầng `onvif-module` mới đã mở rộng và pass S/T/M/G; phần cần kế thừa từ legacy là behavior đã kiểm chứng và bài học tích hợp với backend thật.

Kiến trúc Camera-alvis mới tách trách nhiệm thành bảy khối:

```text
HAL          Hardware abstraction
BUS & IPC    Frame bus, event bus và control IPC
DVR          Capture, encode, live stream, recording và playback
VPU          AI inference, tracking và analytics metadata
Core         Rule engine, alarm/event và metadata nghiệp vụ
Gateway      Egress bảo mật tới hệ thống bên ngoài/cloud
MGMT         Config API, ONVIF stack, OTA và diagnostics
```

MGMT được chia logic thành:

```text
MGMT-01 ConfigApi    Web UI, REST API và quản lý cấu hình
MGMT-02 OnvifStack   ONVIF SOAP, discovery, security và profile behavior
MGMT-03 OtaUpdater   Firmware update
MGMT-04 LogDiag      Log, metrics và diagnostics
```

Repo MGMT hiện tại chủ yếu đang triển khai MGMT-01. Repo `onvif-module` đóng vai trò MGMT-02 và nên tiếp tục tồn tại độc lập ở cấp source repository/process.

## 3. Các repository liên quan

```text
D:\Elcom\Ovif-mock\projects\onvif-module
    Tầng ONVIF SOAP/gSOAP đã pass Profile S/T/M/G.

D:\Elcom\Ovif-mock\projects\mock-camera-backend
    Backend giả lập phục vụ development và conformance regression.

D:\Elcom\Ovif-mock\onvif_server
    ONVIF gSOAP legacy đã pass Profile S với DVR cũ; dùng làm baseline tham chiếu.

D:\Elcom\NewVersion\frontend\MGMT
    MGMT-01 ConfigApi và Web UI của kiến trúc mới.

D:\Elcom\DVR\dvr
    Hệ thống DVR cũ; dùng để đối chiếu hành vi, API và database legacy.
```

## 4. Kiến trúc hiện tại với mock

```text
ONVIF Test Tool / VMS
        │ SOAP/HTTP, WS-Discovery, RTSP
        ▼
onvif-module
        │ ICameraBackend + IPC Unix socket
        ▼
mock-camera-backend
        ├── Fake device/media/PTZ/imaging/event state
        └── MediaMTX/GStreamer/gortsplib cho RTSP và metadata/replay mock
```

`onvif-module` chịu trách nhiệm:

- SOAP/XML và gSOAP.
- WS-Discovery.
- HTTP Digest và WS-Security.
- ONVIF service routing.
- SOAP Fault.
- Quy tắc và behavior cần thiết để pass Profile S/T/M/G.
- Chuyển canonical backend data thành ONVIF XML.

`mock-camera-backend` chịu trách nhiệm:

- Cung cấp dữ liệu giả qua `ICameraBackend`.
- Mô phỏng trạng thái device, media, imaging và PTZ.
- Sinh event/analytics giả.
- Cung cấp live, metadata và replay stream giả.

## 5. Kiến trúc production mục tiêu

```text
VMS / NVR / ONVIF Test Tool
        │
        ▼
onvif-server (MGMT-02)
 ├── DeviceService
 ├── Media1/Media2Service
 ├── ImagingService
 ├── PtzService
 ├── EventService
 ├── AnalyticsService
 ├── RecordingService
 ├── SearchService
 ├── ReplayService
 └── Backend facade
       ├── MGMT adapter      → device/config/network/date-time/users
       ├── DVR adapter       → profiles/RTSP/recording/search/replay
       ├── HAL adapter       → PTZ/peripheral/imaging hardware
       ├── VPU adapter       → detections/analytics metadata
       ├── Core adapter      → rules/alarms/events/metadata index
       └── Identity adapter  → ONVIF/RTSP accounts and RBAC
```

Repo riêng không quyết định transport. Các adapter có thể dùng REST, Unix socket hoặc BUS/IPC tùy service sở hữu dữ liệu. Host, port và socket path phải lấy từ runtime configuration; không hardcode trong ONVIF service.

### 5.1 Quyền sở hữu cấu hình Network và Discovery

MGMT sở hữu desired state của network protocol, Discovery Mode và scopes.
Implementation hiện tại persist chúng trong file CWD-relative
`./mgmt_network_config.json` qua `JsonNetworkRepository`; đây là chi tiết nội
bộ của MGMT, không phải file cấu hình dùng chung giữa các repo.

`onvif-module` dùng `onvif.conf` để bootstrap listener, MGMT endpoint và fallback
runtime values. Ở giai đoạn tích hợp sau, module lấy configured state qua
MGMT adapter/API/IPC; module không đọc trực tiếp `mgmt_network_config.json`.

Developer/AI agent làm Network hoặc Discovery phải đọc phần tương ứng trong
`01-IMPLEMENTATION_PLAN.md`, source `domains/network` của MGMT và file runtime
`mgmt_network_config.json` tại working directory thật để phân biệt configured,
applied và advertised state.

## 6. Ownership theo domain

| ONVIF domain | Chủ sở hữu dữ liệu/hành vi thật |
|---|---|
| Device identity, network, date/time | MGMT |
| User/ONVIF account | MGMT + Security/Identity |
| Media profiles, encoder, stream URI | DVR |
| Live RTSP | DVR media output |
| Imaging configuration | MGMT + HAL/ISP |
| PTZ, zoom/focus, peripherals | HAL qua control IPC |
| Analytics metadata | VPU |
| Rules, alarm và event | Core + BUS event |
| Recording, track và job | DVR |
| Recording search | DVR index + Core metadata |
| Replay URI và replay stream | DVR playback |

`onvif-module` sở hữu **giao thức và mapping**, không sở hữu camera pipeline, AI inference, recorder hoặc hardware.

## 7. Hiểu đúng về stream, metadata và alarm

Ba khái niệm phải được tách rõ:

```text
Video stream     H.264/H.265 frames qua RTP/RTSP.
Metadata stream  ONVIF XML analytics/PTZ/event metadata qua RTP/RTSP.
Event channel    ONVIF PullPoint/Event Service hoặc MQTT nếu hỗ trợ.
```

Một RTSP session có thể có video track và metadata track. Alarm không phải một field bắt buộc trong video stream. Alarm được biểu diễn bằng ONVIF Event gồm Topic, UtcTime, PropertyOperation, Source, Key và Data.

`Application` là context nghiệp vụ Camera-alvis, không phải field bắt buộc chung của ONVIF. Nếu cần truyền, nó phải được map vào Event Source/Data hoặc namespace extension đã khai báo, không thay thế cấu trúc ONVIF chuẩn.

## 8. Profile và nguồn backend

| Profile | Trọng tâm | Backend thật chính |
|---|---|---|
| S | Device, Media, live video, imaging/PTZ/event cơ bản | MGMT, DVR, HAL, Core |
| T | Media2, advanced video, H.265, metadata | DVR, MGMT/HAL, VPU |
| M | Analytics metadata, rules và events | VPU, Core, BUS |
| G | Recording, Search và Replay | DVR, Core metadata |

Một operation có thể xuất hiện trong nhiều profile. Profile không xác định process sở hữu dữ liệu; profile xác định behavior và capability ONVIF mà thiết bị công bố.

## 9. Các nguyên tắc bất biến

1. Không sửa generated gSOAP code bằng tay trừ quy trình generate/patch đã được kiểm soát.
2. Không đặt lời gọi HTTP, database, HAL hoặc socket trực tiếp trong ONVIF service handler.
3. Mọi truy cập backend đi qua interface/adapter.
4. Không hardcode IP, port, token, profile hoặc device identity trong production.
5. Không quảng bá capability mà backend thật không thực hiện được.
6. Không fallback sang mock âm thầm trong production.
7. Giữ mock backend để chạy conformance regression lâu dài.
8. Token phải ổn định và thống nhất giữa Media, Metadata, Recording, Search và Replay.
9. Lỗi backend phải được map sang SOAP Fault phù hợp.
10. Mỗi capability chỉ được coi là migrated khi đã pass unit, integration, DTT và VMS test tương ứng.
11. **Mọi chỉnh sửa code/config đều phải thực hiện ở local rồi commit + push, sau đó `git pull` xuống camera — không được sửa trực tiếp file trên camera.** Sửa trực tiếp trên server tạo local diff không kiểm soát, gây xung đột khi `git pull` lần sau (đã xảy ra thật với `onvif.conf` trên `.125`: ai đó từng sửa tay `interface = eth0` và xoá vài dòng comment trực tiếp trên server, để lại file `.bak`, khiến lần pull kế tiếp bị chặn vì "local changes would be overwritten"). Trường hợp 1 giá trị thực sự chỉ đúng cho riêng 1 máy (ví dụ `device_ip`, `discovery.interface` — tên card mạng khác nhau giữa các máy), xử lý bằng `git stash` trước khi pull rồi `git stash pop` lại sau, không sửa tay rồi bỏ qua git.

## 10. Chế độ chạy trong giai đoạn migration

```text
mock
    Tất cả capability dùng mock; dùng cho conformance baseline.

hybrid
    Mỗi capability chọn real/mock bằng config; dùng trong development.

production
    Chỉ backend thật; capability chưa sẵn sàng phải không được advertise
    hoặc trả Fault rõ ràng. Không trả dữ liệu giả.
```

Ví dụ:

```ini
[backend]
mode=hybrid

[capabilities]
device=real
network=real
media=real
imaging=real
ptz=mock
analytics=mock
events=mock
recording=real
search=real
replay=real
```

## 11. Cách bắt đầu cho AI agent mới

Trước khi sửa code:

1. Đọc file này.
2. Đọc `01-IMPLEMENTATION_PLAN.md`.
3. Đọc `02-INTEGRATION_MATRIX.md` và xác định capability đang làm.
4. Đọc tài liệu profile/conformance liên quan trong thư mục `docs` cha.
5. Đọc service handler, backend interface và mock implementation tương ứng.
6. Xác định service Camera-alvis thật đang sở hữu dữ liệu.
7. Không sửa SOAP behavior đã pass nếu thay đổi chỉ thuộc backend adapter.
8. Chạy regression test của mock trước và sau thay đổi.
9. Cập nhật integration matrix và ghi rõ evidence test.

## 12. Tài liệu trong thư mục này

- `README.md`: bối cảnh, mục đích và ranh giới kiến trúc.
- `01-IMPLEMENTATION_PLAN.md`: các phase triển khai và acceptance gate.
- `02-INTEGRATION_MATRIX.md`: bảng theo dõi operation/capability mock → real.

## 13. Quy ước đặt tên tài liệu

`README.md` luôn là entry point và không đánh số. Các tài liệu triển khai, phân tích, quyết định kiến trúc và handoff tạo sau phải có tiền tố số hai chữ số:

```text
README.md
01-IMPLEMENTATION_PLAN.md
02-INTEGRATION_MATRIX.md
03-<TEN-TAI-LIEU>.md
04-<TEN-TAI-LIEU>.md
...
```

Quy tắc:

1. Số thể hiện thứ tự hình thành/đọc, không tái sử dụng số của tài liệu đã xóa.
2. Tên file dùng chữ hoa hoặc kebab-case nhất quán, không dùng dấu cách.
3. Khi đổi tên phải cập nhật mọi liên kết trong README và tài liệu liên quan.
4. Mỗi file ghi mục đích, phạm vi, trạng thái, ngày cập nhật và nguồn code được đối chiếu.
5. Tài liệu quyết định kiến trúc nên ghi rõ `Proposed`, `Accepted`, `Superseded` hoặc `Deprecated`.
6. AI agent phải đọc các file theo thứ tự số trước khi sửa code hoặc tạo tài liệu tiếp theo.
