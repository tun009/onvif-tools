# RTSP live streaming: thiếu Digest auth + sai port RTSP-over-HTTP

## Bối cảnh

Đang tích hợp `onvif-module` (ONVIF SOAP server, đã pass conformance Profile T/M ở mock) với DVR thật qua REST API DVR (port 8200). Chạy ONVIF Device Test Tool (DTT) trên camera `.125` (ONVIF service port 8001), toàn bộ Media2 SOAP layer (GetProfiles, VideoSource/VideoEncoder Configuration, SnapshotUri) đã **PASS**. Chỉ còn 2 vấn đề, cả hai đều thuộc **RTSP server thật (MediaMTX)** phía DVR, không phải lỗi ONVIF SOAP:

| Test case | Lỗi | Nguyên nhân |
|---|---|---|
| MEDIA2_RTSS-1-1-1, MEDIA2_RTSS-1-1-3 | `Digest authentication is mandatory for Profile T and Profile M` | RTSP DESCRIBE trả `200 OK` thẳng, không có `401 + WWW-Authenticate: Digest` |
| MEDIA2_RTSS-1-1-2 | Stream URI không cùng port với web service | `GetStreamUri(RtspOverHttp)` trả port RTSP thật (1992) thay vì port ONVIF (8001) |

`onvif-module` chỉ đọc và trả lại nguyên văn URI mà DVR cung cấp qua `GetStreamUri` — không tự tạo/sửa hành vi RTSP được. Cả 2 việc dưới đây cần xử lý ở tầng DVR (MediaMTX config hoặc reverse-proxy), không sửa được từ phía ONVIF.

## Vấn đề 1: RTSP DESCRIBE không yêu cầu Digest auth

DVR dùng **MediaMTX** làm RTSP server thật cho live stream (xác nhận qua `lib/mediamtx_api/mediamtx_api.cpp`: DVR gọi HTTP API `localhost:9997` để đăng ký path, không tự viết RTSP server). MediaMTX mặc định cho phép `read` không cần auth trừ khi cấu hình tường minh trong `mediamtx.yml`.

**Cần bật Digest bắt buộc trên MediaMTX** — 2 cách, tùy khả năng MediaMTX bản đang dùng:

```yaml
# Cách 1: danh sách user tĩnh (đơn giản nhưng phải tự đồng bộ tay với MGMT)
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

**Chi tiết giao thức Cách 2 (theo tài liệu chính thức MediaMTX — [mediamtx.org/docs/features/authentication](https://mediamtx.org/docs/features/authentication)):** khi bật `authMethod: http`, mỗi lần có client kết nối, MediaMTX **POST** một JSON payload tới `authHTTPAddress` gồm: `user`, `password`, `token`, `ip`, `action` (`publish`/`read`/`playback`/...), `path`, `protocol` (`rtsp`/...), `id`, `query`, `userAgent`. Server xác thực trả **status 2xx** = cho phép, khác đi = từ chối.

**Lưu ý quan trọng riêng cho RTSP:** client RTSP không tự gửi username/password ngay từ đầu (đúng chuẩn RTSP Digest — phải bị hỏi trước). Nên ở **lượt gọi đầu tiên**, MediaMTX sẽ POST lên với `user`/`password` **rỗng**. Auth server (endpoint MGMT xây) phải **trả về 401** ở lượt này để MediaMTX biết cần thách thức Digest với client — MediaMTX sẽ tự sinh `401 + WWW-Authenticate: Digest` gửi lại client RTSP, client gửi lại DESCRIBE kèm Digest response, MediaMTX POST lại lần 2 lên cùng endpoint với `user`/`password` lần này đã có giá trị thật để xác thực thật sự.

→ Việc cần MGMT làm: viết **1 endpoint HTTP mới** (không dùng thẳng `/internal/v1/auth/onvif/http-digest` hiện có vì payload/protocol khác nhau — endpoint đó nhận tham số kiểu Digest challenge-response đầy đủ như HTTP Digest chuẩn (`nonce`, `nc`, `cnonce`, `response`...), còn MediaMTX chỉ gửi `user`/`password` thô ở dạng MediaMTX tự xử lý Digest nội bộ). Endpoint mới này chỉ cần: nếu `user`/`password` rỗng → trả 401; nếu có → so khớp với tài khoản ONVIF thật trong MGMT (cùng nguồn dữ liệu account mà `/internal/v1/auth/onvif/http-digest` đang dùng) → trả 200 nếu đúng, 401 nếu sai.

**Về tài khoản dùng để auth — quan trọng, đã kiểm tra lại code:** `onvif-module` **không hardcode 1 user cố định** cho ONVIF. `DigestAuthHandler::validate()` (`src/auth/DigestAuthHandler.cpp:243-271`) khi có kết nối MGMT sẽ gọi `HttpMgmtClient::verifyHttpDigest()` → `POST /internal/v1/auth/onvif/http-digest` lên **MGMT** để xác thực (`src/backend/HttpMgmtClient.cpp:528`). Tức **MGMT mới là nơi sở hữu tài khoản ONVIF thật** (có thể nhiều account, quản lý qua MGMT UI/API) — `username=admin`/`password=admin123` trong `onvif.conf` chỉ là giá trị fallback dùng khi chưa nối MGMT (dev/mock), không phải tài khoản thật trên thiết bị production.

Theo đúng thiết kế ONVIF Profile T, VMS chỉ nhập 1 bộ username/password cho cả thiết bị và dùng chung cho cả SOAP lẫn RTSP — không có chỗ nhập thêm mật khẩu RTSP riêng. Vì vậy **không nên hardcode 1 user tĩnh trong `mediamtx.yml`** (sẽ lệch/lỗi thời khi MGMT có account mới hoặc đổi mật khẩu) — nên dùng **Cách 2**: trỏ MediaMTX gọi thẳng vào cơ chế xác thực Digest MGMT đang dùng cho ONVIF (cùng nguồn `verifyHttpDigest` phía trên, hoặc 1 endpoint tương đương MGMT expose riêng cho RTSP), để bất kỳ account nào MGMT quản lý cũng tự động authenticate được RTSP mà không cần đồng bộ tay.

## Vấn đề 2: RtspOverHttp trả sai port

### DVR hiện đang trả `GetStreamUri` như thế nào — nói rõ để DVR team hiểu đúng luồng

**DVR không hề có logic riêng cho `GetStreamUri` theo từng protocol.** `onvif-module` phía client gọi hàm `HttpDvrClient::getStreamUri()` (`src/backend/HttpDvrClient.cpp:269-292`), và hàm này **không gọi endpoint `GetStreamUri` nào của DVR cả** — nó gọi lại `GET /dvr/v3.0/GetProfiles` (endpoint đã dùng để lấy danh sách profile), rồi lấy thẳng field `StreamUri` có sẵn trong response (ví dụ `"StreamUri":"rtsp://192.168.8.125:1992/live/ch100"` — đúng như thấy trong DevTools). Tham số `protocol` (RTSP/RtspUnicast/RtspOverHttp/...) mà client ONVIF gửi lên **hoàn toàn không được gửi xuống DVR** — DVR luôn trả về đúng 1 dạng URI RTSP thô, bất kể ONVIF client hỏi protocol gì.

→ **Kết luận: DVR không cần đổi gì cho vấn đề này.** Toàn bộ việc biến đổi URI theo protocol (đổi scheme, đổi port cho tunnel...) diễn ra bên trong `onvif-module`, ở hàm `Media2Service::GetStreamUri()` (`src/services/Media2Service.cpp:364-448`), sau khi đã nhận URI thô từ DVR.

### Bug thật nằm ở đâu (đã trace ra code, không cần đoán)

Trong `Media2Service::GetStreamUri`, khi protocol là `RtspOverHttp`/`RtspOverHttps` (dòng 432-443):

```cpp
if (protocol == "RtspOverHttp" || protocol == "RtspOverHttps") {
    std::string rtspPortStr = ":" + std::to_string(cfg_.rtspPort);   // ":" + giá trị onvif.conf
    size_t portPos = uri.find(rtspPortStr);
    if (portPos != std::string::npos) {
        uri.replace(portPos, rtspPortStr.length(),
                    std::string(":") + RTSP_HTTP_TUNNEL_PORT);       // hardcode "8080"
    }
    ...
}
```

`cfg_.rtspPort` đọc từ `onvif.conf` dòng `rtsp_port = 8554` — đây là **giá trị port mock cũ (MediaMTX mock chạy 8554)**, không phải port thật của DVR (**1992**). Vì URI thật DVR trả về chứa `:1992` chứ không phải `:8554`, câu lệnh `uri.find(":8554")` luôn thất bại (`npos`) → khối `if` không bao giờ chạy → port **không hề bị thay** → URI cuối cùng chỉ đổi scheme `rtsp://` → `http://`, giữ nguyên port `1992` → ra đúng `http://192.168.8.125:1992/live/ch100` như log DTT đã thấy.

`RTSP_HTTP_TUNNEL_PORT` (`Media2Service.cpp:23`) cũng đang hardcode `"8080"` — port ONVIF của bản **mock**, không phải `8001` đang chạy thật trên `.125`.

### Cách sửa — toàn bộ nằm ở phía `onvif-module`, không cần DVR đổi gì

1. Sửa `onvif.conf`: `rtsp_port = 1992` (khớp port RTSP thật của MediaMTX) thay vì `8554`.
2. `Media2Service.cpp:437`: thay hardcode `RTSP_HTTP_TUNNEL_PORT` bằng port ONVIF thật đang chạy (`cfg_.httpPort`, hiện là `8001`; sau này đọc động từ MGMT thì tự động đúng theo).
3. `OnvifServer.cpp` hàm `proxyRtspHttpTunnel` (dòng 88-141): địa chỉ đích đang hardcode `127.0.0.1:8555` (relay mock) → đổi thành `127.0.0.1:` + `cfg_.rtspPort` (sau khi sửa mục 1, tự động là `1992`).
4. Rà lại khối `if (protocol == "RTSP" || protocol == "RtspUnicast" || protocol == "RtspMulticast")` (dòng 421-429) — khối này định route qua 1 relay xác thực (`RTSP_RELAY_PORT = "8555"`, cũng của mock) cho các protocol RTSP thường. Hiện tại nó cũng đang là no-op vì cùng lý do `:8554` không khớp — **may mắn đang "đúng" theo kiểu tình cờ**. Sau khi sửa mục 1, nếu không dọn/tắt khối này, nó sẽ bắt đầu route sang `127.0.0.1:8555` (không có gì chạy ở đó cho DVR thật) → cần xóa hoặc vô hiệu khối này cho trường hợp real DVR (vì một khi MediaMTX tự bật Digest ở Vấn đề 1, không cần relay trung gian nữa — trả thẳng URI DVR cung cấp là đủ).

**Phần DVR/MGMT chỉ cần xác nhận 1 việc:** port RTSP thật của MediaMTX (`1992`) là ổn định lâu dài và luôn reachable qua `127.0.0.1` từ tiến trình `onvif-module` (chạy chung máy) — để đội ONVIF yên tâm hardcode/derive đúng giá trị này ở bước 1-3 trên.

### Lưu ý quan trọng — tránh lặp lại cách làm cũ chưa thành công

Trong source DVR/onvif_server cũ (đã pass Profile S trước đây), đã có người thử xử lý việc này nhưng **chưa giải quyết trọn vẹn**:

- Có đoạn code định dựng proxy thật bằng `live555ProxyServer -p 8300 -t8123 -R rtsp://127.0.0.1:554/...` nhưng bị **comment out**, không dùng trong production.
- Thay vào đó có endpoint `/fakeUri` — chỉ nhận diện đúng header handshake tunnel rồi trả `200 OK`, sau đó **treo kết nối 1 tiếng bằng `sleep_for(std::chrono::hours(1))`, không hề forward dữ liệu RTSP thật nào qua đó**. Đây là một cách "lừa" bước handshake ban đầu của test tool, không phải tunnel hoạt động thật — đúng như comment trong code cũ ghi: *"Trả về phản hồi rỗng đúng định dạng để 'lừa' ONVIF Tool"*.
- Port cũng lệch tương tự (ONVIF server chạy port 8000, `proxyStream` trả về port 80 mặc định).

→ Bài học: không dùng lại kiểu stub `/fakeUri`. Cơ chế raw-proxy thật (tương tự hướng `live555ProxyServer` từng định làm) **đã có sẵn và chạy ổn trong `onvif-module`** cho bản mock (`OnvifServer.cpp:88-141`) — phía ONVIF sẽ tái dùng/mở rộng code này cho DVR thật, không cần DVR/MGMT viết lại.

## Tóm tắt hành động cần từng bên làm

**Đội DVR/MGMT:**
1. Bật Digest bắt buộc trên MediaMTX cho path live (`rtspAuthMethods: [digest]`).
2. Xây 1 endpoint HTTP xác thực (theo đúng giao thức `authHTTPAddress` của MediaMTX, có nói rõ ở Vấn đề 1) dùng chung nguồn tài khoản ONVIF đang có trong MGMT — không hardcode user tĩnh trong `mediamtx.yml`.
3. Xác nhận port RTSP thật của MediaMTX (`1992`) ổn định lâu dài, reachable qua `127.0.0.1` từ tiến trình `onvif-module` (chạy chung máy).

**Đội ONVIF (`onvif-module`) — không cần chờ DVR, có thể làm song song:**
1. Sửa `onvif.conf`: `rtsp_port = 1992` thay vì `8554` (giá trị mock cũ).
2. `Media2Service.cpp:437`: bỏ hardcode `RTSP_HTTP_TUNNEL_PORT="8080"`, dùng `cfg_.httpPort` (port ONVIF thật đang chạy, `8001`).
3. `OnvifServer.cpp` (`proxyRtspHttpTunnel`): đổi target hardcode `127.0.0.1:8555` → `127.0.0.1:` + `cfg_.rtspPort`.
4. Rà soát/vô hiệu khối route qua `RTSP_RELAY_PORT` (`Media2Service.cpp:421-429`) cho trường hợp real DVR — không cần relay trung gian nữa một khi MediaMTX tự bật Digest.

**Chung:** báo lại nhau khi cả 2 phần xong để chạy lại DTT xác nhận `MEDIA2_RTSS-1-1-1/1-1-2/1-1-3`.
