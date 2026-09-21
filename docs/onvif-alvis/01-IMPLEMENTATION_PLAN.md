# Kế hoạch tích hợp ONVIF Server với Camera-alvis

> Trạng thái: Accepted / Living document  
> Cập nhật gần nhất: 2026-09-04  
> Backend MGMT được đối chiếu: `D:\Elcom\NewVersion\frontend\MGMT\src\backend`

## 1. Mục tiêu

Thay `mock-camera-backend` bằng backend Camera-alvis thật theo từng capability, đồng thời giữ nguyên conformance Profile S, T, M và G của `onvif-module`.

Kết quả cuối:

```text
onvif-module = MGMT-02, process/repo độc lập
mock backend = test fixture/conformance baseline
real backend = adapter tới MGMT, DVR, VPU, Core, HAL và BUS
```

MGMT hiện tổ chức theo domain và dùng SQLite cục bộ (`mgmt.db`). Bảng
`users` phân biệt `type = 'web'` và `type = 'onvif'`; password được mã hóa
bởi `PasswordCrypto` với key do MGMT sở hữu. `onvif-module` không được mở
trực tiếp file SQLite hoặc đọc bảng `crypto_keys`. Mọi xác thực ONVIF phải đi
qua contract nội bộ do MGMT cung cấp.

## 2. Definition of Done chung

Một capability chỉ hoàn thành migration khi:

- Không còn lấy dữ liệu mock trong production mode.
- Getter phản ánh state thật; setter thực sự apply xuống service/hardware thật.
- Không hardcode IP, port, token hoặc identity.
- Có timeout, reconnect và error mapping sang SOAP Fault.
- Capability được advertise đúng với khả năng runtime.
- Mock regression không bị phá.
- DTT test liên quan pass.
- Test được với ít nhất một VMS thật.
- Restart dependency không làm onvif-server crash hoặc trả state giả.
- `02-INTEGRATION_MATRIX.md` được cập nhật kèm evidence.

## 2.1. Bắt buộc xác định đúng phạm vi TRƯỚC khi implement

Trước khi bắt đầu code bất kỳ operation ONVIF nào, phải đối chiếu đủ 2 nguồn
sau để xác nhận operation/field đó thực sự **mandatory**, tránh làm dư phạm vi
không cần thiết:

1. **Spec Profile tương ứng** (`docs/ONVIF_Profile_T_Specification_v1-0.md`,
   `docs/ONVIF_Profile_G_Specification_v1-0.pdf`,
   `docs/ONVIF_Profile_M_Client_Test_Specification_24.06.pdf`,
   `docs/onvif-profile-m-specification-v1-0.pdf`) — tìm đúng mục "Function
   list for devices", đọc cột **Requirement**: `M` (mandatory) chỉ có giá trị
   khi tiêu đề bảng là `Device MANDATORY`; nếu tiêu đề là `Device CONDITIONAL`
   ("if supported") thì `M` chỉ áp dụng **nếu thiết bị tự nhận hỗ trợ**, không
   phải baseline bắt buộc. Field/kiểu dữ liệu con (VD IPv6 trong
   `NetworkInterface`) không tự động mandatory chỉ vì nằm trong 1 operation
   mandatory — chỉ mandatory nếu spec nói rõ.
2. **`docs/onvif-alvis/g13.xml`** — log DTT chạy full Profile S/M/G/T thật
   (05/08/2026, trước khi có tích hợp MGMT). Test case nào **không xuất hiện**
   trong lần chạy full đó (`grep` theo `DEVICE-x-x-x`/tên test) là tín hiệu
   mạnh cho thấy nó optional/conditional, không cần ưu tiên. Test nào có mặt
   và `PASSED` là baseline hành vi SOAP đã đúng chuẩn, chỉ cần thay dữ liệu
   mock bằng MGMT thật mà không phá hành vi đó.

Chỉ triển khai phần vượt quá mandatory khi được yêu cầu rõ ràng, không tự ý mở
rộng "cho đầy đủ".

**Bài học đã xảy ra (2026-09-15/16), để tránh lặp lại:**

- Đã viết `GetNTP`/`SetNTP` trước khi kiểm tra spec — sau mới phát hiện mục
  8.8 NTP của Profile T là `Device CONDITIONAL`, và `DEVICE-3-1-12` không hề
  có trong `g13.xml`. Việc đã làm không sai nhưng không cần ưu tiên.
- Đề xuất thêm hỗ trợ IPv6 cho `GetDNS`/`SetDNS` và
  `GetNetworkInterfaces`/`SetNetworkInterfaces` trước khi kiểm tra spec — sau
  mới phát hiện `ONVIF_Profile_T_Specification_v1-0.md` **không hề nhắc tới
  IPv6** ở mục 7.4; chỉ cần IPv4 là đủ mandatory. Suýt làm dư việc không cần
  thiết (IPv6 trong `SetNetworkInterfaces` của MGMT có logic riêng, validate
  prefix/gateway phức tạp hơn hẳn IPv4).

## 3. Phase 0 — Đóng băng conformance baseline

### Công việc

- Gắn tag/commit baseline đã pass S/T/M/G.
- Lưu DTT reports theo profile.
- Lưu SOAP request/response mẫu.
- Lưu RTSP SDP, metadata XML và replay packet mẫu.
- Lập danh sách test bắt buộc chạy lại cho từng service.
- Xác minh mock mode vẫn chạy độc lập.

### Gate

- Có baseline tái lập được.
- Tất cả test hiện đang pass vẫn pass trước khi nối real backend.

## 4. Phase 1 — Backend facade và runtime configuration

### Công việc

- Giữ `ICameraBackend` làm compatibility facade trong giai đoạn đầu.
- Tách dần thành các interface domain:
  - `IDeviceBackend`
  - `IMediaBackend`
  - `IImagingBackend`
  - `IPtzBackend`
  - `IAnalyticsBackend`
  - `IEventBackend`
  - `IRecordingBackend`
  - `ISearchBackend`
  - `IReplayBackend`
  - `IIdentityBackend`
- Tạo `AlvisBackendFacade` để compose các adapter thật/mock.
- Thêm `mock`, `hybrid`, `production` mode.
- Thêm config cho MGMT, DVR, BUS và timeout; không hardcode port 8086.
- Endpoint MGMT mặc định hiện có thể là port 8086, nhưng đây chỉ là runtime
  configuration, không phải contract compile-time.
- Giữ `onvif.conf` làm cấu hình bootstrap của process `onvif-module`, tối thiểu
  gồm MGMT endpoint, timeout/retry, ONVIF listen port và RTSP port dự phòng.
  Process cần biết listen port trước khi có thể mở SOAP listener, vì vậy không
  được phụ thuộc tuyệt đối vào việc MGMT đã sẵn sàng tại thời điểm khởi động.
- Không cho `onvif-module` đọc trực tiếp `mgmt_network_config.json`. File này là
  persistence detail thuộc MGMT; ONVIF chỉ truy cập state qua
  `IMgmtBackend`/internal REST, Unix socket hoặc IPC contract có version.
- Phân biệt rõ:
  - `listen_port`: cổng process bind nội bộ.
  - `public_port`: cổng VMS/NVR thực sự truy cập và phải xuất hiện trong XAddr,
    service URI và response ONVIF.
  Hai giá trị có thể khác nhau khi có reverse proxy.
- Chuẩn hóa backend error và SOAP Fault mapping.
- Thêm health/reconnect/logging cho từng dependency.

### Gate

- Có thể chọn real/mock độc lập theo capability.
- onvif-server khởi động khi dependency chưa sẵn sàng và tự reconnect.
- Port bootstrap đủ để ONVIF listener khởi động; khi MGMT khả dụng, adapter đối
  chiếu desired state với runtime state và báo mismatch thay vì âm thầm công bố
  port không thực sự lắng nghe.
- Production mode không fallback mock.

### Tiến độ triển khai — Task 01 (2026-09-04)

**Trạng thái:** `REAL_IN_PROGRESS` — source skeleton đã được tạo; chưa có
evidence build hoặc runtime test trên Linux target.

Task này dựng nền để các operation ONVIF không còn phụ thuộc trực tiếp vào
`BackendConnector`/mock backend. Nó chưa migration Network, Discovery,
Authentication hoặc bất kỳ setter nào sang MGMT.

#### Đã thực hiện trong `onvif-module/onvif-module`

| Thành phần | File | Nội dung |
|---|---|---|
| Runtime configuration | `include/config/RuntimeConfig.h`, `src/config/RuntimeConfig.cpp` | Thay parser phẳng bằng parser có section; đọc `[server]`, `[auth]`, `[backend]`, `[capabilities]`, `[startup]`, `[discovery]`; hỗ trợ `mock`, `hybrid`, `production`. |
| Runtime config mẫu | `config/onvif.conf` | Cấu hình vertical slice: HTTP 8001, `hybrid`, `device=real`, MGMT 8086, mock optional, startup smoke test và Discovery đều tắt. |
| MGMT contract | `include/backend/IMgmtClient.h` | Tạo ranh giới client độc lập cho MGMT; không mở SQLite/`mgmt_network_config.json` trực tiếp. |
| MGMT HTTP client | `include/backend/HttpMgmtClient.h`, `src/backend/HttpMgmtClient.cpp` | Có HTTP GET tối giản, timeout, parse endpoint `http://host:port` và implementation đầu tiên cho `GET /mgmt/v1/Config/DeviceInformation`. |
| Facade | `include/backend/AlvisBackendFacade.h`, `src/backend/AlvisBackendFacade.cpp` | `ICameraBackend` compatibility facade. `device=real` gọi `IMgmtClient::getDeviceInformation`; các operation khác vẫn explicit mock hoặc throw nếu mock bị tắt. |
| Bootstrap | `src/main.cpp` | Main tạo mock connector chỉ khi capability còn dùng mock, tạo `HttpMgmtClient` + `AlvisBackendFacade`, rồi truyền facade vào `OnvifServer`. |
| Safe parallel discovery switch | `include/OnvifServer.h`, `src/OnvifServer.cpp`, `config/onvif.conf` | `discovery.enable=false` ngăn tạo/chạy `DiscoveryService`, do đó tiến trình mới không bind/join WS-Discovery UDP multicast 3702; HTTP/SOAP listener vẫn chạy độc lập. |
| Vertical-slice startup | `src/main.cpp` | `startup.run_smoke_tests=false` bỏ chuỗi test DateTime/Media/PTZ/Imaging khi khởi động; `backend.mock_required=false` cho phép server tiếp tục nếu mock Unix socket không tồn tại. |
| Request failure boundary | `src/OnvifServer.cpp`, `src/services/DeviceService.cpp` | Exception do operation còn route mock nhưng mock vắng mặt được chặn ở từng request và trả SOAP Receiver fault. `GetDeviceInformation` không còn fallback sang identity giả khi MGMT lỗi. |
| Build | `Makefile` | Bổ sung các source Task 01 vào `ipc-test`; target `full` tự nhận source mới qua `find src`. |

#### Hành vi routing hiện tại

```text
mode=mock
    Mọi operation → BackendConnector/mock như baseline.

mode=hybrid, device=real
    GetDeviceInformation → HttpMgmtClient → MGMT Config/DeviceInformation.
    Các capability còn lại → mock theo cấu hình; nếu mock không khả dụng và
    mock_required=false thì request tương ứng trả SOAP fault, process vẫn chạy.

mode=production
    Không có mock backend nếu tất cả capability được cấu hình real.
    Operation chưa có adapter thật → lỗi rõ ràng; không fallback mock.
```

#### Contract MGMT đã đối chiếu

```text
GET {mgmt_base_url}/mgmt/v1/Config/DeviceInformation
```

Response cần có `result = 1` và object `GetDeviceInformationResponse` với
`Manufacturer`, `Model`, `FirmwareVersion`, `SerialNumber`, `HardwareId`.
Đây là contract tạm thời của Task 01; trước khi production cần bổ sung
internal authentication, schema/version header, retry/reconnect policy và
mapping HTTP/backend error → SOAP Fault.

#### Chưa làm / không được hiểu là đã hoàn thành

- Chưa có `MgmtIdentityAdapter` hoặc ONVIF authentication thật.
- Chưa có adapter Network/Discovery persistent; `GetNetworkProtocols` vẫn chưa
  đọc runtime config/MGMT và `GetDiscoveryMode` vẫn memory-only trong module.
- `discovery.enable` mới chỉ là bootstrap process switch để test song song an
  toàn. Nó không thay thế SOAP `GetDiscoveryMode`/`SetDiscoveryMode`, không ghi
  MGMT và không đại diện cho desired state lâu dài.
- Chưa gọi hay đọc trực tiếp `mgmt_network_config.json`.
- Chưa apply `SetNetworkProtocols` hoặc restart/reload listener/MediaMTX.
- Chưa có HTTPS, retry loop/background reconnect, metrics hoặc health endpoint.
- Phần vertical-slice startup mới chỉ được static/syntax-check ở local, chưa
  full build/runtime test trên camera. Mọi agent tiếp theo phải ghi evidence
  vào `02-INTEGRATION_MATRIX.md`.

#### Handoff bắt buộc cho agent tiếp theo

1. Build source Task 01/vertical slice trên Linux target.
2. Chạy config mẫu với HTTP 8001, `device=real`, mock optional và Discovery
   disabled; không dừng/restart ONVIF cũ.
3. Xác nhận process mới không sở hữu UDP 3702 và có listener TCP 8001.
4. Gọi SOAP `GetDeviceInformation`, đối chiếu đủ 5 field với response MGMT 8086.
5. Tắt/tạm ngắt MGMT và xác nhận request trả SOAP fault, không trả identity giả
   và không làm process ONVIF dừng.
6. Sau đó triển khai `IIdentityBackend`/`MgmtIdentityAdapter` theo Phase 2.
7. Chỉ sau Authentication mới nối Network/Discovery persistent theo Phase 3.

#### Build evidence trên camera (2026-09-07)

Đã build thành công source ONVIF baseline hiện có trên camera `alvisv4`
(`Ubuntu 20.04.6`, `aarch64`) tại:

```text
/home/alvis/tungdt/onvif/onvif-tools/onvif-module/onvif-module
```

Evidence cuối build:

```text
[LINK] onvif-server
[DONE] onvif-server
exit code: 0
```

Để build được trên camera, đã cài package `gsoap` và `libgsoap-dev`, đồng thời
đã sửa hai tương thích gSOAP 2.8.91 trong source:

- `DeviceService.cpp`: dùng `tds__SystemCapabilities::HttpFirmwareUpgrade`
  thay cho field cũ `FirmwareUpgrade`.
- `scripts/gen_gsoap.sh`: sau khi copy stock `struct_timeval.c`, đổi
  `SOAP_TYPE_xsd__dateTime` thành symbol generated
  `SOAP_TYPE_xsd__dateTime_`.

`soapcpp2` vẫn in warning/semantic diagnostic về `wsa.h`/`wsa5.h`, và generated
source có warning formatting/indentation; tuy nhiên `make full` đã link thành
công. Các warning này phải được giữ trong build evidence và kiểm tra lại khi
nâng version gSOAP/WSDL.

**Ranh giới quan trọng:** build pass này xác nhận baseline source đã copy trên
camera, không phải evidence rằng Task 01 facade/MGMT client đã được sync hoặc
build trên camera. Task 01 vẫn là `REAL_IN_PROGRESS` cho đến khi source branch
chứa `RuntimeConfig`, `HttpMgmtClient` và `AlvisBackendFacade` được build/test.

#### Safe parallel startup switch (2026-09-07)

Đã nối cấu hình `discovery.enable` theo luồng:

```text
config/onvif.conf
    -> RuntimeConfig::discoveryEnabled
    -> OnvifServer(..., discoveryEnabled)
    -> chỉ tạo/start DiscoveryService khi true
```

Config mẫu hiện đặt `false` để khi chạy ONVIF mới song song với ONVIF cũ, tiến
trình mới không mở WS-Discovery UDP 3702. HTTP/SOAP listener không bị tắt và có
thể dùng port riêng (dự kiến 8001) để gọi trực tiếp
`/onvif/device_service`.

Khi khởi động, cần thấy hai log sau để xác nhận config đã được áp dụng:

```text
WS-Discovery: disabled
[OnvifServer] WS-Discovery disabled by configuration; UDP 3702 will not be opened
```

Đây chỉ là source/static verification; thay đổi chưa được build hoặc runtime
test trên camera trong task này. Không được đánh dấu `REAL_VERIFIED` cho đến
khi kiểm tra process mới không sở hữu UDP 3702 và SOAP endpoint port riêng trả
response thành công.

#### Device-information vertical-slice startup (2026-09-07)

Config mẫu được chuẩn bị để chạy song song an toàn:

```ini
[server]
device_ip = 192.168.8.127
http_port = 8001
rtsp_port = 8554

[backend]
mode = hybrid
mock_required = false
mgmt_base_url = http://127.0.0.1:8086

[capabilities]
device = real

[startup]
run_smoke_tests = false

[discovery]
enable = false
```

`mock_required=false` không biến các capability mock thành dữ liệu thật. Nó chỉ
cho phép tiến trình khởi động khi mock Unix socket vắng mặt. Request đi vào
capability chưa migrate phải trả SOAP fault. Riêng `GetDeviceInformation`, lỗi
MGMT cũng trả Receiver fault; source không còn trả bộ Manufacturer/Model/
Firmware/Serial/HardwareId hardcode để che lỗi integration.

## 5. Phase 2 — Identity, Authentication và Authorization foundation

### Mục tiêu

Thiết lập authentication thật trước khi chuyển các operation cần quyền như
`GetDeviceInformation`, Imaging, Media và Recording sang backend thật.

SOAP và RTSP dùng chung ONVIF account store, nhưng account ONVIF tách mục
đích khỏi Web account bằng `users.type = 'onvif'`. Web login/token và ONVIF
WS-Security/Digest dùng chung repository/credential core nhưng là hai protocol
authentication service riêng.

### Nguồn thật

- MGMT `domains/user`: `UserRepository`, `UserService`, `PasswordCrypto`.
- SQLite `mgmt.db`: bảng `users`, `crypto_keys`, do MGMT sở hữu độc quyền.
- `onvif-module`: parse security header, ONVIF operation/access-class mapping
  và SOAP Fault behavior.
- DVR/RTSP service: áp dụng kết quả xác thực từ cùng nguồn ONVIF credential.

### Ranh giới bắt buộc

```text
VMS / ONVIF client
        │ SOAP WSSE hoặc HTTP Digest
        ▼
onvif-module
        │ internal REST hoặc Unix socket (runtime-configured)
        ▼
MGMT OnvifAuthenticationService
        │ UserRepository.find(username, Onvif)
        ▼
SQLite mgmt.db
```

- Không cho `onvif-module` mở trực tiếp `mgmt.db`.
- Không trả plaintext password hoặc encryption key qua API/IPC.
- Không dùng Web JWT/session làm credential của ONVIF client.
- Internal authentication giữa `onvif-module` và MGMT/DVR dùng service
  credential riêng, không dùng account VMS.

### Công việc MGMT

- Tạo credential core dùng chung cho Web và ONVIF trên
  `UserRepository`/`PasswordCrypto`.
- Mở rộng `UserService` hiện có với nghiệp vụ xác minh ONVIF, giữ HTTP adapter
  mỏng trong `UserApiController`; không tạo service/controller song song khi
  cùng thuộc domain user.
- Định nghĩa internal contract xác minh WS-Security UsernameToken
  PasswordDigest: Username, Nonce, Created và PasswordDigest.
- Định nghĩa internal contract xác minh HTTP/RTSP Digest: username, realm,
  method, URI, nonce, qop, nc, cnonce, algorithm và response.
- MGMT tự giải mã credential trong memory phạm vi ngắn để verify proof; không
  trả password cho caller và không ghi credential/digest/key vào log.
- Bổ sung account state và security state tối thiểu: `enabled`,
  `failed_attempts`, `locked_until`, `last_login_at`, `password_changed_at`.
- Bổ sung audit, rate limit, lockout và password policy.
- Thêm SQLite schema migration có version (`PRAGMA user_version`). Không dựa
  vào `CREATE TABLE IF NOT EXISTS` để nâng cấp database đã tồn tại.
- Khi schema thay đổi, cập nhật đồng thời embedded schema trong repository và
  `db/schema.sql`.
- Tắt `MockAuthApiController` bằng runtime/build configuration; production
  không được đăng ký controller chấp nhận mọi username/password.

### Công việc onvif-module

- Tạo `IIdentityBackend`/`MgmtIdentityAdapter`; service handler không gọi
  HTTP/SQLite trực tiếp.
- Ưu tiên WS-Security UsernameToken PasswordDigest để giữ behavior gSOAP đã
  pass và tương thích baseline legacy.
- Kiểm tra Timestamp và cache `(username, nonce, created)` để chống replay.
- Sau đó nối HTTP Digest cho SOAP và dùng cùng credential source cho RTSP
  live/replay.
- Chuẩn hóa `AuthenticatedPrincipal` gồm user id, username, type và role.
- Map operation → access class → role; authentication và authorization là hai
  bước tách biệt.
- Map thiếu/sai credential sang HTTP 401 hoặc ONVIF `ter:NotAuthorized` đúng
  security mechanism đang dùng.

#### WSSE PasswordDigest vertical slice (2026-09-14)

Đã nối source local theo luồng:

```text
SOAP UsernameToken PasswordDigest
  -> WsSecurityHandler
  -> IMgmtClient::verifyWssePasswordDigest
  -> HttpMgmtClient POST /internal/v1/auth/onvif/wsse-password-digest
  -> MGMT UserApiController -> UserService
  -> users(type=onvif) + PasswordCrypto
```

- `mock` tiếp tục dùng credential tĩnh để giữ baseline conformance.
- `hybrid` và `production` dùng MGMT cho WSSE PasswordDigest và không fallback
  về `admin/admin123`.
- `PasswordText` bị từ chối trong real mode vì MGMT chưa có contract tương ứng.
- HTTP Digest trong real mode được chuyển qua MGMT bằng contract nội bộ riêng.
  ONVIF module phát nonce ngẫu nhiên, giữ TTL 5 phút và cưỡng chế nonce-count
  tăng; MGMT xác minh proof bằng user `type=onvif` trong SQLite.
- MGMT/network unavailable được coi là authentication failure; password,
  encryption key và digest không được ghi log.
- Runtime evidence ngày 2026-09-15 trên camera `192.168.8.127`: DTT xác thực
  HTTP Digest thành công bằng user `type=onvif` lưu trong SQLite MGMT, qua
  `onvif-module:8001 -> MGMT:8086`, và đọc được `GetDeviceInformation`.
- HTTP Digest vertical slice được coi là `REAL_VERIFIED`. Toàn bộ Phase 2 vẫn
  là `REAL_IN_PROGRESS` vì WSSE runtime evidence, RBAC và security hardening
  chưa hoàn tất.

### Gate

- ONVIF user được tạo/sửa/xóa persistent trong SQLite với `type = 'onvif'`.
- Web user cùng username không tự động trở thành ONVIF user.
- `GetDeviceInformation` thành công bằng WSSE PasswordDigest thật.
- Sai username/password, Timestamp hết hạn và replay Nonce đều bị từ chối.
- Role được áp đúng theo ONVIF access class; Viewer không gọi được
  `WRITE_SYSTEM`, Administrator gọi được operation được phép.
- SOAP, RTSP live và RTSP replay dùng chung nguồn ONVIF credential.
- Không có plaintext password/key trong API response hoặc log.
- Mock authentication không tồn tại trong production runtime.
- Có unit test digest/replay/RBAC, integration test MGMT↔onvif-module và DTT
  security regression evidence.

## 6. Phase 3 — Device, Network, DateTime và Discovery

### Nguồn thật

MGMT-01, mặc định qua internal REST trên endpoint cấu hình runtime.

### Ownership và persistence của Network/Discovery

MGMT là chủ sở hữu desired state của network protocol, Discovery Mode và
scopes. Trong implementation MGMT hiện tại, các dữ liệu này chưa nằm trong
SQLite `mgmt.db` mà được `JsonNetworkRepository` lưu tại
`./mgmt_network_config.json`, tương đối với working directory của process
`mgmt`.

Ví dụ khi chạy host build từ thư mục `src/backend/build-host`, file thực tế là:

```text
D:\Elcom\NewVersion\frontend\MGMT\src\backend\build-host\mgmt_network_config.json
```

Khi deploy systemd với `WorkingDirectory=/media/sonnt1/mgmt-be/bin`, vị trí dự
kiến là:

```text
/media/sonnt1/mgmt-be/bin/mgmt_network_config.json
```

File hiện đảm nhiệm các chức năng:

- Persist `network_protocols`: Enabled, Name và Port cho HTTP, HTTPS, RTSP và
  ONVIF theo model quản trị nội bộ của sản phẩm.
- Persist `discovery`: `enabled` và `discovery_mode`.
- Persist network interfaces, hostname, IP filter, SNMP, scopes và audit log
  của các thay đổi network do cùng repository quản lý.
- Khôi phục desired state sau khi MGMT restart. Nếu section chưa tồn tại, MGMT
  trả default từ source; response default không chứng minh file đã tồn tại.

Đây là file private của MGMT, không phải shared configuration contract giữa
hai repo. Ở giai đoạn tích hợp sau, developer/AI agent phải đọc và đối chiếu:

```text
MGMT src/backend/src/main.cpp
MGMT src/backend/src/domains/network/json_network_repository.cpp
MGMT src/backend/src/domains/network/network_protocol_api_controller.cpp
MGMT src/backend/src/domains/network/network_discovery_api_controller.cpp
MGMT src/backend/src/domains/network/linux_net_protocol_service.cpp
MGMT src/backend/src/domains/network/linux_net_discovery_service.cpp
runtime mgmt_network_config.json tại working directory thật
```

Mục đích của bước đối chiếu này là xác định ba trạng thái riêng biệt:

```text
configured state  Giá trị MGMT đã persist.
applied state     Giá trị đã apply xuống daemon/process thật.
advertised state  Giá trị ONVIF trả cho VMS qua SOAP/WS-Discovery/URI.
```

Ba trạng thái phải nhất quán trước khi đánh dấu capability là `REAL_VERIFIED`.
MGMT hiện mới persist protocol và log thay đổi; `LinuxNetProtocolService` chưa
thực sự đổi listener/restart service. Vì vậy không được coi
`SetNetworkProtocols` là production-complete chỉ vì JSON đã được ghi.

### Runtime configuration contract

- Giai đoạn đầu: `onvif-module` đọc `http_port` và `rtsp_port` từ `onvif.conf`
  để bootstrap listener và URI. `GetNetworkProtocols` phải dùng chính runtime
  config đã load, không dùng default hardcode trong `DeviceService`.
- Giai đoạn sau: MGMT tiếp tục sở hữu desired state; supervisor/MGMT adapter
  phân phối hoặc sinh runtime config và thực hiện reload/restart service.
- `onvif-module` không mở trực tiếp `mgmt_network_config.json`, không phụ thuộc
  đường dẫn build/deploy và không parse schema JSON private của MGMT.
- REST API MGMT có thể giữ protocol `ONVIF` như khái niệm quản trị sản phẩm,
  nhưng SOAP ONVIF `GetNetworkProtocols` chỉ công bố HTTP, HTTPS và RTSP.
- Khi module được truy cập trực tiếp, ánh xạ MGMT `ONVIF.Port` thành SOAP
  `HTTP.Port`; ánh xạ MGMT `RTSP.Port` thành SOAP `RTSP.Port`.
- Khi có reverse proxy, SOAP HTTP port và mọi XAddr phải dùng `public_port`,
  không dùng cổng bind nội bộ.
- `SetNetworkProtocols` phải hỗ trợ partial update, validate collision/range,
  persist desired state, apply/restart đúng owner và chỉ báo thành công khi có
  kết quả apply rõ ràng. Không được ghi đè các protocol không có trong request.

### Operation ưu tiên

- `GetDeviceInformation`
- `GetSystemDateAndTime`, `SetSystemDateAndTime`
- `GetNetworkInterfaces`, `SetNetworkInterfaces`
- `GetHostname`, `SetHostname`
- `GetDNS`, `SetDNS`
- `GetNTP`, `SetNTP`
- `GetNetworkProtocols`, `SetNetworkProtocols`
- `GetScopes`, `SetScopes`, `AddScopes`, `RemoveScopes`
- `GetDiscoveryMode`, `SetDiscoveryMode`
- `SystemReboot`
- `SetSystemFactoryDefault`

#### GetSystemDateAndTime / SetSystemDateAndTime (camera `192.168.8.124`, 2026-09-15)

```text
ONVIF DeviceService::GetSystemDateAndTime / SetSystemDateAndTime
  -> AlvisBackendFacade
  -> IMgmtClient::getSystemDateAndTime / setSystemDateAndTime
  -> GET|POST /mgmt/v1/Config/GetSystemDateAndTime|SetSystemDateAndTime
  -> MGMT DateTimeService + Linux runtime state (timedatectl, systemd-timesyncd)
```

- `GetSystemDateAndTime`: ánh xạ `DateTimeType`, `DaylightSavings`,
  `UTCDateTime`, `LocalDateTime` từ response MGMT; `TimeZone.TZ` biểu diễn
  theo POSIX offset tự tính từ cặp UTC/local MGMT trả về (không hardcode
  `UTC0`). Không lấy operation này từ mock backend.
- `SetSystemDateAndTime` (case Manual): validate lịch thật (leap year,
  days-in-month) trước khi gọi MGMT; nếu client gửi `TimeZone`, so offset với
  zone IANA hiện có — khớp thì giữ nguyên zone, lệch thì từ chối
  (`ter:InvalidTimeZone`) vì một offset POSIX có thể khớp nhiều zone IANA,
  không có cách suy ngược an toàn. Case NTP: đọc `NTPServer.Mode/Host` hiện
  tại từ MGMT (field này MGMT đã trả sẵn trong `GetSystemDateAndTime`) rồi
  gửi lại nguyên vẹn, vì bản thân `SetSystemDateAndTime` không mang theo NTP
  host; nếu MGMT chưa từng cấu hình NTP (`Host` rỗng) thì trả Receiver fault
  rõ ràng thay vì gửi thiếu dữ liệu xuống MGMT.
- MGMT lỗi hoặc payload thiếu/sai làm request trả SOAP Receiver fault
  (`result=0`/lỗi kết nối); MGMT từ chối do input sai (`result=-1`) map sang
  Sender fault. Không fallback sang giờ/mock của process `onvif-server`.
- **Evidence DTT trên camera `192.168.8.124`:** `DEVICE-3-1-1`
  (GetSystemDateAndTime), `DEVICE-3-1-4` (invalid timezone), `DEVICE-3-1-5`
  (invalid date), `DEVICE-3-1-11` (SetSystemDateAndTime thật, đổi giờ hệ
  thống thành công) — **cả 4 đều PASS**. Trạng thái: `REAL_DTT` (đã pass DTT,
  chưa test VMS/restart-failure nên chưa đủ điều kiện `REAL_VERIFIED`).
- Trong lúc debug đã phát hiện và fix 1 bug hạ tầng không liên quan logic
  DateTime: `make full` (không `clean`) có thể link nhầm object file cũ do
  Makefile thiếu dependency tracking theo header — gây HTTP Digest luôn
  reject dù đúng credential. Từ nay build trên camera nên dùng
  `make clean && make full`.

#### GetNTP / SetNTP — không mandatory, đã viết source nhưng chưa build/test (2026-09-15)

- Đối chiếu `ONVIF_Profile_T_Specification_v1-0.md` mục 8.8: `GetNTP`/`SetNTP`
  là **`Device CONDITIONAL` ("if supported")**, không phải mandatory baseline
  như `GetSystemDateAndTime`/`SetSystemDateAndTime` (mục 7.5, `Device
  MANDATORY`). Đối chiếu `g13.xml` (log DTT full S/M/G/T chạy 2026-08-05):
  `DEVICE-3-1-12 SETSYSTEMDATEANDTIME USING NTP` **không có trong lần chạy
  đó** — khớp với việc NTP là optional, không bắt buộc để đạt conformance.
- Đã viết `DeviceService::GetNTP`/`SetNTP` (đọc/ghi qua field `NTPServer`
  sẵn có trong `GetSystemDateAndTime`/`SetSystemDateAndTime`, không cần MGMT
  thêm API). Giới hạn đã biết: MGMT chỉ thực sự ghi `NTPServer` khi
  `DateTimeType=NTP` (nhánh Manual bỏ qua field này) nên `SetNTP` buộc phải
  gửi kèm `DateTimeType=NTP` — tức gọi `SetNTP` có side effect chuyển đồng hồ
  sang chế độ NTP, khác tinh thần ONVIF spec (SetNTP lẽ ra độc lập với
  DateTimeType) nhưng là cách duy nhất dữ liệu thực sự được lưu với contract
  MGMT hiện tại.
- **Chưa build/chưa có evidence trên camera** (tên field gSOAP
  `tt__NTPInformation`/`tt__NetworkHost`/`tt__NetworkHostType` suy từ đúng
  pattern `tt__DNSInformation`/`tt__IPAddress` đã chạy được trong cùng file,
  nhưng chưa verify compile thật). Vì không mandatory, không cần ưu tiên vá
  tiếp — chỉ nên quay lại nếu sản phẩm quyết định quảng cáo hỗ trợ NTP.

#### Network configuration — GetHostName/SetHostName, GetDNS/SetDNS, GetNetworkInterfaces/SetNetworkInterfaces, GetNetworkDefaultGateway/SetNetworkDefaultGateway, GetNetworkProtocols/SetNetworkProtocols (source local, 2026-09-16)

Đã đối chiếu `g13.xml` (log DTT full S/M/G/T 2026-08-05) theo đúng mục 2.1:
toàn bộ 13 test `DEVICE-2-1-x` liên quan (Hostname, DNS, NetworkInterface,
NetworkDefaultGateway, NetworkProtocols) đều **PASSED**, đều IPv4-only —
khớp `ONVIF_Profile_T_Specification_v1-0.md` mục 7.4 (không nhắc IPv6 chỗ
nào). **Cố tình bỏ qua IPv6** ở lần triển khai này dù MGMT đã hỗ trợ IPv6
thật đầy đủ (DNS, NetworkInterfaces, Gateway) — không cần cho mandatory,
xem mục 2.1.

```text
ONVIF DeviceService::Get/Set{Hostname,DNS,NetworkInterfaces,NetworkDefaultGateway,NetworkProtocols}
  -> AlvisBackendFacade (gate qua capabilities.network, giống getDeviceInfo —
     KHÔNG bypass như GetSystemDateAndTime)
  -> IMgmtClient::get/set{Hostname,Dns,NetworkInterface,NetworkGateway,NetworkProtocols}
  -> GET|POST /mgmt/v1/{GetHostname,SetHostname,GetDNS,SetDNS,
     GetNetworkInterfaces,SetNetworkInterfaces,GetNetworkDefaultGateway,
     SetNetworkDefaultGateway,GetNetworkProtocols,SetNetworkProtocols}
```

Các quyết định thiết kế quan trọng:

- **Subnet mask ↔ prefix length**: MGMT lưu/trả subnet dạng dotted mask
  (`"255.255.255.0"`), ONVIF `PrefixedIPv4Address` dùng CIDR prefix length
  (int). Đã viết `prefixLengthToSubnetMask()`/`subnetMaskToPrefixLength()`
  đổi 2 chiều ở `HttpMgmtClient`.
- **Port mapping `GetNetworkProtocols`/`SetNetworkProtocols`**: MGMT
  `"ONVIF"` ↔ SOAP `HTTP` (đúng như đã ghi sẵn ở mục Runtime configuration
  contract phía trên — MGMT `"HTTP"` là port web UI MGMT 8086, KHÔNG phải
  port SOAP). MGMT `"RTSP"` ↔ SOAP `RTSP`. SOAP `HTTPS` luôn công bố
  `Enabled=false` vì onvif-module chưa hỗ trợ TLS thật — không quảng cáo
  capability không có (nguyên tắc #5, `README.md`). `SetNetworkProtocols`
  với `HTTPS.Enabled=true` bị từ chối `ter:ActionNotSupported` ngay tại
  onvif-module, không gửi xuống MGMT.
- **Giới hạn đã biết — đổi port ONVIF chưa tự áp dụng**: `SetNetworkProtocols`
  đổi port `"ONVIF"` chỉ persist xuống MGMT, KHÔNG tự rebind listener đang
  chạy của onvif-module (chưa có cơ chế reload port runtime) — cần restart
  thủ công để port mới có hiệu lực thật. Nếu thiếu entry `"ONVIF"`/`"RTSP"`
  trong response MGMT, fallback về đúng `cfg_.httpPort`/`cfg_.rtspPort`
  runtime hiện tại thay vì bỏ trống.
- **Giới hạn đã biết — `SetNetworkInterfaces` áp dụng bất đồng bộ**: MGMT trả
  `result:1` ngay sau khi validate xong, việc apply thật (`nmcli`) chạy ở
  background thread riêng của MGMT — không có cách xác nhận apply thành công
  đồng bộ qua chính response này.
- `AlvisBackendFacade` gate các operation này qua `real("network")` (giống
  `getDeviceInfo`), KHÁC với `GetSystemDateAndTime`/`SetSystemDateAndTime`
  (bypass capability, luôn gọi MGMT bất kể mode) — nghĩa là cần đặt
  `capabilities.network = real` trong `onvif.conf` (hiện đang `mock`) thì
  các operation này mới thực sự dùng MGMT khi deploy lên camera.
- Mock path (`capabilities.network = mock`) được giữ nguyên qua
  `BackendConnector` với state mặc định y hệt `NetworkState` cũ trong
  `DeviceService.h` (không qua IPC — mock-camera-backend chưa có message
  type cho các operation này) để không phá baseline `g13.xml` khi cần chạy
  regression thuần mock.
- **Build/evidence trên camera `192.168.8.124` (2026-09-16):** đã build
  (`make clean && make full`) và chạy DTT thật. Trong lúc chạy phát hiện và
  fix 3 bug thật (không phải giả định): (1) `soap_receiver_fault_subcode`
  truyền `e.what()`/literal string vào tham số `detail` — tham số này là
  raw-XML (`##any`) theo schema SOAP 1.2, truyền text thường vi phạm schema
  → DTT fail; đã sửa toàn bộ 18 chỗ gọi (kể cả 1 chỗ có sẵn từ trước, không
  phải do lần sửa này) thành `nullptr`. (2) Thiếu `<tt:Port>` cho entry HTTPS
  trong `GetNetworkProtocolsResponse` dù `Enabled=false` — schema vẫn bắt
  buộc có Port → đã luôn push port. (3) Hardcode port HTTPS `443` — đã sửa
  lấy đúng port thật MGMT trả về, `443` chỉ dùng khi MGMT không có entry
  HTTPS.

- **Đối chiếu đầy đủ 14 test `DEVICE-2-1-x` trong `g13.xml`** (tất cả đều
  `Passed` ở baseline cũ, pre-MGMT) với tiến độ DTT thật hiện tại:

  | Test ID | Nội dung | Trạng thái |
  |---|---|---|
  | `2-1-1` | GetHostname | ✅ **PASS** (DTT thật, camera `.124`, 2026-09-16) |
  | `2-1-3` | SetHostname — error case | ✅ **PASS** (DTT thật) |
  | `2-1-33` | GetNetworkProtocols | ✅ **PASS** (DTT thật) |
  | `2-1-4` | GetDNS | ⬜ Chưa test — an toàn (read-only) |
  | `2-1-17` | GetNetworkInterface | ⬜ Chưa test — an toàn (read-only) |
  | `2-1-25` | GetNetworkDefaultGateway | ⬜ Chưa test — an toàn (read-only) |
  | `2-1-5` | SetDNS — SearchDomain | ⬜ Chưa test — rủi ro thấp |
  | `2-1-6` | SetDNS — DNSManual IPv4 | ⬜ Chưa test — rủi ro thấp |
  | `2-1-8` | SetDNS — FromDHCP | ⬜ Chưa test — rủi ro thấp |
  | `2-1-32` | SetHostname — case hợp lệ | ⬜ Chưa test — rủi ro thấp |
  | `2-1-35` | SetNetworkProtocols — unsupported protocols | ⬜ Chưa test — rủi ro thấp (case lỗi, MGMT từ chối nên không apply) |
  | `2-1-18` | SetNetworkInterface — IPv4 | ⬜ Chưa test — **⚠️ RỦI RO CAO**: đổi IP/subnet thật, có thể làm rớt kết nối tới camera ngay lập tức |
  | `2-1-30` | SetNetworkDefaultGateway — IPv4 | ⬜ Chưa test — **⚠️ RỦI RO CAO**: đổi gateway thật, có thể làm camera mất route ra ngoài cho mọi người đang dùng |
  | `2-1-34` | SetNetworkProtocols | ⬜ Chưa test — **⚠️ RỦI RO CAO**: `applyProtocolPorts()` phía MGMT gọi thật `systemctl restart rtsp_server.service` + `restart/stop onvif_server.service` → gián đoạn RTSP/ONVIF đang chạy |

  **Quyết định (2026-09-16, theo yêu cầu người dùng):** camera `.124` đang có
  nhiều người dùng thật (xem RTSP/web) nên **tạm hoãn 11 case còn lại**,
  đặc biệt 3 case rủi ro cao (`2-1-18`, `2-1-30`, `2-1-34`), để test vào lúc
  không có ai truy cập web/RTSP. Không tự ý chạy các case Set này khi chưa
  xác nhận camera đang rảnh.

#### Scopes (Get/Set/Add/RemoveScopes) — giữ local, dọn mock leftover (2026-09-16/18)

- **Quyết định kiến trúc**: KHÔNG nối MGMT cho Scopes ở giai đoạn này, dù MGMT
  đã có sẵn đủ 6 API (`Get/SetDiscoveryMode`, `Get/Set/Add/RemoveScopes`,
  đọc/ghi bảng SQL `network_scopes`/`network_discovery` thật, xem
  `network_discovery_api_controller.cpp`). Lý do: API Configurable Scope phía
  MGMT **chưa thực sự phát hành** — chỉ hiện trong Swagger, chưa có UI/luồng
  dùng thật (xác nhận trực tiếp từ người dùng) — giống tình huống
  `SystemReboot`/`SetSystemFactoryDefault` bên dưới: source có nhưng chưa sẵn
  sàng dùng thật, nối vào lúc này là làm dư và tự rước phụ thuộc vào 1 API có
  thể còn đổi.
- **Nguyên tắc phân loại rút ra** (áp dụng cho các quyết định tương tự sau
  này): dữ liệu phản ánh **khả năng phần mềm** (Fixed scope: Profile S/T/M/G,
  hardware) luôn nên hardcode/tính trong chính onvif-module — kể cả nếu MGMT
  có làm đúng 100% cũng không nên chuyển, vì 2 repo khác nhau dễ lệch đồng bộ
  theo thời gian, vi phạm nguyên tắc #5 README (không quảng bá capability
  không có thật). Dữ liệu do **người dùng cấu hình** (Configurable scope) mới
  nên đi qua MGMT, và chỉ khi tính năng đó đã thực sự release.
- **Bug tìm thấy khi audit MGMT** (chỉ để tham khảo, KHÔNG sửa — không phải
  repo của mình): `SqliteNetworkRepository::removeScopes()` có 1 danh sách
  Fixed scope hardcode để chặn xoá, nhưng **thiếu `Profile/M`, `Profile/G` và
  sai hardware string** (`Alvis-AI-Camera` thay vì `JetsonOrinNX-8GB` thật) —
  càng củng cố quyết định không lấy Fixed scope từ MGMT.
- **Dọn dẹp thực hiện trong onvif-module** (`DeviceService.h`/`.cpp`):
  1. Xoá `struct NetworkState`/`net_`/`netMtx_` — xác minh 100% không còn
     dùng ở đâu (dead code sót lại sau khi Network config đã chuyển hết sang
     `backend_`, kể cả chính field `hostname = "MockCam-4K"` bên trong đó).
  2. Sửa lỗi gõ nhầm `"onvif://www.onvif.org/Profilae/G"` → `.../Profile/G`
     trong Fixed scope list (bug thật, độc lập, do thiếu chữ "e" làm sai
     substring match `/Profile/` khi phân loại Fixed — người dùng tự phát
     hiện và sửa trong IDE).
  3. Scope `name/MockCam-4K` (rác mock lộ ra tận response SOAP thật, đã có
     trong `g13.xml` từ 2026-08-05) → patch động trong constructor
     `DeviceService::DeviceService`, dùng `std::call_once` gọi 1 lần
     `backend_->getDeviceInfo()` (đường thật, đã pass test từ trước) để thay
     bằng `name/<Model thật>`; nếu MGMT chưa sẵn sàng lúc khởi động thì giữ
     fallback trung tính `Alvis-Camera` (không còn chữ "Mock") và tự retry ở
     lần `copy()` kế tiếp (đặc tính của `call_once` khi callable ném lỗi).
  4. `hardware/JetsonOrinNX-8GB` giữ nguyên hardcode — xác nhận là hardware
     thật, không phải mock; chỉ còn nghi vấn phụ (chưa xác minh) là có đúng
     cho MỌI dòng camera Alvis (ANPR vs Bullet) hay chỉ 1 SKU.
- **Evidence build/test trên `.124` (2026-09-18)**: build (`make clean && make
  full`), restart thành công, DTT xác nhận `GetScopes` trả về đúng
  `name/ALN2-58` (Model thật từ `GetDeviceInformation`, Manufacturer "ERABYTE
  INC.") thay vì "MockCam-4K" — fix hoạt động đúng như thiết kế.

#### Discovery Mode / DISCOVERY-1-1-x — BLOCKED kép: bug MGMT + `discovery.enable=false` (2026-09-16/18)

- **Không nối `SetDiscoveryMode`/`GetDiscoveryMode` xuống MGMT thật** — audit
  `linux_net_discovery_service.cpp` tìm thấy 3 bug cụ thể:
  1. `isDiscoveryActive()`: so khớp bằng `out.find("active")`, mà chuỗi
     `"inactive"` cũng chứa substring `"active"` → hàm gần như luôn trả
     `true` bất kể service thật đang chạy hay dừng hẳn (chỉ sai `false` đúng
     khi service hoàn toàn không cài).
  2. `applyDiscoveryMode()`: luôn `return true` cứng, không đọc exit code
     thật của lệnh `systemctl` (dù cùng file header đã có sẵn
     `executeCommandWithStatus()` đọc đúng exit code, đang được dùng đúng ở
     `linux_net_dns_service.cpp`) → nhánh lỗi phía controller
     (`if (!applied) return 500`) là dead code.
  3. Get kiểm tra cả `wsdd` lẫn `onvif_discovery` (đúng tên service thật mà
     DVR cũ dùng, xem `D:\Elcom\DVR\dvr\service\onvif_discovery.service`),
     nhưng Set **chỉ đụng `wsdd`**, không bao giờ start/stop `onvif_discovery`
     — bất đối xứng, khiến Set gần như vô tác dụng với daemon thật trên dòng
     máy dùng `onvif_discovery`.
  → 3 bug này cộng hưởng có thể khiến DB tự đảo ngược giá trị vừa Set (xem
  phân tích chi tiết trong lịch sử hội thoại 2026-09-16). Đã quyết định
  **không báo cáo lỗi sang MGMT lúc này**, chỉ ghi nhận nội bộ để không ai
  vô tình nối API này vào sau.
- **Phát hiện thêm (2026-09-18, từ DTT thật trên `.124`)**: `config/onvif.conf`
  hiện có `[discovery] enable = false` — switch tạm thời từ "Safe parallel
  startup" (mục Task 01, 2026-09-07) để chạy song song an toàn với ONVIF cũ
  (`/opt/dvr_apps/sub_process/onvif_server`, PID thật vẫn đang chạy trên máy,
  cổng 8000), tránh giành UDP 3702. Hệ quả: `OnvifServer` không tạo
  `DiscoveryService`, nên `DeviceService::SetScopes/AddScopes/RemoveScopes`'s
  `DiscoveryService::current()` luôn `nullptr` → **`announceHelloNow()` không
  bao giờ chạy** trong suốt runtime hiện tại.
  - Verify trực tiếp trên `.124`: `ss -lun | grep 3702` → không ai lắng nghe
    UDP 3702 (kể cả ONVIF cũ PID 682 hiện tại).
  - Hệ quả cho DTT: test `DISCOVERY-1-1-11-v21.06 DEVICE SCOPES CONFIGURATION`
    fail ở step "Waiting for Hello message from the DUT" sau `AddScopes` (chờ
    60s, không nhận được) — **không phải bug code Scopes/AddScopes** (code
    Hello chưa từng chạy được vì bị guard `nullptr`). Ngược lại
    `DISCOVERY-1-1-9-v21.06 DISCOVERY MODE CONFIGURATION` lại PASS toàn bộ
    kể cả bước chờ Hello/Bye — nguồn Hello DTT nhận được (nếu có) không xác
    định được là từ đâu (không loại trừ ONVIF cũ hoặc thiết bị khác trên
    mạng), chưa có công cụ bắt gói tin (tcpdump) để xác minh thêm.
  - **Kết luận**: toàn bộ nhóm test `DISCOVERY-1-1-x` hiện KHÔNG đánh giá
    được đáng tin cậy cho onvif-module mới, kết quả pass/fail mang tính ngẫu
    nhiên theo nguồn phát Hello không xác định. Muốn test thật cần bật lại
    `discovery.enable=true`, nhưng việc đó phải giải quyết trước xung đột UDP
    3702 với ONVIF cũ đang phục vụ người dùng thật — **quyết định này ảnh
    hưởng ONVIF cũ, chưa xử lý, chờ người dùng xác nhận thời điểm/hướng đi**.

#### SystemReboot / SetSystemFactoryDefault — BLOCKED, chờ MGMT (2026-09-15)

- Đọc source `D:\Elcom\NewVersion\frontend\MGMT\src\backend` thấy có sẵn
  controller `MaintenanceRestoreApiController` với 3 endpoint tương ứng:
  `POST /mgmt/v1/Maintenance/Reboot` (map `SystemReboot`),
  `POST /mgmt/v1/Maintenance/RestoreDefault` (map
  `SetSystemFactoryDefault(FactoryDefault=Soft)`, giữ user/logs),
  `POST /mgmt/v1/Maintenance/FactoryReset` (map
  `SetSystemFactoryDefault(FactoryDefault=Hard)`, reset DB tài khoản + xóa
  storage + tự reboot sau 2s).
- **Team backend MGMT xác nhận trực tiếp (2026-09-15): 2 API này (`Reboot`,
  `RestoreDefault`/`FactoryReset`) CHƯA làm xong**, dù đã thấy trong source
  local đối chiếu được. Không dựa vào việc đọc được source để coi là sẵn
  sàng dùng — **chờ MGMT xác nhận hoàn thiện rồi mới nối**.
- Rủi ro cần nhớ khi quay lại: `Hard FactoryReset` sẽ xóa toàn bộ tài khoản
  ONVIF trong SQLite (kể cả user vừa dùng để test DTT) và đổi network về
  DHCP — không nên test trực tiếp trên camera đang dùng để phát triển mà
  không có kế hoạch khôi phục lại IP/tài khoản sau đó.

### Lưu ý

- Tạo canonical DTO, không expose trực tiếp JSON MGMT trong SOAP service.
- Map năm field chuẩn của `GetDeviceInformation`: Manufacturer, Model, FirmwareVersion, SerialNumber, HardwareId.
- Xử lý việc đổi IP mà không làm response treo hoặc giữ endpoint cũ.
- Scope chỉ công bố profile thực sự hỗ trợ.
- `GetNetworkProtocols` phản ánh public/runtime endpoint thật, không chỉ phản
  ánh JSON desired state.
- `GetDiscoveryMode` đọc persistent state từ MGMT. `SetDiscoveryMode` persist
  qua MGMT rồi điều khiển `DiscoveryService` trong `onvif-module`.
- Khi `Discoverable`, module trả lời WS-Discovery Probe/Resolve và công bố XAddr
  đúng public endpoint. Khi `NonDiscoverable`, module không trả lời
  Probe/Resolve.
- Sau restart, `onvif-module` phải phục hồi Discovery Mode từ MGMT; khi MGMT
  tạm unavailable, dùng last-known-good/bootstrap state có đánh dấu degraded,
  không âm thầm thay bằng mock state.
- `onvif-module` là owner của ONVIF WS-Discovery runtime. Không chạy thêm daemon
  `wsdd` độc lập cho cùng device identity/XAddr vì có thể tạo response trùng và
  state không nhất quán.

### Gate

- Device/Discovery tests của S/T/M/G pass.
- SOAP `GetNetworkProtocols`, WS-Discovery XAddr, ONVIF listener và RTSP URI
  khớp các port runtime/public thực tế.
- `SetNetworkProtocols` đã persist và apply; restart service vẫn giữ đúng port.
- `GetDiscoveryMode` giữ đúng state qua restart; `NonDiscoverable` không trả lời
  Probe/Resolve.
- Không có hai WS-Discovery responder công bố cùng một thiết bị.
- Không còn device identity/network mock trong hybrid-real mode.

## 7. Phase 4 — Media, profile và RTSP live

### Nguồn thật

DVR media/profile/encoder/RTSP service.

**Xác nhận (2026-09-18):** source thật nằm tại `D:\Elcom\Ovif-mock\AlvisOS\DVR`
(kiến trúc mới, cùng cấp với `AlvisOS\{BUS,CORE,GATEWAY,HAL,MGMT}`, KHÔNG phải
`D:\Elcom\DVR\dvr` — đó là DVR cũ chỉ dùng đối chiếu hành vi legacy). Trên
camera `.124`, DVR mới đang chạy thật: `/opt/dvr_apps/dvr` (build 2026-09-18),
service `dvr_new.service`, dùng `libdvr_lib.so`/`libbus.so`/`libhal.so` —
đúng kiến trúc module hoá tả trong README. REST API nghe tại
`http://<device-ip>:8200/dvr/v1.0/...` (đã verify `ss -lntp` trên `.124`,
nghe `0.0.0.0`, gọi thẳng được không cần qua Apache proxy — proxy chỉ ảnh
hưởng audit log phía DVR, không chặn request trực tiếp).

Stack cũ (`onvif_server.service` cổng 8000, `rtsp_server.service`,
`videoexport.service`, nhánh `/opt/dvr_apps/sub_process/*`) **vẫn đang chạy
song song** trên cùng camera — cần lưu ý khi test tránh xung đột (đã thấy
tương tự với UDP 3702 ở phần Discovery).

#### Research: contract `GetProfiles` thật + `GetSnapshotUri` (2026-09-18)

- **`GET /dvr/v3.0/GetProfiles`** (`api_helpers.cpp::build_profiles_json`):
  response JSON gần như khớp thẳng field name ONVIF — `token`, `Name`,
  `Enabled`, `Type`, `VideoSourceConfiguration{token,Name}`,
  `VideoEncoderConfiguration{token,Name,Encoding,Resolution{Width,Height},
  RateControl{FrameRateLimit,EncodingInterval,BitrateTarget,GovLength,
  EncodingProfile,BitrateControl}}` — độ tin cậy mapping cao, không cần đoán
  nhiều.
- **Phát hiện quan trọng — 2 loại stream URI song song, chỉ 1 loại dùng được
  cho ONVIF**: mỗi profile có cả `StreamUri` (`rtsp://host:rtsp_port/live/
  ch<N>` — do chính DVR tự chạy 1 RTSP server riêng, KHÔNG phải MediaMTX) và
  `WebStreamUri` (`http://host:whep_port/dvr_ch<N>/whep` — WebRTC/WHEP, do
  MediaMTX phục vụ, chỉ dành cho web UI xem live qua trình duyệt). Code có
  comment xác nhận MediaMTX bị tắt module RTSP riêng để tránh đụng port với
  DVR (`"Disable mediamtx RTSP server to avoid port conflict with DVR"`).
  → ONVIF `GetStreamUri` phải map vào field `StreamUri` có sẵn trong
  `GetProfiles`, KHÔNG được dùng API riêng `GetH264StreamUri` (comment trong
  code ghi rõ "(WebRTC WHEP)" — API đó dành cho web UI, trả về `WebStreamUri`,
  sai giao thức nếu lấy nhầm cho ONVIF).
- **`GetSnapshotUri` — đã verify đủ điều kiện map, KHÔNG cần yêu cầu team DVR
  bổ sung gì**:
  - Đối chiếu đúng 9 bước DTT thật đã PASS ở `MEDIA-6-1-1` (`g13.xml`): chỉ
    cần `GetSnapshotUri` trả `MediaUri.Uri` hợp lệ cú pháp, HTTP GET vào đó
    trả `200` + `Content-Type` đúng + byte là JPEG hợp lệ — **không hề kiểm
    tra độ phân giải ảnh có khớp đúng profile (main/sub/third) hay không**.
  - DVR có sẵn `GET /dvr/v1.0/GetSnapshot?profile=<channel_id>` → đọc tới tận
    `DvrController::getSnapshot()` (dvr_controller.cpp:586): lấy
    `latest_frame_ref` (frame thật mới nhất qua BUS) rồi encode thật bằng
    `mjpeg_codec->encode()` (hardware codec) — **không phải mock/placeholder**,
    trả `image/jpeg` đúng chuẩn, có xử lý lỗi (404 khi chưa có frame/encode
    lỗi) tử tế.
  - Thiết kế: `DeviceService::GetSnapshotUri` không cần gọi DVR lúc xử lý
    request — chỉ tự ghép chuỗi
    `http://<device_ip>:8200/dvr/v1.0/GetSnapshot?profile=<channel_id>` làm
    `Uri` trả về (giống cách build URI tĩnh, không phải fetch dữ liệu).
  - Lưu ý khi code (không phải bug DVR): `ProfileToken` dạng `"0_sub"` phải
    tự tách lấy phần số đầu (`"0"`) làm `channel_id` trước khi ghép URL —
    `std::stoul` phía DVR chỉ đọc phần số đầu, tự bỏ qua hậu tố; chấp nhận
    được vì DTT không kiểm tra độ phân giải theo từng profile.
- **Đối chiếu `g13.xml` — toàn bộ operation ưu tiên đều mandatory và từng
  PASS** ở baseline cũ: `MEDIA-1-1-1/3/5` (GetProfiles Media1),
  `MEDIA-2-2-1/4` (Video Source Config), `MEDIA-2-3-1/4` (Video Encoder
  Config), `MEDIA-6-1-1` (Snapshot URI), `MEDIA2-1-1-4` (GetProfiles Media2),
  `MEDIA2-2-2-4/7`, `MEDIA2-2-3-3`, `MEDIA2-5-1-1` (Snapshot URI Media2),
  `MEDIA2_RTSS-*`/`RTSS-*` (streaming thật H.264/JPEG nhiều transport). Danh
  sách "Operation ưu tiên" hiện tại là đúng và đủ, không cần thêm/bớt.
- **Chưa làm**: chưa thiết kế/code adapter `IDvrClient`/`HttpDvrClient` thật
  (tương tự `HttpMgmtClient` ở Phase 3) — mới dừng ở nghiên cứu contract.

#### DVR REST API có gate xác thực Token — đã tắt tạm trên `.125` để dev (2026-09-18/21)

- **Phát hiện**: mọi endpoint `/dvr/v1.0|v3.0/...` (kể cả `GetProfiles`,
  `GetSnapshot`) đều bắt buộc header `Token` do `ApiServer::installTokenGate()`
  (`api_server.cpp`) chặn — `publicPaths()` rỗng, không có ngoại lệ. Token
  được verify bằng cách gọi `POST http://127.0.0.1:8101/nse/v1.0/VerifyToken`
  — 1 service riêng (`nse.service`), xác nhận đang chạy thật trên cả `.124`
  lẫn `.125`.
- **Không phải bug/thiếu sót mới, cũng không phải "đã bỏ từ lâu" như nghi
  ngờ ban đầu** — đối chiếu source DVR cũ (`D:\Elcom\DVR\dvr`,
  `dvr_control_worker.cpp:6295`) thấy đúng lời gọi
  `POST /nse/v1.0/VerifyToken` đã tồn tại **từ trước**, nhưng bị **comment,
  không chạy** — DVR cũ dùng cơ chế khác (list `tokens_user` nạp từ DB). DVR
  mới đã "đánh thức" lại đúng đoạn code cũ này, biến nó thành đường xác thực
  chính thức, mặc định bật (`DVR_TOKEN_CHECK=1`). `onvif_server` cũ chưa từng
  set header Token khi gọi DVR (đã grep toàn bộ `test.cpp`, không thấy) — vì
  deployment cũ nhiều khả năng tắt hẳn cờ `enableToken` phía DVR, không phải
  vì onvif_server "biết cách" xử lý token.
- **Không tự tìm ra được cách onvif-module lấy token hợp lệ** (source
  `nse.service` không nằm trong 6 module `AlvisOS` đã đối chiếu) → theo yêu
  cầu người dùng, đã nhờ team DVR **tắt tạm token check trên `.125`** để dev.
  Verify trực tiếp (2026-09-21):
  ```
  /opt/dvr_apps/dvr_new.env trên .125:
  DVR_TOKEN_CHECK=0   # 1 = on (default), 0 = off for local testing

  curl http://127.0.0.1:8200/dvr/v3.0/GetProfiles -> HTTP 200, trả đủ 4 profile thật
  ```
  Comment trong `.env` ghi rõ đây là cờ **cho dev/test cục bộ**, không phải
  trạng thái production dự kiến — production sau này vẫn cần bật lại
  `DVR_TOKEN_CHECK=1` và có cơ chế lấy token thật cho onvif-module. Việc này
  **chưa giải quyết**, chỉ tạm gỡ để code/test Phase 4 không bị chặn.
- **Vấn đề riêng, vẫn còn treo cho `GetSnapshotUri`**: dù tắt token trên
  `.125` giúp onvif-module tự gọi DVR nội bộ được, nhưng `GetSnapshotUri`
  trả `Uri` cho **VMS bên ngoài tự gọi trực tiếp** — nếu sau này production
  bật lại `DVR_TOKEN_CHECK=1`, VMS sẽ không biết cách đính token nội bộ này,
  bị 401. Cần 1 trong: (a) thêm `/dvr/v1.0/GetSnapshot` vào `publicPaths()`
  phía DVR, (b) DVR nhận token qua query string thay vì chỉ header (để nhúng
  vào URI trả về), hoặc (c) onvif-module tự proxy ảnh (mở thêm 1 endpoint
  HTTP thường ngoài SOAP, tự gọi DVR kèm token thật rồi relay JPEG lại cho
  VMS). Chưa chọn hướng, cần bàn với team DVR khi tới lúc bật lại token thật.
- **Dữ liệu thật thu được từ `.125` — lưu ý khi code**:
  - `token` theo đúng convention đã giả định: `"0"`/`"0_sub"`/`"1"`/`"1_sub"`
    (số kênh + hậu tố `_sub`), khớp logic tách `channel_id` cho
    `GetSnapshotUri` đã thiết kế.
  - `StreamUri` trả `host=127.0.0.1` (đúng vì gọi nội bộ) — khi build lại
    `Uri` trả cho ONVIF phải thay bằng IP thật của thiết bị
    (`cfg_.deviceIp`), không dùng nguyên host DVR trả về.
  - **RTSP port khác nhau theo từng máy**: `.125` dùng `1992`, `.124` dùng
    `554` — không được hardcode port RTSP, phải lấy động từ chính field
    `StreamUri` DVR trả về (parse port ra) hoặc từ 1 API riêng, không suy
    đoán cố định.

### Operation ưu tiên

- Media1/Media2 `GetProfiles`
- `GetVideoSources`
- `GetVideoSourceConfigurations`
- `GetVideoEncoderConfigurations`
- `GetVideoEncoderConfigurationOptions`
- `SetVideoEncoderConfiguration`
- `GetStreamUri`
- `GetSnapshotUri`
- Metadata configuration/profile operations cần cho Profile M.

### Công việc nền

- Chốt token registry dùng chung.
- Chốt public host/port/path của RTSP URI.
- Map codec/resolution/framerate/bitrate/profile từ runtime thật.
- Đảm bảo SDP khớp với response ONVIF.
- Không truyền video qua onvif-server; onvif-server chỉ trả URI/control.

### Gate

- VMS/VLC mở được RTSP thật.
- Profile S/T media tests pass.
- Thay encoder config phản ánh đúng ở runtime.

## 8. Phase 5 — Imaging, lens và PTZ

### Nguồn thật

- Imaging: MGMT Imaging API + HAL/ISP.
- PTZ/zoom/focus: BUS control IPC + HAL.

### Công việc

- Map ImagingSettings và ImagingOptions đúng range/default/step của HAL.
- Chỉ advertise control thực sự hỗ trợ.
- Tách optical zoom/focus khỏi external pan/tilt.
- Chuẩn hóa coordinate/range PTZ.
- Kiểm tra behavior khi không có active lens hoặc HAL unavailable.

### Gate

- Get/Set imaging dùng state thật.
- PTZ command đến hardware thật.
- Imaging/PTZ test không regression.

## 9. Phase 6 — Profile M analytics metadata và event

### Nguồn thật

- Detection/tracking/metadata: VPU.
- Rule/alarm/event: Core.
- Transport nội bộ: BUS event.

### Công việc

- Định nghĩa canonical `Detection` với UtcTime, ObjectId, BoundingBox, Class, Confidence và các extension Vehicle/LPR/Face/Body/GeoLocation.
- Định nghĩa canonical `AlarmEvent` với Topic, PropertyOperation, Source, Key và Data.
- Đồng bộ timestamp metadata với video.
- Map `Application`/`app_id` vào Source/Data hoặc vendor namespace; không coi là field ONVIF bắt buộc.
- Cập nhật `GetSupportedMetadata`, `GetSupportedAnalyticsModules`, rules và event properties theo runtime thật.
- Cấp metadata track qua RTP/RTSP.
- Cấp event qua PullPoint; MQTT chỉ advertise nếu thực sự hỗ trợ.

### Gate

- Metadata XML valid schema.
- ObjectId ổn định và bounding box đúng hệ tọa độ.
- Topic phát ra tồn tại trong `GetEventProperties`.
- Profile M tests pass với dữ liệu thật.

## 10. Phase 7 — Profile G Recording, Search và Replay

### Nguồn thật

- Recording/track/job/index/replay: DVR.
- Historical event/metadata index: DVR + Core.

### Công việc

- Thay `Recording_0`, `Job_0`, `VIDEO_0`, `META_0` mock bằng registry thật.
- Nối Recording Control.
- Nối Search token lifecycle, forward/backward, time range và track filter.
- Nối Event Search.
- Nối `GetReplayUri` tới DVR playback server.
- Hỗ trợ RTSP Range clock và timestamp replay.
- Không truyền replay payload qua onvif-server.

### Gate

- Recording/Search/Replay token nhất quán.
- VMS phát lại đúng UTC range.
- Profile G pass với archive thật.

## 11. Phase 8 — Production hardening và loại mock khỏi runtime

### Công việc

- Tắt mock capability trong production config.
- Không package mock server/fake stream vào firmware production.
- Thiết lập systemd restart/dependency/readiness.
- Metrics theo operation, latency, error và reconnect.
- Queue limit/backpressure cho metadata/event.
- Soak test nhiều VMS/client.
- Kiểm thử upgrade/rollback và contract version compatibility.

### Gate

- Không còn response giả trong production.
- Restart MGMT/DVR/VPU/Core/HAL độc lập không làm onvif-server mất khả năng phục hồi.
- Full S/T/M/G regression pass trên backend thật.

## 12. Thứ tự triển khai đề xuất

```text
Baseline
  → Backend facade/config/error mapping
  → Identity/Authentication/RBAC
  → Device/Discovery
  → Media/RTSP
  → Imaging/PTZ
  → Profile M Metadata/Event
  → Profile G Recording/Search/Replay
  → Production hardening
```

Không cần đợi toàn bộ backend hoàn thiện. Mỗi capability có thể chuyển sang `real` khi service sở hữu nó đạt acceptance gate; các capability còn lại tiếp tục chạy mock trong hybrid mode.

## 13. Quy trình cập nhật tài liệu

Sau mỗi capability migration:

1. Cập nhật trạng thái trong `02-INTEGRATION_MATRIX.md`.
2. Ghi endpoint/IPC contract và owner.
3. Ghi commit backend và onvif-module đã test.
4. Ghi DTT case/report và VMS đã kiểm tra.
5. Ghi limitation hoặc behavior chưa tương thích.
6. Không đánh dấu `REAL_VERIFIED` nếu mới chỉ compile hoặc smoke test.

Ma trận trạng thái được duy trì tại `02-INTEGRATION_MATRIX.md`.
