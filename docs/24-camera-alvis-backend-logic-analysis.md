# Phân tích logic backend mới (Camera-alvis) từ sơ đồ khối kiến trúc

> Nguồn: `docs/Camera-alvis/so_do_khoi_kien_truc (1).svg`, `Tai_lieu_kien_truc_phan_mem_Camera_AI-V4_1.docx` (SAD v4.1), `Tai_lieu_SRS_Camera_AI.docx` (SRS v1.0). Đối chiếu với mock backend + onvif-module hiện tại.

## 1. Cách đọc sơ đồ

Sơ đồ chia hệ thống thành ba băng ngang và bốn loại luồng dữ liệu. Đọc đúng hai trục này là hiểu được toàn bộ logic.

Ba băng ngang, từ trên xuống:

```text
SERVICE CHỨC NĂNG : DVR · VPU · Core · Gateway   (+ MGMT nằm trên cùng, control plane)
SERVICE NỀN TẢNG  : BUS & IPC (Frame bus · Event bus · Control IPC)  ·  HAL
PHẦN CỨNG         : Image Sensor/ISP · Codec HW · NPU · Ngoại vi
```

Bốn loại luồng, phân biệt bằng màu:

```text
Xanh dương (data plane) : khung thô zero-copy, Sensor → bus → DVR/VPU
Cam (info plane)        : Detection/Event/metadata, VPU → Core → Gateway
Xanh lá (media out)     : media RTSP → Gateway → ra ngoài (Cloud/RTMP/MQTT/FTP)
Xám nét đứt (control)   : cấu hình/OTA/lệnh ngoại vi/ONVIF, MGMT → BUS → HAL → phần cứng
```

Nguyên tắc cốt lõi đọc được ngay từ sơ đồ: **phần cứng bị cô lập dưới HAL; các service nói chuyện qua BUS & IPC; mọi luồng ra ngoài phải đi qua Gateway.** Đây chính là tư tưởng “ONVIF là adapter mỏng trước nguồn dữ liệu” mà dự án mock đang theo.

## 2. Bảy khối và vai trò

| Khối | Loại | Vai trò trong luồng |
|---|---|---|
| HAL | Nền tảng | Trừu tượng Sensor, Codec (video+audio), Peripheral nội/ngoại, InferenceBackend |
| BUS & IPC | Nền tảng | Frame bus (khung), Event bus (detection/event), Control IPC (lệnh) |
| DVR | Chức năng | Capture → Encode → Record → Stream (kèm audio) |
| VPU | Chức năng | Nạp plugin AI, inference, tracker |
| Core | Chức năng | Rule engine, alarm, ghép metadata, analytics, retention |
| Gateway | Chức năng | Security core + output adapters (một cổng ra duy nhất) |
| MGMT | Chức năng | ConfigAPI (Web+API), ONVIF stack, OTA, LogDiag |

Ghi chú quan trọng từ sơ đồ: `CodecHAL` và `InferenceBackend` được liên kết **in-process** trong DVR và VPU, không đi qua IPC. Đây là quyết định hiệu năng: mỗi khung encode và mỗi lần suy luận không được phép trả giá cho serialize/copy qua ranh giới tiến trình.

## 3. Logic bốn luồng dữ liệu

### 3.1 Data plane — khung thô zero-copy (xanh dương)

Đường đi:

```text
Image Sensor/ISP → SensorHAL → CaptureThread(DVR-01) → Frame bus(BUS-01) → DVR-02 và VPU
```

Logic:

- SensorHAL cấp `FrameRef` zero-copy gắn `pts_us` tăng đơn điệu, có `dma_fd` để chia sẻ bộ nhớ không copy.
- `FramePool` (BUS-01) cấp buffer từ pool cố định, đếm tham chiếu, thu hồi khi `count=0`. Không malloc theo từng khung.
- Nhiều consumer cùng nhận một khung: DVR để encode/record, VPU để suy luận. Một producer, nhiều subscriber.
- Khi pool cạn: áp drop policy và đếm khung rớt (backpressure có kiểm soát), không chặn producer.

Đây là điểm khác biệt lớn nhất so với mock: mock dùng GStreamer/ffmpeg + MediaMTX sinh testsrc, còn Camera-alvis có pipeline sensor → frame bus thật với zero-copy.

### 3.2 Info plane — detection/event/metadata (cam)

Đường đi:

```text
VPU → Event bus(BUS-02) → Core → (metadata) → MGMT/ONVIF-05
                                → (alarm/event) → Gateway → ra ngoài
```

Logic:

- VPU chạy plugin AI trên khung, sinh `Detection{label, confidence, bbox, track_id, attrs}`.
- VPU-07 đẩy Detection/Event lên event bus theo đúng thứ tự `pts`, đồng thời map sang ONVIF metadata schema.
- Core (Rule engine) đánh giá luật ROI/vạch ảo/hướng/tốc độ trên Detection để sinh `Event`, nạp lại luật runtime.
- Core ghép metadata vào media đồng bộ theo `pts` và áp retention.
- Event bus non-blocking: subscriber chậm không chặn producer, hàng đợi giới hạn, drop bản tin cũ.

`Detection.attrs` và `Event.payload` là **điểm mở rộng mở**: thêm loại kết quả model mới không phải sửa event bus hay consumer không liên quan. Đây là chỗ khớp trực tiếp với ONVIF Profile M.

### 3.3 Media out — RTSP ra ngoài (xanh lá)

Đường đi:

```text
DVR-02 (encoded) → DVR-04 RtspStreamServer → Gateway → Cloud/RTMP/MQTT/FTP
```

Logic:

- DVR-04 phục vụ RTSP/RTP theo profile, cấp URI cho ONVIF Media.
- Mọi luồng media ra ngoài **bắt buộc** đi qua Gateway (TLS, xác thực, audit). Sơ đồ vẽ rõ media không đi thẳng ra Cloud mà vòng qua Gateway.
- RTSP hỗ trợ audio và backchannel hai chiều (talkback).

### 3.4 Control plane — cấu hình/OTA/lệnh/ONVIF (xám nét đứt)

Đường đi:

```text
VMS/ONVIF ↔ MGMT(ONVIF stack) → Control IPC(BUS-03) → HAL → Ngoại vi
ConfigAPI (Web+API) → BUS-03 → HAL
```

Logic:

- MGMT chứa ONVIF stack (ONVIF-01..07) tách khỏi ConfigAPI.
- Lệnh điều khiển đi qua Control IPC dạng `PeriphCmd{type, params}` tổng quát, ẩn transport (I2C/SPI/PWM/UART/RS485). Thêm loại lệnh mới không sửa tầng vận chuyển.
- ONVIF PTZ (Profile S) ánh xạ sang HAL ngoại vi; zoom/focus nội bộ qua HAL-03.
- Mọi request qua Control IPC phải được xác thực (SEC-01), có audit.

## 4. Logic từng service chức năng

### 4.1 DVR — đường ống media

```text
DVR-01 CaptureThread : Sensor → gắn metadata/pts → publish Frame bus
DVR-02 EncoderWorker : FrameRef → AvPacket (main + sub-stream), ghép audio đồng bộ A/V
DVR-03 RecordWriter  : AvPacket+metadata → file mp4/MJPEG + chỉ mục, xoay vòng, mã hóa at-rest, pre/post-event
DVR-04 RtspServer    : AvPacket → RTSP/RTP theo profile, cấp URI cho ONVIF
```

DVR-03 là khối **quan trọng nhất cho Profile G thật** và là thứ mock hoàn toàn không có: recorder + storage index thật. DoD của nó là “ghi 24h không mất gói, mã hóa at-rest”.

### 4.2 VPU — suy luận AI

```text
VPU-01 PluginManager       : quét manifest, nạp .so, đăng ký Registry, hot-reload
VPU-02 InferenceScheduler  : phân phối khung theo FPS plugin, gom batch theo NPU
VPU-0x Runner/Tracker      : chạy infer in-process qua InferenceBackend, gán track_id
VPU-07 MetadataMapper      : Detection → event bus + ONVIF tt:MetadataStream
```

Điểm mở rộng số một của hệ thống là `IModelPlugin`: thêm/bớt model runtime không gián đoạn ghi/stream. Đây là lý do kiến trúc tách VPU khỏi DVR.

### 4.3 Core — nghiệp vụ

```text
CORE-01 RuleEngine   : luật ROI/vạch ảo/hướng/tốc độ trên Detection → Event, nạp luật runtime
CORE-02 EventManager : quản lý event/alarm, cấp cho ONVIF Event
CORE-03 MetadataMux  : ghép metadata vào media theo pts, áp retention
```

Core là nơi biến “AI thấy gì” (Detection) thành “hệ thống quyết định gì” (Event/alarm), và là nguồn cho ONVIF Event (Profile T/M) và Recording metadata (Profile G).

### 4.4 Gateway — cổng ra an ninh

```text
GW-01 SecureEgress   : mọi payload ra ngoài qua secure() (mã hóa + được phép), fail → từ chối + audit
GW-0x OutputAdapters : IOutputAdapter cho RTMP/REST/MQTT/FTP/Cloud
```

Logic bất biến: không đường ra nào được bỏ qua Gateway. Đây là ràng buộc an ninh xuyên suốt (NDAA, FIPS, GDPR, IEC 62443).

### 4.5 MGMT — control plane và ONVIF

```text
MGMT-01 ConfigAPI  : Web UI + REST, validate, versioning/rollback cấu hình
MGMT-02 OnvifStack : ONVIF SOAP (Device/Media/PTZ/Recording/Analytics/Event/Security), tách ConfigAPI
MGMT-03 OtaUpdater : firmware A/B có chữ ký + rollback
MGMT-04 LogDiag    : log/metric/health tập trung
```

ONVIF-04 RecordingService (Profile G) phụ thuộc DVR-03 và CORE-03; ONVIF-05 AnalyticsMetadata (Profile M) phụ thuộc VPU-07. Bản đồ phụ thuộc này chính là nơi onvif-module hiện tại sẽ cắm vào.

## 5. Ánh xạ logic backend mới sang codebase hiện tại

| Thành phần ONVIF hiện tại | Nguồn dữ liệu ở mock | Nguồn dữ liệu ở Camera-alvis |
|---|---|---|
| getProfiles / getStreamUri | MediaMTX testsrc :8554 | DVR-04 RtspStreamServer |
| VideoEncoderConfig | mock state | DVR-02 qua CodecHAL |
| PTZ | mock trả về cứng | HAL-04 qua Control IPC `PeriphCmd` |
| Imaging | mock state | HAL Peripheral |
| Analytics/Metadata (M) | relay bơm RTP cố định | VPU-07 → Event bus → ONVIF-05 |
| Event (PullPoint) | MockSubscriptionManager | CORE-02 → ONVIF-06 |
| Recording/Search (G) | dữ liệu tĩnh Recording_0 | DVR-03 index thật |
| Replay (G) | relay chế từ /main + 0xABAC | DVR-03 archive reader + packetizer |

Nhìn bảng này thấy rõ: onvif-module đóng vai MGMT-02, còn DVR/VPU/Core/HAL là “ruột” của backend thật thay cho `MockCameraBackend`.

## 6. Khác biệt logic then chốt so với mock

Mock hiện tại có `ICameraBackend` gồm Device/Media2/PTZ/Imaging/Analytics/Events, giao tiếp qua IPC nhị phân 16 byte + JSON. Camera-alvis thêm ba tầng logic mà mock chưa mô hình hóa:

1. **Data plane zero-copy thật.** Frame bus + FramePool + `dma_fd` thay cho testsrc. Ảnh hưởng hiệu năng lớn, là điều kiện để chạy AI in-process.
2. **Info plane AI thật.** VPU sinh Detection thật, Core sinh Event thật. Mock bơm metadata cố định. Đây là nguồn thật cho Profile M và Event.
3. **Recorder + storage thật (DVR-03).** Mock không có khối này. Đây là mảnh còn thiếu để Profile G đọc archive theo Range thay vì phát live.

Ngoài ra Camera-alvis đặt **Gateway** làm cổng ra bắt buộc và **Control IPC tổng quát** (PeriphCmd) — hai thứ mock chưa có, ảnh hưởng tới cách ONVIF media/replay và PTZ được nối dây.

## 7. Rủi ro logic cần lưu ý khi tích hợp

```text
1. In-process vs IPC: CodecHAL/InferenceBackend PHẢI in-process. Nếu onvif-module gọi
   media qua IPC JSON như hiện tại thì chỉ hợp cho control/metadata, KHÔNG cho hot path khung.
2. Ownership phần cứng: SAD chọn topology 6 process, một HAL daemon sở hữu phần cứng.
   Cần định nghĩa thứ tự khởi động (backend/HAL trước, onvif sau) như bài học ở mock.
3. Replay per-session: DVR-03 archive reader phải có cursor riêng mỗi RTSP session,
   khác với replay state global hiện tại trong relay.
4. Gateway chắn luồng ra: ONVIF media/replay phải tôn trọng Gateway TLS/auth, không mở cổng thẳng.
5. Audio bắt buộc: SRS đặt audio là Must; khi bật audio thật, hành vi Search filter no-audio
   hiện tại phải đổi.
```

## 8. Kết luận

Logic backend mới là một pipeline camera AI hoàn chỉnh gồm bốn mặt phẳng: data plane (khung zero-copy), info plane (AI detection/event), media out (RTSP qua Gateway) và control plane (cấu hình/ONVIF/lệnh ngoại vi). ONVIF nằm gọn trong MGMT như một adapter, đúng mô hình mà dự án mock đã dựng.

So với mock, ba khối làm nên khác biệt logic là Frame bus zero-copy, VPU/Core sinh dữ liệu AI thật, và DVR-03 recorder + storage. Ba khối này quyết định tính khả thi của việc thay backend, đặc biệt cho Profile M (metadata thật) và Profile G (archive thật). Chi tiết lộ trình thay dần theo phase đã trình bày ở `docs/23`.