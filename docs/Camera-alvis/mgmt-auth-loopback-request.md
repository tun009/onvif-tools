# Đề nghị MGMT-BE: cho dịch vụ nội bộ (onvif-module) gọi `/mgmt/v1/*` từ loopback trực tiếp mà không cần session

> Tài liệu cho team MGMT-BE (và AI agent hỗ trợ). Viết ngày 2026-10-13. Nội dung đã đối chiếu với source
> nhánh `mgmt-01-config-api-v4/auth` (commit `3aa056a`) và với camera `192.168.8.194` đang chạy bản MGMT build lúc
> 2026-10-13 14:15. Chỗ nào chưa kiểm chứng được ghi rõ `[CHƯA KIỂM CHỨNG]`.

## 1. Tóm tắt một dòng

Sau khi bật cơ chế auth mới (session + `Require*` filter), **onvif-module** (chạy cùng camera, gọi MGMT qua
`127.0.0.1:8086`) bị `401 Login required` ở mọi route `/mgmt/v1/Config/*`. Đề nghị thêm **một điều kiện
`isDirectLoopback` vào `RoleFilterBase::check`** để yêu cầu đến trực tiếp từ loopback được bỏ qua kiểm tra session.

## 2. Bối cảnh

- **onvif-module** là server ONVIF (SOAP) chạy trên camera, đóng vai trò client của MGMT: đọc/ghi thông tin
  thiết bị, thời gian, mạng, imaging, zoom/focus, user ONVIF… qua REST `http://127.0.0.1:8086/mgmt/v1/...`.
- Nó là tiến trình hệ thống, **không có tài khoản web, không có session/cookie `MGMT_SID`**.
- Nó đã gọi được `/internal/v1/auth/onvif/*` (route dùng `LoopbackOnly`) để xác thực user ONVIF: phần này **vẫn chạy
  bình thường** sau bản MGMT mới.

## 3. Hiện tượng (đo trên camera `.194`, gọi từ chính camera qua `127.0.0.1`)

| Yêu cầu | Kết quả |
|---|---|
| `GET /mgmt/v1/Ping` | `200 {"status":"ok"}` |
| `GET /mgmt/v1/Config/DeviceInformation` | `401 {"message":"Login required","result":0}` |
| `GET /mgmt/v1/Config/GetSystemDateAndTime` | `401 Login required` |
| `GET /mgmt/v1/Config/GetOnvifUsers` | `401 Login required` |
| `GET /mgmt/v1/Config/ZoomFocus` | `401 Login required` |
| `GET /mgmt/v1/GetNetworkProtocols` | `401 Login required` |
| `GET /mgmt/v1/GetHostname` | `401 Login required` |
| `POST /internal/v1/auth/onvif/wsse-password-digest` (dữ liệu giả) | `400 {"Authenticated":false}` (route vẫn đến được) |

Hệ quả cho onvif-module: không đọc được thông tin thiết bị, thời gian, mạng; không điều khiển được imaging,
zoom/focus; không đọc/ghi được user ONVIF. Khi khởi động, nó còn không đọc được cổng từ MGMT nên rơi về cổng
trong file cấu hình (8001 thay vì 8000).

## 4. Cơ chế auth hiện tại (đọc từ source)

File: `src/backend/include/domains/auth/filters/auth_filters.hpp`, `src/backend/src/domains/auth/filters/auth_filters.cpp`.

1. Mỗi route gắn một filter qua `ADD_METHOD_TO(...)`. Đếm trong `include/`: 118 route = 50 `RequireAdmin`, 43
   `RequireOperator`, 1 `RequireViewer`, 4 `PublicRoute`, 2 `LoopbackOnly`, 2 `UserInputValidationFilter`.
2. Ba filter `RequireViewer`/`RequireOperator`/`RequireAdmin` đều gọi **cùng một hàm** `RoleFilterBase::check(...)`.
   Hàm này: bỏ qua `OPTIONS`; lấy `session` từ cookie `MGMT_SID`; nếu không có session hợp lệ thì
   `reject(rejection(401, "Login required"))`; thiếu quyền thì `403 "Permission denied"`; ngược lại gắn
   `AuthContext` và `markAuthChecked(request)`.
3. `AuthGuard` (đăng ký ở `src/main.cpp:504`, `addPreHandlingAdvice`) chặn route **không có filter** nào. Prefix
   được bảo vệ: `/mgmt/`, `/nse/`, `/ws/`, `/internal/`; miễn: `/mgmt/v1/Maintenance/`.
4. `isDirectLoopback(request)` hiện là:
   ```cpp
   return request->peerAddr().isLoopbackIp()
       && request->getHeader("X-Forwarded-For").empty()
       && request->getHeader("X-Real-IP").empty();
   ```
5. `LoopbackOnly::doFilter` dùng `isDirectLoopback`, trả `403 "Local services only"` nếu không thỏa; thỏa thì
   `markAuthChecked` + `pass()`.

## 5. Thay đổi đề nghị

Trong `RoleFilterBase::check` (`auth_filters.cpp`), **ngay sau khối xử lý `OPTIONS`** và trước khi đọc session, thêm:

```cpp
// Dịch vụ nội bộ trên camera (onvif-module) gọi trực tiếp qua loopback, không qua Apache: không cần session.
if (isDirectLoopback(request))
{
    markAuthChecked(request);
    pass();
    return;
}
```

Một thay đổi này phủ toàn bộ 94 route `Require*`, và mọi route sau này dùng `Require*` mà không phải khai báo từng route.
Yêu cầu từ trình duyệt (qua Apache) và từ máy khác vẫn đòi đăng nhập như hiện nay.

### Ghi chú về `AuthContext`

Khi bỏ qua session thì request **không có `AuthContext`**. Các chỗ đọc nó đã xử lý `nullptr`:
`user_api_controller.cpp` (`actorOf` trả `"-"`, `isSelf` trả `false`), nên không gây lỗi; chỉ là log ghi người thực
hiện là `-`. Nếu muốn log rõ hơn, gắn một `AuthContext` cố định (ví dụ username `internal-service`, role
`Administrator`) khi vào nhánh loopback. Các handler yêu cầu `AuthContext` ở `auth_api_controller.cpp:136` là route
đăng nhập/phiên của web, không liên quan onvif-module.

## 6. Điều kiện an toàn cần xác nhận (QUAN TRỌNG)

Camera chạy Apache làm reverse proxy: `/etc/apache2/sites-available/000-default.conf` có
`ProxyPass /mgmt/v1 http://localhost:8086/mgmt/v1` (dòng 178) và `RequestHeader set X-Forwarded-Proto "http"`
(dòng 177). Nghĩa là **trình duyệt từ mạng LAN đi qua Apache rồi đến MGMT từ `127.0.0.1`**. Nếu bỏ qua session chỉ dựa
vào "địa chỉ nguồn là loopback" thì mọi người dùng web sẽ được bỏ qua đăng nhập, rất nguy hiểm.

Cách `isDirectLoopback` hiện tại phân biệt là kiểm tra **không có `X-Forwarded-For` và `X-Real-IP`**. Cần xác nhận:

1. `mod_proxy_http` của Apache luôn thêm `X-Forwarded-For` cho yêu cầu `/mgmt/v1` (mặc định `ProxyAddHeaders On`).
   Cần kiểm tra cấu hình không tắt `ProxyAddHeaders` và không xóa header này (`RequestHeader unset`).
   `[CHƯA KIỂM CHỨNG trên thiết bị: mới thấy RequestHeader set X-Real-IP/X-Forwarded-For ở dòng 256-271 trong một
   số khối, chưa xác nhận khối nào áp dụng cho /mgmt/v1]`
2. Các đường khác vào MGMT (WebSocket `/ws/` qua `ProxyPass "/ws/" "ws://localhost:8086/ws/"`) cũng phải mang header
   này. Nếu có route `Require*` nào được gọi qua WebSocket proxy mà không có header, nó sẽ bị coi là loopback trực tiếp.
3. Tùy chọn tăng độ chắc chắn: coi `X-Forwarded-Proto` (Apache luôn `set` cho `/mgmt/v1`) cũng là dấu hiệu "đi qua
   proxy", tức `isDirectLoopback` trả `false` khi header này khác rỗng. Việc này khớp với chỗ `isSecureRequest` đang dùng.
4. Cổng 8086 đang listen trên `0.0.0.0` (`ss -ltnp` trên `.194`). Máy khác trong LAN gọi thẳng `:8086` có địa chỉ nguồn
   không phải loopback nên vẫn bị 401 (an toàn). Nếu ý định là chỉ bind localhost thì nên đổi (ngoài phạm vi yêu cầu này).

## 7. Kiểm thử đề nghị

Chạy trên camera sau khi deploy bản sửa:

```bash
# (a) Trực tiếp từ loopback, không session: phải 200 (trước khi sửa là 401)
curl -s -o /dev/null -w "%{http_code}\n" http://127.0.0.1:8086/mgmt/v1/Config/DeviceInformation
curl -s -o /dev/null -w "%{http_code}\n" http://127.0.0.1:8086/mgmt/v1/GetNetworkProtocols
curl -s -o /dev/null -w "%{http_code}\n" -X POST -H 'Content-Type: application/json' \
     -d '{"Username":["x"],"UserType":"onvif"}' http://127.0.0.1:8086/mgmt/v1/Config/DeleteUsers

# (b) Qua Apache từ máy khác, không đăng nhập: phải vẫn 401 (hỏng nếu thành 200)
curl -s -o /dev/null -w "%{http_code}\n" http://<ip-camera>/mgmt/v1/Config/DeviceInformation

# (c) Giả mạo header từ loopback: phải vẫn 401 (có X-Forwarded-For nghĩa là qua proxy)
curl -s -o /dev/null -w "%{http_code}\n" -H 'X-Forwarded-For: 1.2.3.4' http://127.0.0.1:8086/mgmt/v1/Config/DeviceInformation

# (d) Gọi thẳng cổng 8086 từ máy khác trong LAN: phải 401
curl -s -o /dev/null -w "%{http_code}\n" http://<ip-camera>:8086/mgmt/v1/Config/DeviceInformation

# (e) Đăng nhập web bình thường vẫn hoạt động và route theo vai trò (RequireAdmin) vẫn kiểm tra quyền khi đi qua Apache.
```

Tiêu chí đạt: (a) `200`; (b), (c), (d) `401`; (e) không thay đổi so với trước.

## 8. Phương án thay thế (nếu không muốn thêm ngoại lệ chung)

- Cấp cho onvif-module một token/tài khoản dịch vụ (ví dụ `Authorization: Bearer …` hoặc session cố định) và cho biết
  cách lấy/gia hạn. Onvif-module sẽ thêm vào `HttpMgmtClient`. Phức tạp hơn: phải lưu bí mật, xử lý hết hạn.
- Hoặc chuyển/lặp lại các route onvif-module dùng sang tiền tố `/internal/v1/...` với `LoopbackOnly` (phải làm lần lượt
  từng route, tốn công mỗi khi onvif-module thêm tính năng).

Đề nghị chọn phương án ở mục 5 vì chỉ sửa một chỗ và các route về sau không phải xin mở riêng.

## 9. Tham chiếu

- `src/backend/include/domains/auth/filters/auth_filters.hpp`, `src/backend/src/domains/auth/filters/auth_filters.cpp`
- `src/backend/src/main.cpp` dòng 504: `httpServer.addPreHandlingAdvice(AuthGuard({"/mgmt/", "/nse/", "/ws/", "/internal/"}, {"/mgmt/v1/Maintenance/"}))`
- `src/backend/include/domains/user/user_api_controller.hpp` (ví dụ khai báo route với filter)
- Apache: `/etc/apache2/sites-available/000-default.conf` dòng 178 (ProxyPass `/mgmt/v1`), 177 (`X-Forwarded-Proto`), 243 (`/ws/`), 256-271 (`X-Real-IP`, `X-Forwarded-For`).
