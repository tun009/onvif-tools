# MGMT treo đồng bộ toàn bộ endpoint khi lens motor zoom di chuyển quãng xa

> **OPEN (2026-09-28) — đã xác định nguyên nhân + có bằng chứng log, CHƯA
> có fix. Bug nằm ở tầng MGMT/HAL/firmware subboard CV25, KHÔNG phải
> onvif-module.** Phát hiện trong lúc verify Phase 5 (PTZ Zoom) bằng DTT
> trên camera `192.168.8.194`, xem
> `docs/onvif-alvis/01-IMPLEMENTATION_PLAN.md` mục "Triển khai Phase 5
> (2026-09-28)" phần "Bug ngoài phạm vi onvif-module".

## Bối cảnh

Đang tích hợp `onvif-module` với PTZ Zoom thật qua MGMT
(`LensApiController`, `/mgmt/v1/Config/ZoomFocus`). Chạy DTT test
`PTZ-3-1-1 ABSOLUTE MOVE` trên camera `192.168.8.194`: lệnh `AbsoluteMove`
(→ MGMT `PUT /mgmt/v1/Config/ZoomFocus`) **thực thi đúng, motor quay thật**
(xác nhận qua log `LensService HAL-DIRECT-OK set_zoom`), nhưng bước tiếp
theo — `GetStatus` (→ MGMT `GET /mgmt/v1/Config/ZoomFocus`) — bị treo tới
mức **timeout phía `onvif-module` (3000ms)**, trả lỗi `PTZ backend
unavailable` cho DTT.

**Đã tái hiện độc lập, không qua onvif-module** — gọi thẳng MGMT REST API
từ máy ngoài:

```powershell
# Set zoom từ 4.0 (MAX) về 1.0 (MIN) — full-range move
Invoke-RestMethod -Uri "http://192.168.8.194:8086/mgmt/v1/Config/ZoomFocus" -Method PUT `
  -ContentType "application/json" `
  -Body '{"VideoSourceId":"0","ZoomFocusCamera":{"FocusMode":1,"FocusValue":100,"ZoomValue":1.0}}'

# Gọi GET ngay sau đó
Invoke-RestMethod -Uri "http://192.168.8.194:8086/mgmt/v1/Config/ZoomFocus?VideoSourceId=0" -Method GET
```

**Kết quả đo thực tế: `GET` mất ~25 giây mới phản hồi.** Đây không phải
vấn đề riêng của DTT hay của `onvif-module` — bất kỳ client nào gọi thẳng
MGMT REST API cũng gặp y hệt.

## Vấn đề: `LensApiController` đọc trạng thái đồng bộ (blocking) qua UART, bị motor "chiếm" bus

### Bằng chứng log (`journalctl -u mgmt`, giờ local +07)

**Trường hợp bình thường** (poll định kỳ, không có lệnh set nào đang chạy)
— 1 chu kỳ đọc đủ 10 lệnh Pelco-D hoàn tất trong **~1 giây**, mỗi lệnh có
`Parsed SUCCESS` ngay lập tức:

```
11:09:22.xxx  [Cv25SubboardTransport] TX sendGetCommand cmd=0xcc → Pelco-D Parsed SUCCESS
11:09:22.xxx  [Cv25SubboardTransport] TX sendGetCommand cmd=0xef → Pelco-D Parsed SUCCESS
... (8 lệnh còn lại: 0xea, 0x4f, 0x5f, 0xeb, 0xd5, 0xd6, 0xd7, 0xab07 — tương tự)
11:09:23.314  [DirectHalBridge] readState() -> status=ok, states={...}   ← hoàn tất ~1s
```

**Trường hợp lỗi** — ngay sau khi `LensService` gửi lệnh `set_zoom`:

```
11:09:31.415  [LensService]      HAL-DIRECT-START  set_zoom position=4
11:09:31.427  [DirectHalBridge]  exec(set_zoom) -> true        ← lệnh SET tự nó rất nhanh (12ms)
11:09:31.427  [LensService]      HAL-DIRECT-OK    set_zoom
11:09:31.429  [LensService]      HAL-DIRECT-OK    set_focus
11:09:31.459  GET /mgmt/v1/Config/LensInfo?VideoSourceId=0     ← client gọi getLensBounds()
11:09:31.461  GET /mgmt/v1/Config/ZoomFocus?VideoSourceId=0    ← client gọi getZoomFocus()
11:09:31.xxx  [Cv25SubboardTransport] TX sendGetCommand cmd=0xcc
              ▓▓▓ KHÔNG có "Pelco-D Parsed SUCCESS" cho cmd=0xcc ▓▓▓
11:09:34.xxx  TX sendGetCommand cmd=0xef     ← 3.0s sau mới thử lệnh KẾ TIẾP (không phải retry 0xcc)
11:09:37.xxx  TX sendGetCommand cmd=0xea     ← lại đúng 3.0s sau
11:09:40.xxx  TX sendGetCommand cmd=0x4f     ← lại đúng 3.0s sau
11:09:43.xxx  TX sendGetCommand cmd=0x5f     ← log dừng theo dõi ở đây, quan sát trực tiếp qua PowerShell đo được tổng ~25s
```

### Phân tích cơ chế

1. **Lệnh ghi (`set_zoom`/`set_focus`) qua UART hoàn tất tức thì (~12-15ms)**
   — không phải vấn đề ở đường ghi.
2. **Ngay sau đó, đường ĐỌC (poll trạng thái qua Pelco-D) mất phản hồi
   hoàn toàn** — không phải chậm, mà **im lặng tuyệt đối** (không có gói
   trả lời/NACK nào). Mỗi lệnh poll cách nhau đúng **~3.0 giây** — trùng
   khớp `request_timeout_ms` phía client (`onvif-module`), cho thấy đây
   là **timeout của chính driver UART trong MGMT đang chờ phản hồi**, chứ
   không phải retry cố ý theo chu kỳ 3s.
3. `LensApiController::handleGetZoomFocus`/`handleGetLensInfo` gọi
   **đồng bộ (blocking)** vào đúng chuỗi poll UART này để build response
   → **toàn bộ HTTP worker thread xử lý request đó bị treo theo** cho tới
   khi UART có phản hồi trở lại (~25s với full-range move, đo thực tế).
4. Vài request khác (VD Digest auth) **vẫn lọt qua được** xen giữa cửa sổ
   treo — cho thấy MGMT (Drogon framework) có nhiều worker thread, chỉ
   1-2 thread bị kẹt cứng ở lời gọi UART, các thread khác vẫn phục vụ
   được request không đụng tới lens. → Request nào rơi đúng vào thread bị
   kẹt sẽ fail/treo, request khác thì không — giải thích hiện tượng "401
   xảy ra ngẫu nhiên" quan sát được khi chạy DTT dồn dập.

### Vì sao đây là vấn đề thật, không chỉ ảnh hưởng conformance test

- Bất kỳ client ONVIF/NVR/VMS thật nào ra lệnh zoom xa rồi hỏi trạng thái
  ngay sau đó (thao tác rất bình thường: "zoom rồi xác nhận vị trí") đều
  gặp y hệt — không phải artifact riêng của DTT.
- MGMT bị treo **toàn bộ endpoint chia sẻ worker thread bị kẹt**, không
  giới hạn ở API lens — bất kỳ thao tác nào khác chạm MGMT đúng lúc đó
  (đổi network config, xác thực Digest ONVIF, hay người dùng đang login
  MGMT UI) đều có thể bị ảnh hưởng lây.

## Nghi vấn cơ chế thật (cần team HAL xác nhận — chỉ có bằng chứng phía MGMT log, không thấy được firmware CV25)

| Giả thuyết | Mô tả |
|---|---|
| **(a) Firmware CV25 block khi đang chấp hành lệnh** | Vòng lặp điều khiển motor (VD: `while(!at_target) step_motor();`) chạy trên cùng thread/main-loop xử lý UART RX → không rảnh tay trả lời poll cho tới khi motor tới đích. |
| **(b) UART bus bị giữ/lock** | Lệnh ghi giữ bus, không nhả cho tới khi motion xong, dù về lý thuyết ghi và đọc có thể chạy độc lập. |
| **(c) Thiếu cơ chế "busy response"** | Giao thức Pelco-D hiện tại không có cách trả "đang bận" — chỉ có "im lặng" hoặc "trả kết quả xong", nên poll giữa chừng không nhận được gì thay vì 1 NAK/busy nhanh. |

## Đề xuất xử lý (theo mức độ ưu tiên/công sức)

### A. Nhanh nhất, rủi ro thấp nhất — sửa phía MGMT, không cần đụng firmware

`LensApiController::handleGetZoomFocus`/`handleGetLensInfo` **không nên tự
poll UART mỗi HTTP request**. MGMT đã có sẵn `DirectHalBridge::readState()`
chạy nền định kỳ (~1 lần/giây khi bình thường, thấy trong log) — đổi 2
handler này sang **đọc từ cache trạng thái gần nhất** của `readState()`
thay vì tự đi poll UART trực tiếp mỗi lần.

Hệ quả: GET trả về trong vài ms bất kể motor đang bận hay không; giá trị
có thể trễ 1 nhịp poll (~1s) nhưng **không còn treo hàng chục giây**. Fix
contained trong code MGMT, không đụng firmware — triển khai nhanh nhất,
khuyến nghị làm trước.

### B. Trung hạn — thêm timeout/circuit-breaker ở tầng transport UART

`DirectHalBridge`/`Cv25SubboardTransport` nên giới hạn mỗi lệnh
`sendGetCommand` ở timeout ngắn (VD 500ms-1s) thay vì chờ vô thời hạn.
Timeout → trả cache cũ + flag "có thể đang bận" thay vì treo cả chuỗi
HTTP. Giảm mạnh worst-case block dù chưa sửa gốc rễ firmware. Có thể làm
song song hoặc thay thế cho (A) nếu (A) không khả thi.

### C. Đúng gốc nhất, tốn công nhất — sửa firmware CV25

Làm điều khiển motor **bất đồng bộ** — nhận lệnh `set_zoom`, khởi động
motion (interrupt/timer-driven), **trả lời ngay** cho poll tiếp theo với
trạng thái "đang di chuyển" (`ZMActive=true`) thay vì im lặng chờ xong.
Đây là fix đúng bản chất nhất: cả MGMT lẫn mọi client phía trên đều được
lợi vì poll trong lúc motor chạy sẽ trả nhanh + đúng trạng thái busy thay
vì bị treo. Cần thiết nếu sau này muốn theo dõi tiến độ motion real-time
(progress bar zoom chẳng hạn) — (A)/(B) chỉ che triệu chứng ở tầng HTTP,
motor thật vẫn "vô hình" với hệ thống trong lúc di chuyển.

## Cách tái hiện (tóm tắt để team tự verify)

```powershell
# 1. Xem vị trí hiện tại
Invoke-RestMethod -Uri "http://<camera-ip>:8086/mgmt/v1/Config/ZoomFocus?VideoSourceId=0" -Method GET

# 2. Set về đầu đối diện (quãng đường càng xa càng dễ trigger)
Invoke-RestMethod -Uri "http://<camera-ip>:8086/mgmt/v1/Config/ZoomFocus" -Method PUT `
  -ContentType "application/json" `
  -Body '{"VideoSourceId":"0","ZoomFocusCamera":{"FocusMode":1,"FocusValue":100,"ZoomValue":<gia-tri-doi-dien>}}'

# 3. Đo thời gian GET ngay sau đó
Measure-Command {
  Invoke-RestMethod -Uri "http://<camera-ip>:8086/mgmt/v1/Config/ZoomFocus?VideoSourceId=0" -Method GET
}
```

Đối chứng: set zoom chỉ nhích nhẹ (VD lệch 0.2 so với hiện tại) rồi lặp
lại bước 3 — nếu lần này `GET` trả nhanh (~vài trăm ms), củng cố kết luận
bug tỉ lệ thuận với quãng đường motor phải di chuyển, không phải lỗi cố
định mỗi lần gọi `set_zoom`.

## Tóm tắt hành động

1. Xác nhận giả thuyết (a)/(b)/(c) ở tầng firmware CV25 — cần người có
   quyền truy cập source firmware, ngoài phạm vi những gì log MGMT cho
   thấy được.
2. Làm (A) trước — đổi `handleGetZoomFocus`/`handleGetLensInfo` sang đọc
   cache `readState()`, không tự poll UART mỗi request. Fix nhanh, rủi ro
   thấp.
3. Cân nhắc (B) làm cùng đợt nếu không tốn nhiều công thêm.
4. (C) làm sau, khi có kế hoạch cho firmware CV25 — không chặn Phase 5
   phía onvif-module (Zoom hiện đã hoạt động đúng khi motor không di
   chuyển quá xa).
5. Báo lại khi xong để verify lại bằng DTT (`PTZ-3-1-1`, `PTZ-3-1-4`,
   `PTZ-3-1-5` — các case từng dính cascade fail vì bug này).
