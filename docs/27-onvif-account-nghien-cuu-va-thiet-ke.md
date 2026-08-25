# ONVIF account dùng để làm gì? Đề xuất cho Camera-alvis

> Nguồn chuẩn chính: ONVIF Core Specification 26.06 (tháng 6/2026), tài liệu Axis OS và Hikvision. Đối chiếu code `onvif-module` và RTSP relay hiện tại.

## 1. Kết luận ngắn

**ONVIF account là tài khoản dành cho ứng dụng máy-máy** — chủ yếu VMS/NVR/ONVIF client đăng nhập vào camera qua ONVIF SOAP, RTSP và các tài nguyên liên quan. Nó không nhất thiết là tài khoản để con người đăng nhập trang Web.

Ví dụ:

```text
Tài khoản Web:
admin_tung → dùng trình duyệt quản trị camera

Tài khoản ONVIF:
milestone_cam01 → Milestone VMS tự động gọi ONVIF + lấy RTSP
```

Việc các hãng đặt mục **ONVIF Account** riêng trên Web có ba mục đích: bật/tắt bề mặt tích hợp ONVIF, cấp danh tính riêng cho từng VMS, và giới hạn VMS đó được xem hay được cấu hình những gì.

ONVIF account **không phải tài khoản để pass Test Tool**. Test Tool chỉ đóng vai một ONVIF client và kiểm tra camera có xác thực/phân quyền đúng chuẩn không. Trong sản phẩm thật, chính VMS/NVR sử dụng tài khoản này hàng ngày.

## 2. ONVIF account bảo vệ những gì?

Một bộ credentials ONVIF thường được dùng xuyên ba lớp:

```text
1. SOAP/HTTP  : Device, Media, PTZ, Event, Recording, Search, Replay...
2. RTSP       : live stream và replay stream
3. HTTP media : snapshot hoặc tài nguyên HTTP liên quan
```

ONVIF Core 26.06 nêu rõ credentials được quản lý bằng `GetUsers`, `CreateUsers`, `DeleteUsers`, `SetUser`; khi xác thực RTSP/HTTP, thiết bị dùng cùng bộ credentials. Nếu hỗ trợ WS-Security, cũng dùng cùng bộ credentials đó.

Luồng VMS thực tế:

```text
1. Admin vào Web camera, tạo ONVIF account:
   username = milestone_cam01
   password = ...
   role     = Operator

2. Admin nhập credentials này một lần vào Milestone.

3. Milestone dùng tài khoản đó để:
   - gọi GetServices/GetProfiles/GetStreamUri
   - mở RTSP URI được trả về
   - nhận Event/PullPoint
   - điều khiển PTZ nếu có quyền
   - tìm recording/replay nếu có quyền
```

Tức tài khoản là **chìa khóa VMS dùng để bước qua cổng chuẩn ONVIF**, không tham gia nghiệp vụ AI/recording bên trong.

## 3. Vì sao tách ONVIF account khỏi Web account?

Chuẩn ONVIF định nghĩa user và quyền, nhưng **không bắt buộc giao diện Web phải dùng chung hay tách kho tài khoản**. Đây là quyết định kiến trúc của hãng.

Hikvision ghi rõ hệ thống ONVIF user độc lập với hệ thống quản lý tài khoản thiết bị. Axis OS cũng tách VAPIX/Web users và ONVIF users thành giao diện quản lý/quyền riêng. Tách riêng mang lại:

```text
Tách mục đích:
- Web account đại diện con người
- ONVIF account đại diện ứng dụng/VMS

Giảm quyền:
- VMS chỉ cần xem stream không cần quyền đổi firmware/mạng

Thu hồi độc lập:
- bỏ VMS A → xóa account VMS A, không đổi mật khẩu Web admin

Audit rõ:
- log biết Milestone hay Genetec đã gọi camera

Giảm lộ secret:
- không phải nhập mật khẩu admin Web vào mọi VMS/NVR

Bật/tắt integration:
- chưa tạo ONVIF account thì ONVIF có thể giữ trạng thái vô hiệu hóa
```

Axis gọi loại này là **service account** (tài khoản cho ứng dụng, không phải người). Axis khuyến nghị mỗi ứng dụng có account riêng, không chia sẻ credentials; xóa account khi ứng dụng không còn dùng.

Ví dụ tốt:

```text
milestone_site_A   → VMS A, Operator
nvr_backup_01      → NVR backup, User/Media
onvif_maintenance  → công cụ bảo trì, Administrator, chỉ tạo tạm thời
```

Ví dụ không tốt:

```text
admin/admin123 dùng chung cho:
- Web UI
- Milestone
- NVR
- Test Tool
- RTSP player
```

Một credentials dùng mọi nơi khiến không biết ai đã thao tác, khó thu hồi, và lộ một lần là mất toàn bộ camera.

## 4. Ba mức quyền ONVIF

ONVIF định nghĩa ba user level có tài khoản và một mức không có tài khoản:

```text
Administrator : toàn quyền, gồm cấu hình hệ thống và quản lý user
Operator      : xem + điều khiển vận hành như PTZ/recording job
User          : chủ yếu đọc thông tin, xem media/recording
Anonymous     : chưa đăng nhập; không được tạo user Anonymous
```

ONVIF gom từng API vào **access class** theo mức tác động:

| Access class | Ý nghĩa dễ hiểu | Ví dụ | Quyền mặc định |
|---|---|---|---|
| `PRE_AUTH` | được gọi trước đăng nhập | endpoint/capability công khai tối thiểu | Tất cả, kể cả Anonymous |
| `READ_SYSTEM` | đọc cấu hình thường | network interface, NTP | User trở lên |
| `READ_SYSTEM_SENSITIVE` | đọc thông tin nhạy hơn | tùy API | Operator trở lên |
| `READ_SYSTEM_SECRET` | đọc bí mật hệ thống | system log | Administrator |
| `WRITE_SYSTEM` | đổi cấu hình hệ thống | network gateway, user | Administrator |
| `UNRECOVERABLE` | thay đổi khó/không hoàn tác | factory reset | Administrator |
| `READ_MEDIA` | xem live/recording/event | GetRecordings, stream | User trở lên |
| `ACTUATE` | làm hệ thống hành động | PTZ, recording job | Operator trở lên |

Cách nhớ:

```text
User          = nhìn
Operator      = nhìn + điều khiển
Administrator = nhìn + điều khiển + quản trị hệ thống
```

Hãng có thể đặt tên `Media User`/`Viewer` thay cho `User` trên Web. Hikvision mô tả Media User đọc cấu hình và xem stream; Operator gần như mọi vận hành nhưng không được đổi mạng, factory reset, firmware, hoặc đọc bí mật; Administrator không giới hạn.

## 5. Xác thực diễn ra thế nào?

### 5.1 SOAP ONVIF

VMS gọi API ONVIF qua HTTP/HTTPS. Camera có thể xác thực bằng:

```text
HTTP Digest Authentication
hoặc
WS-Security UsernameToken (PasswordText/PasswordDigest)
```

Trong production nên dùng HTTPS/TLS. Digest và UsernameToken tự thân không thay thế kênh mã hóa mạnh.

Sau **authentication** (đúng username/password), camera phải làm **authorization** (role có được gọi API này không):

```text
milestone_cam01 / Operator
    ↓ xác thực OK
CreateUsers
    ↓ access class WRITE_SYSTEM
Từ chối: Operator không đủ quyền
```

### 5.2 RTSP live/replay

VMS lấy URI qua `GetStreamUri` hoặc `GetReplayUri`, rồi mở kết nối RTSP riêng. RTSP server lại challenge Digest và kiểm **cùng account**:

```text
SOAP GetStreamUri → rtsp://camera/live/main
RTSP DESCRIBE     → 401 + WWW-Authenticate: Digest
VMS gửi Authorization bằng milestone_cam01
RTSP server kiểm account + quyền READ_MEDIA
```

ONVIF SOAP module không nằm trên đường RTP, nhưng **Identity Store dùng chung** phải phục vụ cả SOAP và RTSP.

## 6. Trạng thái code mock hiện tại

Code hiện tại đã có hình thức user management nhưng chưa phải ONVIF account production hoàn chỉnh.

### Đã có

```text
- HTTP Digest cho SOAP
- WS-Security PasswordText/PasswordDigest
- GetUsers/CreateUsers/DeleteUsers/SetUser
- 3 level Admin/Operator/User trong MockUser
- không trả password trong GetUsers
- cấm xóa admin cuối cùng
- RTSP Digest challenge
```

### Vấn đề quan trọng

**1. User được tạo nhưng không dùng để đăng nhập.**

`CreateUsers` ghi vào `sys_.users`, nhưng `WsSecurityHandler` và `DigestAuthHandler` chỉ kiểm đúng **một** cặp `cfg_.username/cfg_.password` (`admin/admin123`). Vì vậy user do ONVIF `CreateUsers` tạo có thể xuất hiện trong `GetUsers`, nhưng không xác thực được request thật.

```text
DeviceService::CreateUsers → sys_.users.push_back(mu)

Nhưng authentication:
WsSecurityHandler(cfg_.username, cfg_.password)
DigestAuthHandler(cfg_.username, cfg_.password)
```

**2. Chưa có RBAC thật theo từng operation.**

Middleware chủ yếu quyết định request có cần đăng nhập không; sau khi đúng credentials, không có bước map operation → access class → role. Một Operator/User hợp lệ (nếu login được) chưa bị chặn rõ ràng khỏi `CreateUsers`, `SetNetworkDefaultGateway`, factory reset...

**3. Credentials RTSP tách và hard-code.**

Relay dùng `rtspUser="admin"`, `rtspPassword="admin123"`; chưa đọc chung danh sách `sys_.users`. Điều này trái mục tiêu "một ONVIF account dùng cho SOAP + RTSP".

**4. User chỉ ở RAM.**

Restart service sẽ mất user tạo thêm. Password lưu plaintext trong `MockUser`; không phù hợp production.

**5. Default account cố định.**

`admin/admin123` dùng mặc định. Camera production không nên có mật khẩu mặc định dùng chung; SRS Camera-alvis cũng yêu cầu buộc tạo mật khẩu mạnh lần đầu.

**6. Chính sách bypass đang làm theo tên XML thô.**

`isAuthRequired()` tìm chuỗi operation trong raw headers/body. Production nên route operation trước, lấy access class từ bảng policy, rồi xác thực/phân quyền có cấu trúc.

## 7. Thiết kế nên dùng cho Camera-alvis

Nên đặt quản lý account ở **MGMT + Security/IAM**, không đặt trong DVR/Core/VPU.

```text
Web UI (MGMT-01)
   │ tạo/sửa/xóa ONVIF account
   ▼
ONVIF Account API
   ▼
IdentityStore / CredentialStore (SEC)
   ├── user id, username, role, enabled
   ├── password verifier/hash hoặc digest material được bảo vệ
   ├── createdBy/createdAt/lastUsed
   └── audit log
          ▲                     ▲
          │                     │
SOAP Auth Middleware      RTSP Auth Provider
(MGMT-02)                 (DVR-04)
```

### Thành phần đề xuất

```text
IIdentityStore
- findByUsername()
- createUser()
- updateUser()
- deleteUser()
- listUsers()
- verifyPassword()/digest material

IAuthorizationPolicy
- requiredAccessClass(service, operation)
- isAllowed(role, accessClass)

AuditService
- LOGIN_SUCCESS / LOGIN_FAILED
- USER_CREATED / USER_UPDATED / USER_DELETED
- ACCESS_DENIED
```

Cả hai đường quản lý phải gọi chung service:

```text
Web UI "Add ONVIF account"
       └──► IdentityService.createUser()

ONVIF DeviceService.CreateUsers
       └──► IdentityService.createUser()
```

Không được có một danh sách user trong Web và một vector riêng trong `DeviceService.cpp` nếu UI nói chúng là cùng ONVIF account.

### Bảng role nên triển khai ban đầu

```text
Media User / User:
- xem live, snapshot, metadata/event
- tìm và replay recording
- đọc media/system config không bí mật
- không PTZ, không đổi config

Operator:
- toàn bộ Media User
- PTZ/zoom/focus
- thay media config nếu sản phẩm cho phép
- điều khiển recording job
- không user/network/firmware/factory reset

Administrator:
- mọi quyền ONVIF
- quản lý ONVIF account
- network/system/security
```

### Giao diện Web tối thiểu

```text
ONVIF Integration
[Enable ONVIF]

Accounts
Username              Role            Status     Last used
milestone_cam01       Operator        Enabled    2026-08-05 14:20
nvr_backup            Media User      Enabled    2026-08-05 14:18

[Add account] [Change password] [Change role] [Disable] [Delete]
```

Không hiển thị lại password. Khi tạo, password chỉ nhập và xác nhận; sau đó muốn đổi phải đặt password mới.

## 8. Lựa chọn dùng chung hay tách Web account

### Phương án A — tách ONVIF account khỏi Web account (khuyến nghị)

```text
Web users   → con người đăng nhập Web/REST
ONVIF users → VMS/NVR/ứng dụng máy-máy
```

Ưu điểm: least privilege, audit rõ, thu hồi độc lập, tương tự Axis/Hikvision. Nhược: thêm một kho logic account cần quản lý.

### Phương án B — dùng chung một Identity Store, nhưng tách loại account

Đây thường là cách code tốt nhất:

```text
Một DB/IdentityService
  ├── type=HUMAN, allowedInterfaces=WEB/REST
  └── type=SERVICE, allowedInterfaces=ONVIF/RTSP
```

Bên ngoài người dùng vẫn thấy mục "ONVIF Accounts" riêng; bên trong dùng chung password policy, audit, encryption và lifecycle. Đây là đề xuất tốt nhất cho Camera-alvis.

### Phương án C — mọi account dùng mọi giao diện

Dễ làm nhất nhưng bảo mật và audit kém. Không khuyến nghị production.

## 9. Ví dụ đầy đủ

Admin tạo trên Web:

```text
username = milestone_cam01
role     = Operator
scope    = ONVIF + RTSP
```

Milestone gọi:

```text
GetProfiles       → READ_MEDIA → cho phép
GetStreamUri      → READ_MEDIA → cho phép
RTSP DESCRIBE     → READ_MEDIA → cho phép
ContinuousMove    → ACTUATE    → cho phép
CreateUsers       → WRITE_SYSTEM → từ chối ter:NotAuthorized
FactoryDefault    → UNRECOVERABLE → từ chối ter:NotAuthorized
```

Một tài khoản backup:

```text
username = nvr_backup
role     = User/Media User
```

NVR backup được xem live, tìm và replay recording; không được quay PTZ hoặc đổi encoder. Đây là mục đích thực của phân quyền ONVIF account.

## 10. Đề xuất thực hiện theo thứ tự

```text
1. Tạo IdentityService/IdentityStore dùng chung, persistent
2. Chuyển cfg admin cố định thành lookup nhiều user
3. Chuyển Get/Create/Delete/SetUser sang IdentityService
4. Thêm bảng operation → access class → role
5. Cho RTSP server dùng chung IdentityStore
6. Thêm Web UI mục ONVIF Accounts
7. Thêm audit, lockout/rate-limit, password policy
8. Chạy lại DTT Security + S/T/M/G regression
```

Kết luận: mục ONVIF Account trên Web **không phải phần trang trí hay riêng cho Test Tool**. Nó là giao diện để quản trị service account mà VMS/NVR dùng khi tích hợp camera qua chuẩn ONVIF. Với Camera-alvis, nên tách tài khoản Web và ONVIF ở cấp mục đích/quyền, nhưng dùng chung một IdentityService an toàn ở bên dưới; SOAP và RTSP phải xác thực cùng ONVIF account.
