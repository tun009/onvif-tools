# Phân tích chi tiết từng service Camera-alvis (kèm ví dụ minh họa)

> Nguồn: `docs/Camera-alvis/so_do_khoi_kien_truc (1).svg`, `Tai_lieu_kien_truc_phan_mem_Camera_AI-V4_1.docx` (SAD v4.1, ch.1-7), `Tai_lieu_SRS_Camera_AI.docx` (SRS v1.0). Đối chiếu mock backend + onvif-module hiện tại. Tiếp nối `docs/23` (khả thi migration) và `docs/24` (logic 4 luồng).

## 0. Cách đọc tài liệu này

`docs/24` giải thích **luồng dữ liệu** (4 mặt phẳng ngang). Tài liệu này đi **dọc từng khối**: mỗi service làm gì, nhận gì, trả gì, chạy mấy thread, hỏng thì sao, và một ví dụ đời thực để dễ nhớ.

Hệ thống có **7 khối**: 2 nền tảng (HAL, BUS & IPC) đỡ 5 khối chức năng (DVR, VPU, Core, Gateway, MGMT). Nguyên tắc xuyên suốt: **khối trên chỉ gọi khối dưới qua interface có version, không ai cầm con trỏ thô hay chạm phần cứng của người khác.**

```text
        ┌─────────────────────────────────────────────┐
        │  DVR    VPU    Core    Gateway    MGMT        │  ← 5 chức năng
        ├─────────────────────────────────────────────┤
        │  HAL          │   BUS & IPC                   │  ← 2 nền tảng
        ├─────────────────────────────────────────────┤
        │  Sensor/ISP · Codec HW · NPU · Ngoại vi       │  ← phần cứng
        └─────────────────────────────────────────────┘
```

Một câu tóm tắt mỗi khối để cầm theo khi đọc:

```text
HAL      : người phiên dịch giữa phần mềm và phần cứng
BUS&IPC  : hệ thống đường ống + bưu điện nội bộ
DVR      : ê-kíp quay phim (thu, nén, lưu, phát)
VPU      : bộ não AI (nhìn khung, nhận ra vật)
Core     : quản đốc ra quyết định (thấy vật → có báo động không)
Gateway  : bảo vệ cổng (mọi thứ ra ngoài phải qua đây)
MGMT     : lễ tân + phòng điều hành (web, ONVIF, OTA, log)
```

---

## 1. HAL — Hardware Abstraction Layer (nền tảng)

### Vai trò một câu

HAL là **người phiên dịch duy nhất** biết nói chuyện với phần cứng. Ba con chip khác nhau (Jetson của NVIDIA, Ambarella, Rockchip) nói ba "thứ tiếng" khác nhau; HAL dịch hết về một interface chung để 5 khối trên không cần biết bên dưới là chip gì.

### Ví dụ minh họa

> Hãy tưởng tượng bạn thuê tài xế. Bạn chỉ nói "đến sân bay". Bạn không quan tâm xe số sàn hay số tự động, xe xăng hay điện. Tài xế (HAL) lo hết. Đổi xe (đổi từ Jetson sang Rockchip) thì thay tài xế, còn bạn — ông chủ (DVR/VPU) — không đổi một câu lệnh nào.

Đây chính là yêu cầu `FR-EXT-4`: thêm một dòng chip mới chỉ cần thêm thư mục `hal/<nền tảng>/`, các service chức năng giữ nguyên.

### Năm khối con

```text
HAL-01 SensorHAL             : mở cảm biến, cấp FrameRef zero-copy gắn pts
HAL-02 CodecHAL*             : nén/giải nén video (H.264/H.265/MJPEG) + audio (G.711/AAC/Opus)
HAL-03 InternalPeripheralHAL : ngoại vi TRONG camera — IR cut, zoom/focus, sưởi, gạt nước
HAL-04 ExternalPeripheralHAL : ngoại vi NGOÀI camera — PTZ motor, relay, cảm biến cửa
HAL-05 InferenceBackend*     : nạp & chạy model AI trên NPU (TensorRT/RKNN/CVflow)
```

`*` = **bắt buộc in-process**. Đây là ràng buộc bất biến quan trọng nhất của cả kiến trúc: CodecHAL nằm chung tiến trình DVR, InferenceBackend nằm chung tiến trình VPU. Lý do: mỗi khung encode và mỗi lần suy luận là dữ liệu rất lớn; nếu đẩy qua ranh giới tiến trình (IPC) sẽ phải copy/serialize — SAD gọi đây là "lỗi thiết kế tốn kém nhất cần tránh".

### Điểm thiết kế đắt giá: PeriphCmd

HAL-03/04 không có hàm `setFocus()`, `setHeater()` riêng lẻ. Thay vào đó **một** hàm tổng quát:

```cpp
struct PeriphCmd {
    std::string type;                    // "set_focus","set_zoom","set_heater","ptz_move"
    std::map<std::string, Value> params; // {"level":3} hoặc {"pos":120}
};
bool exec(const PeriphCmd&);             // transport (I2C/SPI/UART/PWM) ẩn bên trong
```

> Ví dụ: web bấm "bật sưởi mức 3" → `PeriphCmd{"set_heater",{"level":3}}`. Camera đời này nối sưởi qua I2C, đời sau qua GPIO — bên gọi không đổi. Đây là `FR-EXT-3`: đổi transport I2C↔SPI khu trú trong HAL, ONVIF/ConfigAPI không biết.

### Đối chiếu mock

Mock **không có** HAL. PTZ/Imaging trong mock trả giá trị cứng. Ở Camera-alvis, PTZ đi qua HAL-04, zoom/focus qua HAL-03, ánh xạ từ ONVIF-03. Đây là chỗ `ptz*` trong `ICameraBackend` sẽ nối xuống `PeriphCmd` thật.

---

## 2. BUS & IPC — hạ tầng giao tiếp (nền tảng)

### Vai trò một câu

BUS & IPC là **hệ thần kinh**: đường ống truyền khung hình (to, nhanh), bưu điện truyền sự kiện (nhỏ, nhiều), và điện thoại truyền lệnh (hỏi-đáp).

### Ba khối con — ba loại "đường"

```text
BUS-01 Frame bus  : ống dẫn khung hình zero-copy (shared memory)  — data plane
BUS-02 Event bus  : bưu điện pub/sub cho Detection/Event          — info plane
BUS-03 Control IPC : tổng đài hỏi-đáp cho lệnh/cấu hình (có xác thực) — control plane
```

### Ví dụ minh họa — vì sao tách ba đường

> **Frame bus** như băng chuyền trong nhà máy: một khung hình đặt lên băng, nhiều người cùng lấy (DVR để nén, VPU để phân tích) mà **không ai photocopy** khung đó. Đó là "zero-copy": chỉ chuyền tham chiếu (`FrameRef`), đếm xem còn mấy người đang cầm; khi count=0 thì thu hồi buffer về pool. Không cấp phát bộ nhớ mới cho từng khung (`NFR-PERF-4`).
>
> **Event bus** như nhóm chat: VPU đăng "thấy 1 xe ở làn 2", ai subscribe topic đó thì nhận (Core, Gateway). Người đọc chậm không làm nghẽn người đăng — hàng đợi đầy thì bỏ tin cũ (`FR-BUS02-1`).
>
> **Control IPC** như gọi điện tổng đài: "cho tôi đổi bitrate" → chờ trả lời OK/lỗi. Mọi cuộc gọi phải xác thực trước (`SEC-01`).

### Cơ chế backpressure (điểm hay bị bỏ sót)

Khi Frame bus cạn buffer (consumer chậm, tải cao), BUS-01 **áp drop policy và đếm khung rớt** thay vì chặn producer. Camera 24/7 không được phép dừng vì một consumer nghẽn. Đây là `FR-BUS01-2`.

### Đối chiếu mock

Mock dùng **IPC nhị phân 16 byte + JSON** (`IpcProtocol.h`, header `MsgHeader` 16 byte, magic `0x4F4E5646` "ONVF"). Đó tương ứng **chỉ BUS-03 (Control IPC)** của Camera-alvis. Mock **không có** Frame bus và Event bus thật — đây là khoảng trống lớn nhất về hiệu năng khi lên production.

---

## 3. DVR — đường ống media (chức năng)

### Vai trò một câu

DVR là **ê-kíp làm phim**: thu khung, nén, lưu ra file, và phát trực tiếp cho VMS.

### Bốn khối con — dây chuyền tuần tự

```text
DVR-01 CaptureThread    : lấy khung từ SensorHAL, gắn pts/channel, đẩy lên Frame bus
DVR-02 EncoderWorker    : FrameRef → AvPacket (luồng chính + sub-stream), ghép audio A/V
DVR-03 RecordWriter     : AvPacket → file mp4/MJPEG + chỉ mục, xoay vòng, mã hóa at-rest
DVR-04 RtspStreamServer : AvPacket → RTSP/RTP, cấp URI cho ONVIF Media
```

### Ví dụ minh họa

> DVR-01 là **người quay** giơ máy thu hình. DVR-02 là **phòng dựng** nén file cho nhẹ, làm hai bản: bản HD (main) và bản xem nhanh (sub). DVR-03 là **kho lưu trữ** cất băng vào kệ có đánh số (chỉ mục), kệ đầy thì ghi đè băng cũ nhất (xoay vòng), băng khóa két (mã hóa at-rest — tháo ổ cứng ra máy khác không đọc được). DVR-04 là **phòng chiếu trực tiếp** ai muốn xem live thì mở RTSP.

### DVR-03 — khối quan trọng nhất cho Profile G

DVR-03 là thứ **mock hoàn toàn không có**. DoD của nó: "ghi 24h không mất gói, mã hóa at-rest". Nó tạo ra bản đồ:

```text
recording → segment → [start, end] → codec → offset trong file
```

Chính bản đồ này (storage index) là nguồn thật cho `GetRecordings`, `FindRecordings`, và cho replay đọc theo Range. Mock hiện dựng timeline giả và phát live `/main`; production đọc segment đã ghi theo thời gian.

### Đối chiếu mock

```text
Mock hiện tại                         Camera-alvis
─────────────────────────────────────────────────────────
GStreamer/ffmpeg testsrc              DVR-01 + SensorHAL (khung thật)
—                                     DVR-02 qua CodecHAL (encode HW thật)
MediaMTX :8554 → relay :8555          DVR-04 RtspStreamServer
dữ liệu tĩnh Recording_0              DVR-03 storage index thật
relay chế /replay từ /main            DVR-03 archive reader (đọc theo Range)
```

---

## 4. VPU — bộ não AI (chức năng)

### Vai trò một câu

VPU là **bộ não thị giác**: nhìn từng khung, nhận ra vật là gì (xe, người, biển số), theo dõi vật qua các khung.

### Điểm mở rộng số một: plugin

Điểm thiết kế quan trọng nhất của cả hệ thống: mỗi ứng dụng AI là **một plugin nạp động** theo `IModelPlugin`. Thêm model mới (phát hiện cháy, đội mũ bảo hiểm, đếm xe) **không sửa bất kỳ service nào khác**.

```cpp
class IModelPlugin {
    virtual PluginCaps caps() const = 0;            // input format, target_fps, cần tracking?
    virtual bool init(const Config&, IInferenceBackend*) = 0;
    virtual std::vector<Detection> process(const FrameRef&) = 0;  // ← ranh giới ổn định
    virtual void destroy() = 0;
};
```

### Ví dụ minh họa

> VPU như **phòng chuyên gia**. Mỗi chuyên gia (plugin) giỏi một việc: một người chỉ nhận xe, một người chỉ đọc biển số, một người chỉ nhận khuôn mặt. Muốn thêm chuyên gia "phát hiện cháy"? Chỉ cần **tuyển thêm một người** (thả thư mục plugin vào `/opt/ai/models/`, gọi API reload) — cả phòng vẫn làm việc bình thường, không ai phải học lại. Đó là hot-reload (`FR-VPU01-1`).

### Bảy khối con

```text
VPU-01 PluginManager      : quét manifest, nạp .so, đăng ký Registry, hot-reload
VPU-02 InferenceScheduler : chia khung theo target_fps từng plugin, gom batch cho NPU
VPU-03 ModelWorkerPool    : chạy infer trên N worker qua InferenceBackend
VPU-04 PreProcessor       : thư viện resize/letterbox/chuẩn hóa — plugin GỌI TÙY CHỌN
VPU-05 PostProcessor      : thư viện NMS/decode box — plugin GỌI TÙY CHỌN
VPU-06 Tracker            : gán track_id liên khung (SORT/ByteTrack)
VPU-07 ResultPublisher    : Detection → Event bus + ONVIF tt:MetadataStream
```

Điểm tinh tế: VPU-04/05 **không phải stage bắt buộc**. Model có tiền/hậu xử lý lạ tự làm trong `process()`. Ranh giới ổn định duy nhất là chữ ký `process(FrameRef) → vector<Detection>`. Nhờ vậy "bán kính ảnh hưởng" khi thêm model = đúng một thư mục plugin.

### Kết quả tự mô tả — khớp thẳng Profile M

```cpp
struct Detection { std::string label; float confidence; Rect bbox; int track_id; Attributes attrs; };
struct Event     { std::string type; int64_t ts_us; ...; Json payload; };
```

`Detection.attrs` (biển số, embedding, màu) và `Event.payload` (cấu trúc mới) là **điểm mở rộng mở**: thêm loại kết quả không cần sửa event bus hay consumer. VPU-07 map các trường này sang `tt:MetadataStream` cho ONVIF-05. Đây là chỗ Profile M cắm vào tự nhiên.

### Đối chiếu mock

Mock bơm metadata RTP **cố định** (relay phát định kỳ). Camera-alvis có nguồn thật: VPU sinh Detection → VPU-07 map schema → ONVIF-05 phát. Yêu cầu conformance: giữ đúng thứ tự pts, schema hợp lệ — trùng đúng điều Profile M kiểm.

---

## 5. Core — quản đốc nghiệp vụ (chức năng)

### Vai trò một câu

Core biến **"AI thấy gì"** (Detection) thành **"hệ thống quyết định gì"** (Event/alarm). VPU nói "có một chiếc xe ở tọa độ X"; Core nói "chiếc xe đó vượt vạch cấm → báo động".

### Bốn khối con

```text
CORE-01 RuleEngine    : luật ROI/vạch ảo/hướng/tốc độ trên Detection → Event, nạp luật runtime
CORE-02 AlarmManager  : cooldown/khử trùng lặp/lịch/ưu tiên — chống spam cảnh báo
CORE-03 MetadataStore : ghép metadata vào media theo pts, áp retention
CORE-04 Analytics     : đếm/lưu lượng/heatmap theo cửa sổ thời gian
```

### Ví dụ minh họa

> VPU là **camera an ninh** chỉ biết "có người ở khu A". Core là **trưởng ca** đọc nội quy: "khu A sau 22h có người = báo động; trong giờ hành chính thì kệ". RuleEngine cầm cuốn nội quy (đổi luật không cần khởi động lại máy). AlarmManager là người **lọc báo động trùng**: một người đi qua đi lại 10 lần trong 5 giây → chỉ hú còi một lần (cooldown), không làm phiền bảo vệ.

### Vì sao AlarmManager quan trọng

Không có cooldown/khử trùng, một vật đứng yên trong ROI sẽ sinh hàng nghìn event/giây → ngập event bus, ngập VMS. `FR-CORE02-1`: "không sinh cảnh báo trùng trong cooldown".

### Đối chiếu mock

Core là nguồn cho **ONVIF-06 Event** (Profile T/M) và **Recording metadata** (Profile G qua CORE-03). Mock dùng `MockSubscriptionManager` trả event giả; production nối CORE-02 → ONVIF-06 với event thật.

---

## 6. Gateway — cổng ra an ninh (chức năng)

### Vai trò một câu

Gateway là **cổng ra duy nhất**. Mọi dữ liệu rời khỏi camera (lên cloud, MQTT, FTP, RTMP) **bắt buộc** đi qua đây để mã hóa, xác thực, ghi audit.

### Ba khối con

```text
GW-01 SecurityCore    : TLS/mTLS, kiểm allowlist đích, ghi audit — điểm thô→mã hóa
GW-02 AdapterRegistry : nạp IOutputAdapter động + bảng định tuyến + sở hữu Outbox
GW-03 OutputAdapters  : RTMP/REST/MQTT/FTP/Cloud — mỗi giao thức một adapter
```

### Ví dụ minh họa

> Gateway như **cổng hải quan sân bay**. Không kiện hàng nào rời nước mà không qua hải quan: kiểm giấy tờ (xác thực), niêm phong (mã hóa), ghi sổ (audit). Muốn gửi hàng bằng hãng mới (thêm giao thức MQTT)? Chỉ đăng ký thêm một quầy (adapter), không xây lại cả sân bay (`FR-GW02-1`).

### Outbox — điểm thiết kế đắt giá

GW-02 sở hữu **Outbox bền (store-and-forward)**: mọi payload ghi xuống đĩa **trước khi** gửi. Mất mạng 5 phút rồi khôi phục → mọi event tới đích **đúng một lần**, không mất không trùng (at-least-once + dedup bằng idempotency key = hiệu quả exactly-once). Bền qua cả reboot/mất điện. Media lớn thì Outbox **chỉ lưu tham chiếu** (record id + [t0,t1]), bytes ở lại kho DVR, và Gateway **pin** đoạn ghi để DVR retention không xóa mất trước khi gửi xong.

### Phân biệt push vs pull (điểm hay nhầm)

Gateway lo luồng **ĐẨY** chủ động ra ngoài. Còn **ONVIF (MGMT) và RTSP (DVR) là server để VMS KÉO về** — đường pull đó do MGMT/DVR phục vụ trực tiếp, không qua adapter Gateway, nhưng vẫn tuân chính sách bảo mật chung. Đây là điểm cần chốt khi tích hợp: ONVIF endpoint nằm trước hay sau Gateway (câu hỏi mở ở `docs/23` §6.5).

### Đối chiếu mock

Mock **không có** Gateway; mở cổng RTSP trực tiếp. Production phải đưa media/replay tôn trọng Gateway TLS/auth. Ràng buộc tuân thủ: NDAA 889, FIPS 140-2/3, GDPR, IEC 62443.

---

## 7. MGMT — control plane & ONVIF (chức năng)

### Vai trò một câu

MGMT là **lễ tân + phòng điều hành**: web UI để người dùng cấu hình, ngăn xếp ONVIF cho VMS, cập nhật firmware, thu log/metric.

### Bốn khối con

```text
MGMT-01 ConfigApi   : Web UI + REST, validate, versioning/rollback cấu hình — KHÔNG gồm ONVIF
MGMT-02 OnvifStack  : ngăn xếp ONVIF SOAP (Device/Media/PTZ/Recording/Analytics/Event/Security)
MGMT-03 OtaUpdater  : firmware A/B có chữ ký + anti-rollback, không brick
MGMT-04 LogDiag     : log/metric/health tập trung
```

Điểm tách bạch quan trọng: **ONVIF (MGMT-02) tách khỏi ConfigAPI (MGMT-01)**. ConfigAPI không phụ thuộc ONVIF; một cái chết không kéo cái kia.

### Ví dụ minh họa

> MGMT-01 là **quầy lễ tân** cho chủ nhà (admin đăng nhập web đổi cấu hình). MGMT-02 là **phòng phiên dịch quốc tế** nói tiếng ONVIF chuẩn để mọi VMS (Milestone, Genetec...) hiểu. MGMT-03 là **thợ nâng cấp** thay firmware kiểu A/B: cài bản mới lên phân vùng B, lỗi thì quay về A — máy không bao giờ thành "cục gạch". MGMT-04 là **phòng giám sát** theo dõi sức khỏe từng service.

### MGMT-02 chính là chỗ onvif-module cắm vào

Đây là mấu chốt của toàn bộ dự án migration. Bản đồ phụ thuộc ONVIF:

```text
ONVIF-01 Device/Discovery  → MGMT-01, SEC-01
ONVIF-02 Media (S,T)       → DVR-02, DVR-04
ONVIF-03 PTZ (S)           → HAL-04 (PTZ ngoài), HAL-03 (zoom/focus nội)
ONVIF-04 Recording (G)     → DVR-03, CORE-03
ONVIF-05 AnalyticsMeta (M) → VPU-07, BUS-02
ONVIF-06 Event             → CORE-02, ONVIF-01
ONVIF-07 Security          → SEC-02, SEC-04
```

`onvif-module` của dự án mock đóng đúng vai **MGMT-02/OnvifStack**. Các khối DVR/VPU/Core/HAL là "ruột" backend thật thay cho `MockCameraBackend`.

### Đối chiếu mock

Mock đã có `DeviceService`, `MediaService`, `PTZService`, `AnalyticsService`, và 3 service Profile G (Recording/Search/Replay) đạt 313/313 DTT. Toàn bộ tầng SOAP này **tái sử dụng nguyên vẹn** — chỉ đổi nguồn dữ liệu phía sau IPC từ `MockCameraBackend` sang `AlvisCameraBackend`.

---

## 8. Ghép lại: một khung hình đi hết vòng đời

Để thấy 7 khối phối hợp, theo chân một khung hình có chiếc xe vượt đèn đỏ:

```text
1. Sensor/ISP thu ánh sáng
2. HAL-01 SensorHAL     → FrameRef zero-copy (pts=T), đẩy lên...
3. BUS-01 Frame bus     → phát cho HAI consumer cùng lúc:
   ├─ 4a. DVR-02 EncoderWorker → nén → DVR-03 ghi file + DVR-04 phát RTSP live
   └─ 4b. VPU-02 Scheduler → VPU-03 infer → "xe, conf 0.96, bbox, track_id=42"
5. VPU-06 Tracker       → gán track_id=42 ổn định qua các khung
6. VPU-07 ResultPublisher → đẩy Detection lên...
7. BUS-02 Event bus     → Core nhận
8. CORE-01 RuleEngine   → "track 42 vượt vạch khi đèn đỏ" → sinh Event
9. CORE-02 AlarmManager → chưa có cảnh báo trùng trong 30s → cho qua
10. Rẽ hai nhánh:
    ├─ 10a. ONVIF-06 Event   → VMS nhận notification (pull)
    ├─ 10b. ONVIF-05 Metadata → VMS đọc tt:MetadataStream (pull)
    └─ 10c. Gateway GW-01    → mã hóa → GW-03 MQTT đẩy cảnh báo lên cloud (push)
11. Muốn xem lại sau: ONVIF-04 Recording → DVR-03 archive reader đọc segment quanh T
```

Một khung, bốn số phận: **ghi (DVR-03), phát live (DVR-04), phân tích (VPU), đẩy cảnh báo (Gateway)** — tất cả từ một `FrameRef` zero-copy duy nhất, không copy.

## 9. Bảng tra nhanh: khối ↔ ONVIF ↔ mock

| Khối Camera-alvis | Phục vụ ONVIF | Tương đương ở mock |
|---|---|---|
| HAL-01 SensorHAL | (nguồn khung) | testsrc GStreamer |
| HAL-02 CodecHAL | ONVIF-02 encoder cfg | — (mock không encode thật) |
| HAL-03/04 Peripheral | ONVIF-03 PTZ | ptz* trả cứng |
| HAL-05 InferenceBackend | (nền VPU) | — |
| BUS-01 Frame bus | — | — (thiếu) |
| BUS-02 Event bus | ONVIF-06 nguồn | — (thiếu) |
| BUS-03 Control IPC | (mọi lệnh) | IPC 16 byte + JSON |
| DVR-02/04 | ONVIF-02 Media | MediaMTX + relay |
| DVR-03 RecordWriter | ONVIF-04 Recording (G) | dữ liệu tĩnh Recording_0 |
| VPU-07 ResultPublisher | ONVIF-05 Metadata (M) | RTP metadata cố định |
| CORE-02 AlarmManager | ONVIF-06 Event | MockSubscriptionManager |
| CORE-03 MetadataStore | ONVIF-04 metadata (G) | — |
| Gateway | (chính sách egress) | — (mở cổng thẳng) |
| MGMT-02 OnvifStack | **toàn bộ SOAP** | **onvif-module (đạt 313/313)** |

## 10. Ba điều rút ra

Thứ nhất, **onvif-module = MGMT-02**, gần như không phải đổi. Toàn bộ tri thức SOAP/Profile G khó nhất đã giải quyết xong (313/313); migration là đổi nguồn dữ liệu phía sau IPC, không viết lại tầng giao thức.

Thứ hai, **ba khối làm nên khác biệt** so với mock là những khối tạo dữ liệu thật: Frame bus zero-copy (BUS-01), VPU/Core sinh AI thật (nguồn Profile M/Event), và DVR-03 recorder + storage index (nguồn Profile G archive). Ba khối này quyết định tính khả thi thay backend.

Thứ ba, **hai ràng buộc bất biến** phải tôn trọng khi nối dây: CodecHAL/InferenceBackend luôn in-process (không đẩy khung/tensor qua IPC), và mọi luồng ĐẨY ra ngoài phải qua Gateway (còn ONVIF/RTSP là đường pull do MGMT/DVR phục vụ trực tiếp). Lộ trình thay dần theo 6 phase đã trình bày ở `docs/23`.
