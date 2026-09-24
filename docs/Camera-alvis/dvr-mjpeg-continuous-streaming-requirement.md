# Cần bổ sung MJPEG streaming liên tục qua RTSP cho ít nhất 1 kênh

## Bối cảnh

Camera này công bố hỗ trợ ONVIF Profile S. Theo đúng tài liệu chuẩn chính thức
của ONVIF (`ONVIF Profile S Specification v1.3`, mục 7.9, tải trực tiếp từ
[onvif.org](https://www.onvif.org/wp-content/uploads/2019/12/ONVIF_Profile_-S_Specification_v1-3.pdf)),
việc streaming MJPEG qua RTSP là yêu cầu **bắt buộc** (`Device MANDATORY`),
không phải tính năng tuỳ chọn:

```
7.9 Video Streaming – MJPEG
• Streaming of MJPEG video using RTSP.

7.9.1 Device requirements
• Device shall declare MJPEG Option in VideoEncoderConfigurationOptions.
• Device shall be able to stream MJPEG according to the Streaming Specification.

7.9.3 Video Streaming – MJPEG Function List for Devices
Video Streaming – MJPEG          Device MANDATORY
MJPEG Media streaming using RTSP  | Streaming | M
```

So sánh với H.264 trong CHÍNH tài liệu này (mục 8.2): H.264 chỉ là
`Device CONDITIONAL` ("if supported"). Tức là bản thân chuẩn Profile S coi
MJPEG mới là codec baseline bắt buộc, H.264 chỉ là bổ sung — ngược lại với
cảm giác thông thường bây giờ.

Camera hiện tại **không còn phát MJPEG liên tục** — chỉ còn API chụp 1 ảnh
tĩnh (`GetSnapshot`). Đây là điểm đang thiếu để đạt đúng chuẩn Profile S đầy
đủ, cần bổ sung lại.

## Việc cần làm — chỉ trong phạm vi source DVR, không cần đụng gì repo khác

Có 1 hệ thống khác (ONVIF SOAP server, repo riêng) sẽ tự đọc dữ liệu DVR trả
ra và convert sang chuẩn ONVIF — **không cần DVR quan tâm phần đó**. Việc của
DVR chỉ có đúng 3 phần dưới đây, tất cả nằm trong source DVR đang có sẵn.

### 1. Encode MJPEG liên tục cho ít nhất 1 kênh (không cần cả 8 profile)

Đã có sẵn `SnapshotWorker` (`include/snapshot_worker/snapshot_worker.h`,
`src/controller/dvr_controller_snapshot.cpp`) — bọc `NvJPEGEncoder` (mã hoá
JPEG bằng **hardware** Jetson, không phải software JPEG codec CPU, nên chi
phí encode liên tục thấp hơn nhiều so với lo ngại thông thường về MJPEG).
Hiện nó chỉ được gọi **1 lần duy nhất mỗi khi có request `GetSnapshot`**
(`SnapshotWorker::encode()`).

Cần thêm 1 vòng lặp gọi `encode()` liên tục (theo khung hình mới nhất,
`ctx->latest_frame_ref`, giống logic đang dùng cho snapshot) cho **ít nhất 1
kênh** (khuyến nghị: kênh có độ phân giải thấp nhất đang có, ví dụ `0_sub`
hoặc `0_third`, để giảm tải), rồi đẩy từng frame JPEG ra ngoài cho bước 2.

Lưu ý quan trọng đã ghi sẵn trong chính header file: *"NvJPEGEncoder keeps
pointers into the stack of the thread that created it... codec created, used
and destroyed only on this thread"* — vòng lặp continuous-encode nên chạy
trên đúng 1 thread riêng dành cho việc này (không gọi xen kẽ với luồng
snapshot one-shot hiện tại trên cùng instance).

### 2. Thêm 1 RTSP mount point stream JPEG liên tục

`src/stream_server/rtsp_server.cpp` (`RtspStreamServer`) đã có sẵn đúng
pattern cần dùng cho H.264/H.265 — chỉ cần thêm nhánh thứ 3 song song:

```cpp
// Pattern hiện có (rtsp_server.cpp, ~dòng 300-307), codec H265/H264:
if (codec == "h265")
    launch_str = "appsrc name=src is-live=true do-timestamp=true format=time "
                 "block=false max-latency=0 max-bytes=10485760 ! h265parse ! "
                 "rtph265pay name=pay0 pt=96 config-interval=1";
else
    launch_str = "appsrc name=src is-live=true do-timestamp=true format=time "
                 "block=false max-latency=0 max-bytes=10485760 ! h264parse ! "
                 "rtph264pay name=pay0 pt=96 config-interval=1";
```

Thêm nhánh JPEG tương tự:

```cpp
else if (codec == "jpeg")
    launch_str = "appsrc name=src is-live=true do-timestamp=true format=time "
                 "block=false max-latency=0 max-bytes=10485760 ! jpegparse ! "
                 "rtpjpegpay name=pay0 pt=26";
```

Ghi chú:
- `pt=26` là **payload type tĩnh, cố định theo chuẩn RTP** dành riêng cho
  JPEG (RFC 2435) — không phải số tự chọn như `pt=96` (dynamic) đang dùng cho
  H.264/H.265. Bắt buộc phải là `26`, không đổi số khác.
  Client ONVIF sẽ nhận diện qua SDP dòng `a=rtpmap:26 JPEG/90000`.
- `media_configure_cb` hiện tại lấy `appsrc` theo tên `"src"` và tự set caps
  theo codec (dòng ~15-29 cùng file) — cần thêm nhánh set caps đúng cho JPEG
  (`image/jpeg,framerate=...`) tương tự cách đang set cho H264/H265.
- Vòng lặp continuous-encode ở bước 1 đẩy buffer JPEG vào đúng `appsrc` này
  bằng `gst_app_src_push_buffer` — giống hệt cách H264/H265 đang push
  (`RtspStreamServer::add_appsrc`/callback push buffer, cùng file, ~dòng
  197-211 và ~450-477).
- Đặt path mount mới, ví dụ `/live/ch0_mjpeg` (không trùng path H264/H265 kênh
  đó).

### 3. `GET /dvr/v3.0/GetProfiles` phải có sẵn 1 entry với `Encoding: "JPEG"`

Response hiện tại (đã verify thật trên `.125`) có dạng:

```json
{
  "token": "0_sub",
  "Name": "ctx sub",
  "Type": "sub",
  "StreamUri": "rtsp://127.0.0.1:554/live/ch0",
  "VideoEncoderConfiguration": {
    "token": "0_sub_enc",
    "Encoding": "H264",
    "Resolution": {"Width": 1280, "Height": 720},
    "RateControl": {"FrameRateLimit": 25, ...}
  }
}
```

Có 2 cách để có 1 entry `"Encoding": "JPEG"`, **cách A rẻ hơn, ưu tiên dùng
nếu khả thi**:

**Cách A — đổi codec của 1 tier đang dư, không tạo entry mới (khuyến nghị)**

Mỗi kênh hiện có 4 tier: `main`, `sub`, `third`, `fourth`. Nếu tier
`third`/`fourth` của 1 kênh (ví dụ `0_fourth`) **không có tính năng nào khác
đang phụ thuộc** (VPU/analytics không lấy input từ tier đó, web UI không cần
nó), chỉ cần đổi `Encoding` của tier đó từ `"H264"` sang `"JPEG"` và trỏ
`StreamUri` sang mount JPEG mới ở bước 2 — **không cần thêm token/entry mới**,
tận dụng lại đúng slot đã có. Đỡ tốn công hơn hẳn cách B.

Tự kiểm tra trước: nếu không chắc tier nào đang rảnh, hỏi lại người quản lý
tính năng VPU/web UI trước khi đổi, tránh đổi nhầm tier đang được dùng.

**Cách B — thêm hẳn 1 entry mới (nếu cả 4 tier đều đang dùng, không đổi được)**

Thêm 1 entry mới cùng cấu trúc JSON trên, chỉ khác:
- `"Encoding": "JPEG"` (thay vì `"H264"`).
- `"StreamUri"` trỏ đúng path mount mới ở bước 2.
- `token` là 1 giá trị mới, chưa dùng (ví dụ `"0_mjpeg"`).

Dù chọn cách nào, không cần sửa field nào khác trong response, không cần
thêm endpoint mới.

## Tiêu chí nghiệm thu (cách tự kiểm tra trước khi báo hoàn thành)

1. `curl http://127.0.0.1:8200/dvr/v3.0/GetProfiles` trả thêm đúng 1 entry
   `"Encoding": "JPEG"` với `StreamUri` hợp lệ.
2. Mở thẳng URL đó bằng VLC hoặc `ffprobe rtsp://<ip>:554/live/ch0_mjpeg`:
   phải thấy stream **JPEG liên tục cập nhật** (không phải 1 ảnh đứng yên) —
   `ffprobe` phải báo được đúng `Video: mjpeg`.
3. `ffprobe` hoặc Wireshark xác nhận SDP trả về dòng `a=rtpmap:26 JPEG/90000`
   khi DESCRIBE vào path đó.
4. Luồng H.264/H.265 các kênh khác vẫn hoạt động bình thường, không bị gián
   đoạn khi bật thêm luồng JPEG này (test song song).

## Không cần làm

- Không cần thêm JPEG cho tất cả 8 profile hiện có — 1 kênh là đủ để đạt yêu
  cầu `Device MANDATORY` của chuẩn (ONVIF không quy định số lượng, chỉ cần
  có ít nhất 1 profile công bố hỗ trợ và stream được thật).
- Không cần động vào cơ chế Digest auth hiện tại của `rtsp_server.cpp` (đã
  hoạt động đúng, mount mới tự thừa hưởng qua `applyAuthPermissions`).
- Không cần sửa API `GetSnapshot` hiện tại (giữ nguyên, không liên quan).
