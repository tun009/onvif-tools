# 18 — Profile G: Khảo sát Replay endpoint (2026-07-28)

Nối tiếp `17-profile-g-plan.md`. Tài liệu này chốt kết quả verify để làm **replay streaming** — phần rủi ro cao nhất của Profile G. Nguồn: đọc source `main.go`, `IpcServer.cpp`, `BackendConnector.cpp`, `IpcProtocol.h` + gortsplib v5.6.1 source + ONVIF Streaming Spec Ver 26.06.

## 1. Đường đi request (3 mắt xích)

- **DTT → onvif-module (SOAP :8080):** `OnvifServer.cpp:544` route theo pathPrefix → service manual-XML (mẫu `AnalyticsService`). 3 service Profile G: Recording `/onvif/recording`, Search `/onvif/search`, Replay `/onvif/replay`.
- **onvif-module ↔ backend (IPC Unix socket):** header nhị phân **16 byte cố định** `{magic 0x4F4E5646 "ONVF", version u8, msgType u8, flags u16, requestId u32, payloadLen u32}` + payload **JSON**. Enum `MsgType` ở `include/interface/ipc/IpcProtocol.h` — **file trong `interface/` = vùng sync 2 repo, CẤM lệch**. Client: `BackendConnector::sendRequest(MsgType, json)`. Server: `IpcServer::dispatch()` switch → `backend_->method()`.
- **Streaming:** GStreamer/ffmpeg (testsrc) → mediamtx :8554 → gortsplib-relay :8555 (`main.go`, file Go DUY NHẤT).

## 2. Quyết định kiến trúc quan trọng nhất

`GetReplayUri` ở tầng SOAP **chỉ trả về URI chuỗi tĩnh** dạng `rtsp://<ip>:8555/replay?token=...` — y hệt pattern `getStreamUri` hiện tại; streaming thật thì client tự cắm vào :8555.

→ **KHÔNG cần thêm MsgType IPC mới** cho luồng replay stream. **KHÔNG đụng `interface/`** (vùng cấm). Toàn bộ độ khó dồn gọn vào `main.go`.

## 3. `main.go` — hiện trạng & điểm mở rộng

- `OnPlay` (dòng 68–71): chỉ log + `return ok()` — KHÔNG parse header, KHÔNG Range, KHÔNG extension.
- `startMetadata()` (dòng 190–201): **nguyên mẫu bơm RTP thủ công** — tự dựng `&rtp.Packet{Header:..., Payload:...}` rồi `st.WritePacketRTP`. Đây chính là cơ chế replay sẽ mở rộng.
- `paths` map ở `main()` (dòng 204): thêm `"replay"` vào đây.

## 4. gortsplib v5.6.1 — API đủ dùng, không cần thư viện mới

- `ServerHandlerOnPlayCtx{Session, Conn, Request *base.Request, Path, Query}` — có `Request.Header` → đọc được `Range`, `Require`, `Rate-Control`, `Scale`.
- `ServerHandlerOnSetupCtx{Session, Conn, Request, Path, Query, Transport}`.
- pion/rtp v1.10.2 (đã có sẵn): set `pkt.Header.Extension = true`, `pkt.Header.ExtensionProfile = 0xABAC` + raw payload. Cùng struct `rtp.Packet` mà `startMetadata` đang dùng.

## 5. Byte-layout RTP ext 0xABAC (Streaming Spec §6.3) — CHỐT

Extension profile ID = **0xABAC**, length = **3 words** (12 byte content):

| Trường | Kích thước | Ý nghĩa |
|---|---|---|
| NTP timestamp | 8 byte | 64-bit fixed-point, UTC wallclock của frame (đo tại frame grabber **trước** encode); phải tăng đơn điệu |
| Byte cờ | 1 byte | `C`=0x80 sync/clean point (I-frame); `E`=0x40 cuối đoạn ghi liên tục; `D`=0x20 sau discontinuity (dùng reverse); `T`=0x10 terminal (hết data track); các bit còn lại mbz=0 |
| Cseq | 1 byte | low-order byte của Cseq trong lệnh PLAY khởi tạo |
| padding | 2 byte | cho đủ 3 words |

- Ext **chỉ cần ở gói RTP ĐẦU của mỗi access-unit (frame)**, không cần ở gói sau.
- Scope đã **bỏ reverse** → bit `D` thực tế không dùng; chỉ set `C` ở I-frame, `E`/`T` ở frame cuối.

## 6. Header DTT gửi khi PLAY replay (Spec §6.4)

```
PLAY rtsp://.../replay RTSP/1.0
Cseq: 123
Session: 12345678
Require: onvif-replay          <- server BẮT BUỘC accept SETUP+PLAY, KHÔNG được trả 551
Range: clock=20090615T114900.440Z   <- CHỈ absolute UTC clock; format: 8DIGIT "T" 6DIGIT ["." 1-9DIGIT] "Z"
Rate-Control: no               <- vắng = "yes" (server điều tốc, real-time RTP timing); "no" = gửi nhanh nhất, client điều tốc
```

Response PLAY (§5.2.2.3) nên kèm: `Range: clock=...` (start time) + `RTP-Info` với `rtptime=` khớp start time. Theo driver Milestone: nếu có cả 0xABAC ext lẫn Range-header method thì client ưu tiên 0xABAC.

## 7. Replay caps (trp `GetServiceCapabilities`)

`ReversePlayback=false` (bỏ reverse), `SessionTimeoutRange`, `RTP_RTSP_TCP=true`.

## 8. Còn phải verify khi bắt tay code (rủi ro cao — không chốt được qua lý thuyết)

1. `ServerStream.WritePacketRTP` có **tôn trọng** Extension fields tự set không, hay overwrite seq/ssrc/ext → **phải test thật** trên server.
2. Bắt gói DTT Replay test case thật (Wireshark/log) đối chiếu byte-layout trước khi tin 100%.
3. DTT có bắt buộc `RTP-Info` trong PLAY response không.
