# RTSP live streaming: cần bật Digest auth bắt buộc trên MediaMTX

## Bối cảnh

Đang tích hợp ONVIF service với DVR thật. Chạy ONVIF Device Test Tool (DTT), toàn bộ Media2 SOAP layer (GetProfiles, VideoSource/VideoEncoder Configuration, SnapshotUri, RtspOverHttp URI/port/tunnel) đã **PASS**.

**Chỉ còn đúng 1 vấn đề, thuộc RTSP server thật (MediaMTX) phía DVR:**

| Test case | Lỗi |
|---|---|
| MEDIA2_RTSS-1-1-1 (UDP), MEDIA2_RTSS-1-1-2 (RtspOverHttp), MEDIA2_RTSS-1-1-3 (RTSP/TCP) | `Digest authentication is mandatory for Profile T and Profile M` — RTSP DESCRIBE trả `200 OK` thẳng, không có `401 + WWW-Authenticate: Digest` |

## Vấn đề: RTSP DESCRIBE không yêu cầu Digest auth

DVR dùng **MediaMTX** làm RTSP server thật cho live stream (xác nhận qua `lib/mediamtx_api/mediamtx_api.cpp`: DVR gọi HTTP API `localhost:9997` để đăng ký path, không tự viết RTSP server). MediaMTX mặc định cho phép `read` không cần auth trừ khi cấu hình tường minh trong `mediamtx.yml`.

**Cần bật Digest bắt buộc trên MediaMTX** — 2 cách, tùy khả năng MediaMTX bản đang dùng:

```yaml
# Cách 1: danh sách user tĩnh (đơn giản nhưng phải tự đồng bộ tay với MGMT
# mỗi khi có account mới/đổi mật khẩu)
authInternalUsers:
  - user: <username>
    pass: <password>
    permissions:
      - action: read
        path: ~^live/.*$
rtspAuthMethods: [digest]
```

```yaml
# Cách 2 (khuyến nghị): external HTTP auth callback, nếu bản MediaMTX hỗ trợ
authMethod: http
authHTTPAddress: http://<mgmt-host>/<endpoint-xac-thuc-rtsp>
rtspAuthMethods: [digest]
```

Cách 2 tốt hơn vì không phải đồng bộ tay danh sách user — MediaMTX tự hỏi MGMT mỗi lần có kết nối, MGMT luôn trả lời đúng theo tài khoản đang có tại thời điểm đó.

## Giao thức `authHTTPAddress` — flow cụ thể kèm ví dụ payload

Theo tài liệu chính thức MediaMTX ([mediamtx.org/docs/features/authentication](https://mediamtx.org/docs/features/authentication)): khi bật `authMethod: http`, mỗi lần có client kết nối, MediaMTX **POST** một JSON payload tới `authHTTPAddress`. Payload gồm: `user`, `password`, `token`, `ip`, `action` (`publish`/`read`/`playback`/...), `path`, `protocol` (`rtsp`/...), `id`, `query`, `userAgent`. Server xác thực trả **status 2xx** = cho phép, khác đi = từ chối.

Vì RTSP Digest yêu cầu client phải bị "hỏi" trước (client không tự gửi mật khẩu ngay từ đầu), flow thực tế gồm 2 lượt gọi tới MGMT cho **cùng một kết nối RTSP**:

**Lượt 1 — client vừa mới DESCRIBE, chưa có credential:**

```
DESCRIBE rtsp://192.168.8.125:554/live/ch100 RTSP/1.0
CSeq: 1
```

```json
POST http://<mgmt-host>/<endpoint-xac-thuc-rtsp>
{
  "user": "",
  "password": "",
  "token": "",
  "ip": "192.168.8.50",
  "action": "read",
  "path": "live/ch100",
  "protocol": "rtsp",
  "id": "a1b2c3d4-...",
  "query": "",
  "userAgent": "ONVIF RTSP Client 25.12"
}
```

→ MGMT phải trả **401** ở lượt này (không phải 403/200) — đây là tín hiệu để MediaMTX biết cần thách thức Digest với client, không phải để từ chối hẳn.

**MediaMTX tự sinh và gửi challenge cho client** (client không thấy gì từ MGMT, đây là nội bộ MediaMTX):

```
RTSP/1.0 401 Unauthorized
WWW-Authenticate: Digest realm="...", nonce="...", qop="auth"
```

**Client gửi lại DESCRIBE kèm Digest response:**

```
DESCRIBE rtsp://192.168.8.125:554/live/ch100 RTSP/1.0
CSeq: 2
Authorization: Digest username="admin", realm="...", nonce="...",
  uri="rtsp://192.168.8.125:554/live/ch100", response="...", qop=auth, nc=..., cnonce="..."
```

**Lượt 2 — MediaMTX POST lại lên MGMT, lần này kèm credential thật:**

```json
POST http://<mgmt-host>/<endpoint-xac-thuc-rtsp>
{
  "user": "admin",
  "password": "<mật khẩu thật hoặc digest response — xem lưu ý bên dưới>",
  "token": "",
  "ip": "192.168.8.50",
  "action": "read",
  "path": "live/ch100",
  "protocol": "rtsp",
  "id": "a1b2c3d4-...",
  "query": "",
  "userAgent": "ONVIF RTSP Client 25.12"
}
```

→ MGMT tìm user `admin`, xác thực đúng thì trả **200**, sai thì trả **401**. MediaMTX mới cho DESCRIBE đi tiếp và trả SDP thật.

**Lưu ý quan trọng cần verify khi tích hợp thật:** tài liệu MediaMTX không nói rõ field `password` ở lượt 2 chứa mật khẩu dạng gì (plaintext MediaMTX tự có sẵn từ đâu, hay digest response client gửi). Cần bật log/bắt gói thật lúc cấu hình để xác nhận chính xác field này chứa gì trước khi viết logic so khớp ở MGMT — đừng giả định cứng theo tài liệu.

## MGMT chỉ cần đọc 2 field: `user` và `password`

Payload MediaMTX gửi có 10 field (`user`, `password`, `token`, `ip`, `action`, `path`, `protocol`, `id`, `query`, `userAgent`), nhưng **MGMT chỉ cần đọc `user` + `password` để xác thực** — các field còn lại bỏ qua (hoặc dùng thêm để log/audit ai đang connect nếu muốn, không bắt buộc).

Lưu ý quan trọng: đây **không phải yêu cầu của chuẩn ONVIF** — ONVIF không hề biết và không quan tâm MediaMTX gọi callback bằng field gì, ONVIF chỉ quan tâm hành vi Digest ở tầng RTSP giữa client và MediaMTX (RFC 2617: `username`, `realm`, `nonce`, `uri`, `response`, `qop=auth`, `nc`, `cnonce`, `algorithm=MD5` — phần này MediaMTX tự xử lý nội bộ, không lộ ra ngoài payload gọi MGMT). 10 field kể trên hoàn toàn do MediaMTX tự thiết kế cho callback này, MGMT không có quyền thêm/bớt field gửi lên — chỉ có quyền chọn dùng field nào trong số MediaMTX gửi.

## MGMT đã có sẵn hạ tầng mã hóa/xác thực mật khẩu — không cần xây lại từ đầu

Đã kiểm tra code MGMT (`src/domains/user/user_service.cpp`), xác nhận **đã có sẵn đầy đủ** những gì cần để viết endpoint mới:

- **`PasswordCrypto`** (`encrypt()` / `decrypt()` / `verify()`): mật khẩu account ONVIF được lưu **mã hóa 2 chiều** (không phải hash 1 chiều như bcrypt) — bắt buộc phải vậy vì Digest auth cần biết lại mật khẩu gốc để tính toán, không thể chỉ so hash.
- **`UserService::verifyOnvifHttpDigest()`**: hàm đã hoàn chỉnh — tìm user theo `UserType::Onvif`, giải mã mật khẩu (`passwordCrypto.decrypt`), tính HA1/HA2/response theo đúng RFC 2617, so khớp bằng `CRYPTO_memcmp` (constant-time, chống timing attack), có chống replay nonce. Đây chính là hàm đứng sau endpoint `/internal/v1/auth/onvif/http-digest` hiện tại (dùng cho SOAP, không phải RTSP).
- **Repository lookup** (`repository.find(username, UserType::Onvif, user)`): đã tách riêng loại account `Onvif`, không lẫn với account MGMT web UI.

→ Endpoint mới cho MediaMTX **không cần viết crypto/verify từ đầu** — chỉ cần 1 controller mỏng: nhận đúng shape JSON của MediaMTX (`user`/`password`/`action`/...), gọi lại `UserService` hiện có (hoặc hàm tương đương, điều chỉnh input cho khớp field MediaMTX gửi) để xác thực, trả 401/200 theo đúng flow ở trên.

## Tóm tắt hành động

1. Bật Digest bắt buộc trên MediaMTX cho path live (`rtspAuthMethods: [digest]`).
2. Cấu hình `authMethod: http` trỏ tới 1 endpoint mới, viết dựa trên `UserService` sẵn có (không cần xây crypto/verify mới).
3. Bắt gói/log thật khi test để xác nhận chính xác field `password` MediaMTX gửi lên chứa gì (xem lưu ý ở trên) trước khi chốt logic so khớp.
4. Báo lại khi xong để chạy lại DTT xác nhận `MEDIA2_RTSS-1-1-1/1-1-2/1-1-3`.
