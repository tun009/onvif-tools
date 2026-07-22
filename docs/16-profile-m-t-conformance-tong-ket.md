# Tổng kết Conformance Profile M + T — Bài học & Kinh nghiệm

> Cập nhật: 2026-07-22
> Trạng thái: **✅ ĐẠT 244/244 (0 failed)** — DTT 24.12, Profile **M + T + S** đều SUPPORTED.
> Report: `done-profile-m_t.xml` (ONVIF Device Test Tool 24.12).

Tài liệu này tổng kết toàn bộ quá trình đưa mock ONVIF camera pass conformance
Profile M + T, để làm **cẩm nang kinh nghiệm** cho các profile về sau (G / A / D / C)
hoặc khi thay mock bằng DVR thật.

---

## 1. Kiến trúc tổng thể (nhắc lại)

```
DTT Tool ──SOAP/HTTP:8080──► onvif-module (gSOAP)
                                  │ IPC Unix socket (/tmp/mock-camera.sock)
                                  ▼
                            mock-camera-backend (fake data + điều phối stream)
                                  │
                    ┌─────────────┴──────────────┐
                    ▼                             ▼
             mediamtx :8554                gortsplib relay :8555
        (RTSP gốc, API :19997,        (RTSP + RTSP-over-HTTP tunnel kiểu Apple,
         ffmpeg testsrc2 feed)         "stable description" cache SDP, auth)
                    │                             ▲
                    └── ffmpeg publish ───────────┘
                                  ▲
                    onvif-module :8080 raw-proxy tunnel ──► 8555
```

**2 repo:**
- `onvif-module` — tầng SOAP (ONVIF protocol), không biết hardware.
- `mock-camera-backend` — implement `ICameraBackend`, trả fake data + quản stream.
- Giao tiếp qua `include/interface/` (shared contract, **cấm sửa lệch 2 bên**).

**3 tầng cổng mạng cần phân biệt rõ:**
| Port | Ai phục vụ | Dùng cho |
|------|-----------|----------|
| 8080 | onvif-module (gSOAP) | SOAP + đồng thời là cổng RTSP-over-HTTP tunnel (DTT bắt buộc cùng port web service) |
| 8554 | mediamtx | RTSP gốc, phục vụ SDP tươi (fresh) — đổi resolution được |
| 8555 | gortsplib relay | RTSP + HTTP-tunnel, **cache SDP lần đầu** (không refresh động) |
| 19997 | mediamtx control API | PATCH runOnInit để đổi/khởi động lại stream |

---

## 2. Kết quả — phân loại vấn đề đã fix

Chia làm 2 nhóm lớn: **tầng SOAP** (dễ, sửa trong service) và **tầng hạ tầng
streaming/RTSP** (khó, đây là nơi tốn nhiều công nhất).

### 2.1. Nhóm SOAP-layer (Media2Service, MediaLegacyHandler)

| Case | Nguyên nhân | Fix |
|------|-------------|-----|
| MEDIA2-1-1-2 | DeleteProfile không fire event `ProfileChanged` | Fire `ProfileChanged` ở cả CreateProfile + DeleteProfile |
| MEDIA2-2-3-3 | GetProfiles nhúng resolution CŨ (stale) | Đọc backend live cùng logic với GetOptions |
| MEDIA2-2-2-5 / 2-3-4 | Set VideoSource/Encoder config không persist | Override store (`g_vscOverride`, `g_vecOverride`); Get echo lại CHỈ khi request không kèm ProfileToken |
| Event Preliminary FAILED | GetEventProperties prefix `tns1:` cả leaf topic | Chỉ prefix node gốc, leaf KHÔNG prefix (xem memory `event-topicset-prefix`) |

### 2.2. Nhóm hạ tầng streaming / RTSP (khó nhất)

| Case | Vấn đề gốc | Giải pháp cuối |
|------|-----------|----------------|
| MEDIA2_RTSS-1-1-2 / 4-1-2 (RtspOverHttp) | DTT yêu cầu URI tunnel **cùng port web service = 8080**; trả 8555 → fail check, trả 8080 → gSOAP 404 | onvif-module(8080) phát hiện tunnel request (GET/POST + path stream + header `x-rtsp-tunnelled`) → **raw-proxy socket sang 127.0.0.1:8555** (commit `bbd96e9`) |
| MEDIA2_RTSS tunnel chỉ ~2fps | Nagle + buffer nhỏ + 4K CRF quá nặng | TCP_NODELAY (`7da8b2e`) + SO_RCVBUF/SNDBUF 8MB (`8a0c8fd`) + cap /main 2Mbps (`fb50e8e`) → 72fps |
| MEDIA2-8-1-1 (metadata Set-Get) | onvif-module đọc HTTP body KHÔNG đủ Content-Length → XML cắt cụt → không lưu | Đọc đủ body bằng MSG_PEEK (non-consuming) trước dispatch (`ensureFullBody`, `237e222`) |
| **RTSS-1-1-48 (H.264 resolution)** — case cuối cùng | Xem mục 3 (chi tiết) | Option B: token-aware options + sub1 persist |

---

## 3. RTSS-1-1-48 — case khó nhất, và bài học lớn nhất

### 3.1. Test làm gì
DTT iterate qua **CẢ 3 H.264 encoder config** (main / sub1 / sub2). Với mỗi config:
lấy `ResolutionsAvailable` từ `GetVideoEncoderConfigurationOptions`, chọn
**highest / median / lowest**, set từng mức, PLAY stream, **decode frame thật** và
so resolution decode được với mức đã set.

### 3.2. Vì sao khó — giới hạn kiến trúc relay
- H.264 nhét resolution trong **SPS (SDP)**. Relay gortsplib (8555) dùng cơ chế
  **"stable description"** — cache SDP của lần DESCRIBE đầu tiên, **không rebuild
  được** khi stream đổi resolution.
- Nên nếu đổi resolution rồi PLAY qua relay → DTT vẫn decode ra resolution CŨ → mismatch → FAILED.

### 3.3. Những hướng đã THỬ và THẤT BẠI (đừng lặp lại)
1. **Reconfigure /main tại chỗ** (`reconfigure_stream.sh` kill+respawn ffmpeg):
   respawn ~1-2s quá chậm so với lúc DTT check frame (vẫn đọc frame 4K cũ); và
   một khi body-read fix mở khóa mọi Set request → /main bị respawn LIÊN TỤC →
   phá vỡ các streaming test khác (MEDIA2_RTSS chỉ còn 9 frame). → **REVERT**.
2. **Patch runOnInit /main dùng chung**: đụng relay + churn stream → hỏng case đang pass. → **REVERT**.

### 3.4. Giải pháp cuối — 2 phần bổ trợ nhau

**(a) Option D — pre-warm stream riêng cho main** (`32bba59`, `c5fc6f6`):
Tạo path `main640` (640x480) + `main720` (1280x720) chạy sẵn trong mediamtx.yml.
Media1 `GetStreamUri` route theo config hiện tại của main:
- 4K → `8555/main` (relay, cache 4K)
- 720 → `8554/main720` (mediamtx trực tiếp, SDP tươi)
- 640 → `8554/main640`

→ 3 mức của **main** đều decode đúng. KHÔNG đụng /main dùng chung → relay + tunnel không ảnh hưởng.

**(b) Option B — token-aware Options** (`9cc9f4f`, onvif) + **sub1 persist** (`b69d51c`, backend):
Vấn đề còn lại: DTT test cả **sub1/sub2**, mỗi cái cũng highest/median/lowest.
Trước đây `GetVideoEncoderConfigurationOptions` trả list cố định 4 resolution
(3840/1920/1280/640) cho **MỌI token** → sub1/sub2 bị test cả 4K/640, nhưng
sub1/sub2 không có stream đa resolution (relay chỉ phục vụ default 720/480).
Pre-warm thêm ~9 stream (sub1_4k, sub2_4k...) là **không bền vững**.

Fix gốc, sạch hơn: **mock quảng bá sai** — sub1 (720p) không nên khai báo hỗ trợ
4K. Cho Options **trả theo token**:
- `sub1` → chỉ `1280x720` | `sub2` → chỉ `640x480` → highest=median=lowest=default
  → DTT chỉ test đúng mức relay phục vụ được → **PASS**, và khớp
  `GetVideoEncoderConfigurations` (consistency).
- `main` / no-token → giữ full list (Option D đã lo).

Phụ: sub1 stream trước để `sub1: {}` rỗng (không `runOnInit`) → ffmpeg chết là
không tự dậy → 0 frame. Thêm `runOnInit` + `runOnInitRestart`.

### 3.5. Bài học rút ra từ RTSS-1-1-48
> **Khi hạ tầng không thể đáp ứng đúng cách test, hãy sửa cái mock QUẢNG BÁ, đừng
> cố ép hạ tầng.** Giảm `ResolutionsAvailable` về đúng năng lực stream thật vừa
> pass test vừa đúng bản chất (sub-stream 720p không nên khai 4K). Rẻ hơn và ít
> rủi ro hơn nhiều so với dựng cascade stream cho từng config × resolution.

---

## 4. Cạm bẫy vận hành (VẬN HÀNH — quan trọng cho lần sau)

1. **Thứ tự deploy**: LUÔN start **backend TRƯỚC**, onvif-server SAU. onvif kết nối
   IPC socket lúc khởi động; backend chưa lên → onvif thoát ("Connecting to mock
   backend..." rồi chết) → toàn bộ test Internal error.
2. **Dọn process cũ**: `pkill -f mock-camera-server` **có thể KHÔNG diệt được** (đã
   gặp 7 backend stale chồng nhau). Dùng `kill -9` theo PID lấy từ `pgrep -f`.
   Luôn verify `ps -C onvif-server,mock-camera-server` = đúng 1+1 và `ss -ltnp | grep :8080`.
3. **paramiko + nohup treo channel**: `nohup ... &` / `setsid ... &` làm
   `exec_command` chờ EOF → timeout. Chạy start rồi verify bằng kết nối RIÊNG
   (đừng đọc kết quả lệnh start).
4. **Verify build chỉ trên `/tmp`**: TUYỆT ĐỐI không `sftp.put` đè repo path trên
   server (user tự pull). Copy repo → `/tmp/build_check` → upload file sửa → `make full` → grep `error:`.
5. **Relay cache**: mediamtx (8554) đổi resolution được; relay (8555) cache SDP lần
   đầu. Cần SDP tươi → dùng 8554 trực tiếp. Cần tunnel/HTTP → qua 8555.
6. **Chạy riêng vs chạy full khác nhau**: nhiều case pass khi chạy riêng nhưng fail
   khi chạy full (state pollution, tải tunnel, phân mảnh TCP). Luôn xác nhận bằng
   **full run**, và tăng Operation Delay khi nghi timing.

---

## 5. Checklist khuyến nghị cho profile mới (G / A / D / C)

- [ ] Phân biệt rõ case là **SOAP-layer** hay **hạ tầng streaming** — ưu tiên soi
      SOAP trước (rẻ, an toàn).
- [ ] Với case streaming: xác định nó decode frame thật hay chỉ đọc SOAP. Nếu
      decode → chú ý giới hạn relay cache.
- [ ] Trước khi dựng hạ tầng phức tạp, hỏi: **có thể sửa cái mock quảng bá cho
      khớp năng lực thật không?** (bài học RTSS-1-1-48).
- [ ] Mỗi thay đổi: build-check `/tmp` → commit (report tên+ID) → push → pull →
      rebuild → restart đúng thứ tự → verify không regression bằng full run.
- [ ] Tìm "case sinh đôi" đang PASS trong cùng nhóm để làm blueprint (vd RTSS-1-1-46
      JPEG là bản mẫu cho RTSS-1-1-48 H.264).
- [ ] Không đụng vùng cấm (`interface/`, `external/gsoap/`, serve loop
      `OnvifServer.cpp`) trừ khi thực sự là core change.

---

## 6. Commit chính (chặng cuối)

| Commit | Repo | Nội dung |
|--------|------|----------|
| `bbd96e9` | onvif | RTSP-over-HTTP tunnel proxy 8080 → relay 8555 |
| `7da8b2e` / `8a0c8fd` | onvif | TCP_NODELAY + buffer 8MB cho tunnel |
| `fb50e8e` | backend | Cap /main 2Mbps (tunnel nhẹ, đủ frame) |
| `237e222` | onvif | ensureFullBody — đọc đủ HTTP body (MEDIA2-8-1-1) |
| `32bba59` / `c5fc6f6` | backend | main640 / main720 pre-warmed (Option D) |
| `b69d51c` | backend | sub1 stream persistent (runOnInit + runOnInitRestart) |
| `9cc9f4f` | onvif | Media1 VideoEncoderConfigurationOptions token-aware (Option B) |

Xem thêm: `15-profile-m-t-gortsplib-handover.md` (streaming stack), `11-profile-m-plan.md`,
`06-profile-t-conformance-plan.md`.
