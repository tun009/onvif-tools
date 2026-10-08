# Kế hoạch tích hợp ONVIF Server với Camera-alvis

> Trạng thái: Accepted / Living document  
> Cập nhật gần nhất: 2026-10-07 (Phase 7 — kế hoạch Profile G; Phase 6 vẫn tạm hoãn)  
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
- **Vấn đề riêng cho `GetSnapshotUri` — ĐÃ RESOLVE (2026-09-22), không cần
  hướng workaround nữa**: từng lo ngại nếu production bật lại
  `DVR_TOKEN_CHECK=1`, VMS gọi thẳng `Uri` trả về sẽ không biết đính token,
  bị 401 (3 hướng workaround (a)/(b)/(c) từng cân nhắc: thêm public path,
  nhận token qua query string, hoặc onvif-module tự proxy ảnh). **Đã confirm
  trực tiếp với team DVR: cơ chế Token này là logic cũ, chắc chắn sẽ bị bỏ
  đi** — nên không cần thiết kế workaround, giữ nguyên `GetSnapshotUri` xây
  URI gọi thẳng DVR như hiện tại.
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

#### Port ONVIF/RTSP động từ MGMT + fix RTSP-over-HTTP tunnel — DONE, chờ DVR bật Digest (2026-09-22)

- **Đã code + build + verify trên `.125` (DTT r10.xml)**:
  - `main.cpp`: sau smoke test, nếu `backendMode != Mock` thì gọi
    `mgmtClient->getNetworkProtocols()` (SQLite `mgmt_network_config.db`,
    không phải đọc file config) để ghi đè `cfg.httpPort`/`cfg.rtspPort` đọc
    tĩnh từ `onvif.conf` — map theo tên entry `"ONVIF"` (port SOAP web
    service) và `"RTSP"` (port stream DVR thật). Lỗi/thiếu entry → giữ
    nguyên giá trị `onvif.conf` (fallback chủ đích, không phải bug). Mock
    giữ nguyên cfg tĩnh.
  - `ServiceConfig` (`DeviceService.h`) thêm `useMockRtspRelay` (true chỉ
    khi capability `media` = Mock) — `Media2Service::GetStreamUri` chỉ
    route qua relay Digest giả `RTSP_RELAY_PORT=8555` khi cờ này bật; với
    DVR thật thì trả thẳng URI backend cung cấp (MediaMTX tự lo Digest).
  - `RtspOverHttp`/`RtspOverHttps`: port đích tunnel đổi từ hằng cứng sang
    `cfg_.httpPort` (giá trị đã lấy động ở trên) — tự động đúng port web
    service dù MGMT trả số nào.
  - `OnvifServer::proxyRtspHttpTunnel` nhận `relayPort` làm tham số thay vì
    hardcode; điều kiện nhận diện tunnel ở `listenLoop()` bỏ hẳn check
    `streamPath` theo path mock (`/main`, `/sub1`,...) — chỉ còn dựa vào
    header `x-rtsp-tunnelled`, vì path DVR thật (`/live/ch100`,...) không
    khớp whitelist cũ nên trước đây tunnel không bao giờ kích hoạt được cho
    stream thật.
  - Verify build: `192.168.8.36` không dùng được (nhánh git ở đó cũ, lịch sử
    Profile-G, không liên quan `onvif-v4.0.0`) → build-check thật thực hiện
    trên chính `.125` (copy tạm `/tmp/build_check_local`, không đụng repo
    thật) — build sạch, không lỗi/warning.
  - Verify DTT (r10.xml, full run trên `.125`): log khởi động in đúng
    `[main] Port from MGMT: ONVIF=8000, RTSP=554`; `MEDIA2_RTSS-1-1-2` STEP
    12 ("same port with web service") và STEP 13 (same scheme) chuyển từ
    FAIL → PASS; STEP 14 (Describe qua tunnel) nhận SDP thật từ MediaMTX
    (không còn liên quan port/tunnel, chỉ còn fail vì Digest — xem dưới);
    `MEDIA2_RTSS-1-1-1`/`1-1-3` (RTSP thường) vẫn trả đúng
    `rtsp://192.168.8.125:554/live/ch100`, không bị route nhầm qua relay
    mock 8555 — không hồi quy. Toàn bộ Media2 SOAP layer trước đó
    (MEDIA2-1-1-4, 2-2-4, 2-2-7, 2-3-3, 5-1-1) vẫn PASS.
- **Còn lại — ngoài phạm vi onvif-module, đã bàn giao DVR/MGMT team**:
  `MEDIA2_RTSS-1-1-1/1-1-2/1-1-3` vẫn FAIL vì MediaMTX (RTSP server thật
  của DVR) không bắt buộc Digest — DESCRIBE trả thẳng `200 OK` thay vì `401
  + WWW-Authenticate: Digest` (bắt buộc với Profile T/M). Đã gửi tài liệu
  chi tiết cho DVR team: `docs/Camera-alvis/rtsp-digest-and-http-tunnel-issue.md`
  (nêu rõ cần bật `rtspAuthMethods: [digest]` trên MediaMTX + 1 endpoint
  xác thực mới ở MGMT dựa trên `UserService::verifyOnvifHttpDigest()` có
  sẵn). **Trạng thái (2026-09-22): đã báo DVR team, họ đang làm, sẽ báo lại
  khi xong** — không còn việc gì phía onvif-module có thể làm tiếp cho tới
  lúc đó. Khi có báo lại: chạy lại DTT xác nhận 3 case trên để đóng nốt
  Media2/Profile T streaming.

#### Quyết định: đưa Media1 (Profile S, `MediaLegacyHandler`) vào backend thật ngay trong Phase 4 (2026-09-22)

- **Trước đó**: Media1 hoàn toàn mock — comment đầu `MediaLegacyHandler.cpp`
  ghi rõ *"Không backend real"*, toàn bộ state (4 fixed profile
  `profile_main/sub1/sub2/jpeg`, VSC/VEC) sống trong biến static ở file,
  không hề gọi `ICameraBackend`/`AlvisBackendFacade`/DVR. `MediaLegacyService`
  (adapter đăng ký vào `ServiceRegistry`, `pathPrefix "/onvif/media"`) cũng
  được tạo không kèm backend (`std::make_unique<MediaLegacyService>()`,
  `OnvifServer.cpp:259`) — khác hẳn `Media2Service`/`ImagingService`/
  `DeviceService` đều nhận `backend_` qua constructor.
- **Quyết định người dùng (2026-09-22)**: làm Media1 thật ngay trong Phase 4
  này, không để lại đợt sau.
- **Rủi ro/khoảng trống kiến trúc đã phát hiện khi nghiên cứu, cần chốt
  hướng trước khi code**:
  1. **Mô hình profile khác nhau giữa mock và DVR thật**: mock Media1 mô
     phỏng 1 nguồn vật lý (`src_main`) → 3 tier độ phân giải (main 4K/sub1
     720p/sub2 480p) + 1 profile JPEG riêng. DVR thật (`HttpDvrClient::
     getProfiles()`, đã verify trên `.125`) trả token dạng kênh vật lý
     (`"0"`, `"0_sub"`, `"1"`, `"1_sub"` — 2 kênh, mỗi kênh main+sub, tổng
     4 profile) — cấu trúc khác hẳn, không phải 3 tier cùng 1 nguồn.
     `Media2Service` ở nhánh backend thật đã dùng thẳng token DVR trả về
     (không ép về tên `profile_main`/`profile_sub1` như nhánh mock).
  2. **`ICameraBackend`/`Codec` enum KHÔNG có JPEG** (`MediaTypes.h`: `enum
     class Codec { H264, H265 }`) — toàn bộ pipeline `getProfiles()` (cả
     `HttpDvrClient` lẫn `AlvisBackendFacade`) không có chỗ cho encoding
     JPEG. Mock Media1 hiện có hẳn 1 profile JPEG riêng phục vụ bộ test
     Profile S JPEG (RTSS-1-1-31..36/45/53, MEDIA-2-1-9 negative). Nếu
     route Media1 qua `ICameraBackend` như hiện trạng, profile JPEG sẽ
     không có nguồn dữ liệu thật tương ứng.
  3. `MediaLegacyHandler` hiện 100% `static` (hàm + state toàn cục), không
     có constructor nhận `backend_`/`cfg_` — cần refactor sang instance
     (giống pattern `Media2Service`) hoặc truyền `backend_` qua tham số
     tĩnh, đồng thời sửa `MediaLegacyService` để nhận và truyền `backend_`
     xuống khi `registry_.registerService(...)` ở `OnvifServer.cpp:259`.
- **Đã chốt hướng (2026-09-22)**:
  1. **Token**: dùng thẳng token DVR trả về (giống Media2 ở nhánh backend
     thật) — không map ngược về tên cũ `profile_main/sub1/sub2`. `GetProfiles`
     Media1 sẽ trả đúng các profile thật DVR có (2 kênh × main/sub).
  2. **JPEG**: bỏ hẳn `profile_jpeg` — verify trực tiếp source DVR
     (`dvr_controller.cpp:57`: *"MJPEG web streaming has been removed in
     favor of H264/WebRTC via MediaMTX"*) xác nhận DVR mới không còn hỗ trợ
     MJPEG streaming liên tục (chỉ còn `mjpeg_codec` dùng cho
     `GetSnapshot` — ảnh tĩnh, không phải stream). Đây là giới hạn thật của
     DVR, không phải thiếu sót `ICameraBackend`. Theo nguyên tắc ưu tiên
     thay full bằng camera thật, chấp nhận các test JPEG của Profile S
     (RTSS-1-1-31..36/45/53, MEDIA-2-1-9 nhánh JPEG) chuyển từ PASS sang
     FAIL/không áp dụng ở lần chạy đầu.
     > **ĐÍNH CHÍNH (2026-09-24)**: quyết định "chấp nhận FAIL" ở trên dựa
     > trên giả định JPEG chỉ là optional — **sai**, đã tra lại đúng
     > `ONVIF Profile S Specification v1.3` (không phải Profile T) và xác
     > nhận MJPEG streaming là **Device MANDATORY** thật của Profile S. Đây
     > là 1 regression thật, không phải đánh đổi vô hại. Đã yêu cầu DVR
     > team bổ sung lại và fix xong hoàn toàn — xem mục "MJPEG streaming —
     > RESOLVED" phía dưới, `r17.xml` xác nhận PASS toàn bộ.
- **Đã code xong, build sạch trên `.125`** (`/tmp/build_check_media1`,
  `EXIT_CODE=0`, không đè repo thật) — `MediaLegacyHandler` giờ đọc
  `backend_->getProfiles()/getStreamUri()/getSnapshotUri()` thật thay vì
  state tĩnh; thêm `MediaLegacyHandler::setBackend()`, gọi cùng chỗ với
  `setEndpoint()` trong `OnvifServer::listenLoop()`. Người dùng đã pull +
  build + restart trên `.125` thật (build 2026-09-22 10:53, PID mới, log xác
  nhận cấu hình đúng).

#### RTSP Digest authentication (Profile T/M) — RESOLVED, DTT xác nhận PASS (r11.xml, 2026-09-22)

- **Trạng thái cũ**: `MEDIA2_RTSS-1-1-1/1-1-2/1-1-3` fail vì RTSP DESCRIBE
  trả `200 OK` thẳng, không yêu cầu Digest — đã bàn giao DVR/MGMT team qua
  `docs/Camera-alvis/rtsp-digest-and-http-tunnel-issue.md` (đề xuất
  MediaMTX + `authHTTPAddress` callback về MGMT).
- **Cách DVR team thực tế đã làm — KHÁC với đề xuất trong tài liệu bàn giao**:
  đối chiếu trực tiếp source DVR mới nhất (`AlvisOS/DVR`):
  - RTSP server thật của DVR là **RTSP server GStreamer tự viết**
    (`src/stream_server/rtsp_server.cpp`), không phải MediaMTX đứng sau
    Digest như đã giả định trước đó — response header `Server: GStreamer
    RTSP server` (thấy rõ trong `r11.xml`) xác nhận điều này, không phải
    `Server: mediamtx`.
  - DVR bật thẳng `gst_rtsp_auth_set_supported_methods(auth_,
    GST_RTSP_AUTH_DIGEST)` (realm cố định `"onvif"`, khớp đúng realm
    `UserService` bên MGMT dùng — có comment ghi rõ 2 bên phải khớp).
  - Tài khoản nạp trực tiếp bằng `OnvifUserStore` (file mới:
    `include/database/onvif_user_store.h` /
    `src/database/onvif_user_store.cpp`) — **đọc thẳng file SQLite
    `/media/database/mgmt.db` của MGMT (read-only)**, tự giải mã password
    (AES-256-GCM, cùng key lưu trong DB) rồi gọi
    `gst_rtsp_auth_add_digest(auth_, username, password, ...)` cho từng
    account — **không hề có endpoint HTTP callback nào cả**, không cần MGMT
    lộ thêm API gì, không đúng như phương án `authHTTPAddress` đã đề xuất
    trong tài liệu bàn giao trước đó. DVR tự đọc thẳng DB MGMT vì 2 service
    chạy chung 1 thiết bị, cùng quyền truy cập filesystem.
  - Có thêm 1 nguồn nạp tài khoản qua biến môi trường (dòng
    `user:pass`), dùng cho fallback/dev, không phải đường chính.
- **Verify bằng DTT thật (`r11.xml`, chạy 2026-09-22)**: `MEDIA2_RTSS-1-1-1`
  (RTP-Unicast/UDP), `MEDIA2_RTSS-1-1-2` (RTP-Unicast/RTSP/HTTP/TCP — cả
  bước "same port/scheme with web service" lẫn tunnel Describe qua port
  8000), `MEDIA2_RTSS-1-1-3` (RTP/RTSP/TCP) — **cả 3 đều `TEST PASSED`**,
  request RTSP DESCRIBE/SETUP/PLAY/TEARDOWN đều mang đúng
  `Authorization: Digest username="admin", realm="onvif", ...` và server trả
  `200 OK` hợp lệ (không còn `200 OK` trần trụi không auth như trước).
- **Kết luận**: đây là hạng mục cuối cùng còn treo của Media2/Profile T
  streaming trong Đợt 1 — **giờ đã đóng hoàn toàn**, không còn việc gì phía
  onvif-module hay MGMT cần làm thêm cho mục này. Tài liệu
  `docs/Camera-alvis/rtsp-digest-and-http-tunnel-issue.md` coi như đã lỗi
  thời (đề xuất kỹ thuật trong đó không phải cách được chọn) — có thể đánh
  dấu resolved/archived khi cần dọn dẹp docs.

#### MJPEG streaming — RESOLVED, Profile S Device MANDATORY đã đóng hoàn toàn (r17.xml, 2026-09-24)

**Đính chính quyết định trước đó (2026-09-22, mục "JPEG" ở phần "Đã chốt
hướng" phía trên)**: lúc đó ghi "chấp nhận các test JPEG chuyển từ PASS sang
FAIL" dựa trên đọc `ONVIF_Profile_T_Specification_v1-0.md` (chỉ yêu cầu ≥1
trong H264/H265, không nhắc JPEG) — **kết luận đó SAI khi áp dụng cho Media1/
Profile S**. Tra trực tiếp bản PDF gốc `ONVIF Profile S Specification v1.3`
tải từ chính `onvif.org`
(https://www.onvif.org/wp-content/uploads/2019/12/ONVIF_Profile_-S_Specification_v1-3.pdf),
mục 7.9 "Video Streaming – MJPEG":

```
Video Streaming – MJPEG          Device MANDATORY
MJPEG Media streaming using RTSP  | Streaming | M
Device shall declare MJPEG Option in VideoEncoderConfigurationOptions.
Device shall be able to stream MJPEG according to the Streaming Specification.
```

So với H.264 trong CHÍNH tài liệu Profile S đó (mục 8.2): H.264 chỉ là
`Device CONDITIONAL` ("if supported"). Tức là Profile S coi MJPEG là baseline
bắt buộc, H.264 chỉ là bổ sung — ngược thiết kế trực giác hiện đại. Việc bỏ
MJPEG trước đó là **regression thật với 1 yêu cầu Mandatory chính thức**, đã
được vá lại đầy đủ, không phải "chấp nhận fail vì optional" như ghi nhầm
trước đây.

**Phía DVR đã làm** (commit `AlvisOS/DVR` `2c74b09 Add MJPEG RTSP stream for
ONVIF`, 2026-09-24): thêm 1 stream role `mjpeg` mới cho MỖI kênh (không đụng
4 tier `main/sub/third/fourth` cũ), mã hoá bằng HAL JPEG hardware (NVJPG trên
Jetson, `#ifndef PLATFORM_AMBARELLA` — không bật trên CV25 vì HAL không có
JPEG sink riêng). Kích thước cố định 640×360, FPS 1-20 (mặc định ban đầu 5,
sau nâng lên 15 — xem bên dưới). Mount RTSP tại offset cố định
`channel_id + 200` (`ch200`/`ch201`), pipeline
`appsrc ! jpegparse ! rtpjpegpay pt=26` (payload type tĩnh theo RFC 2435).
`GetProfiles` trả thêm 2 profile mới `0_mjpeg`/`1_mjpeg`, `Encoding: "JPEG"`.
WHEP (web live view) bị bỏ qua cho profile này vì WebRTC không tải được JPEG.
`SetVideoEncoderConfiguration` phía DVR khoá cứng: không cho đổi qua lại giữa
profile MJPEG và H264/H265.

**Phía onvif-module đã sửa** (không chỉ thêm mới — phát hiện thêm 2 bug tiềm
ẩn khi rà soát trước khi sửa):
- `include/interface/types/MediaTypes.h` (2 bản, sync `onvif-module` +
  `mock-camera-backend`): thêm `Codec::JPEG`.
- `src/backend/HttpDvrClient.cpp::parseCodec`: nhận diện chuỗi `"JPEG"` từ
  response DVR thật.
- `src/services/MediaLegacyHandler.cpp`:
  - **Bug tìm thấy**: `resolveVecBaseline` map MỌI codec khác `H265` thành
    `"H264"` — nếu không fix, dù DVR đã có JPEG thật, Media1 vẫn báo sai
    `Encoding=H264` cho profile MJPEG. Đã sửa thêm nhánh JPEG.
  - `GetVideoEncoderConfigurationOptions`: thêm nhánh `<tt:JPEG>` (đọc động
    resolution/FPS range từ backend) thay vì luôn `<tt:H264>`.
  - `SetVideoEncoderConfiguration`: chấp nhận `Encoding=JPEG` nhưng khoá
    chéo đúng ràng buộc DVR thật (không cho đổi 1 config đang là JPEG sang
    H264 và ngược lại — suy profile token thật từ config token để biết
    baseline codec).
- `src/services/Media2Service.cpp`:
  - **Bug tìm thấy #1**: 1 ternary encoding chỉ phân biệt H264/H265, bỏ sót
    JPEG (nhánh dynamic-profile fallback).
  - **Bug tìm thấy #2**: 1 chỗ hardcode tuyệt đối `enc->Encoding = "H264"`
    trong nhánh profile thật (static branch của `GetVideoEncoderConfigurations`)
    — sẽ luôn báo sai cho bất kỳ profile JPEG nào backend trả về.
  - Viết lại `createOption()` trong `GetVideoEncoderConfigurationOptions`:
    tự suy codec thật từ backend thay vì luôn hardcode `"H264"`; bỏ
    `ProfilesSupported`/`GovLengthRange` cho JPEG (không áp dụng, chỉ có ý
    nghĩa với H264/H265); `FrameRatesSupported` JPEG dùng range riêng
    (1-20fps) khác H264.
  - `SetVideoEncoderConfiguration`: cùng logic khoá chéo JPEG↔H264 như Media1.
  - **Chủ động KHÔNG đổi** `GetGuaranteedNumberOfVideoEncoderInstances` (dù
    có thể thêm `<tt:JPEG>1</tt:JPEG>` cho đúng thực tế DVR chạy cả 2 đồng
    thời) — rủi ro phá `RTSS-1-1-27..30` đang pass mà không verify được lúc
    đó; để nguyên `TotalNumber=1/H264=1`.
- Build-check thật trên `.125` (`/tmp/build_check_jpeg`, xoá sau khi xong):
  link sạch, `[LINK]`/`[DONE]`, không lỗi.

**Bài học vận hành phát sinh trong lúc làm** (không phải code, nhưng ảnh
hưởng trực tiếp evidence):
1. DVR encoder mặc định `kMjpegDefaultFps=5` — quá thấp so với ngưỡng DTT
   tính động (`frames_cần ≈ FrameRateLimit_đã_Set × 5s ÷ 2`; onvif-module
   chấp nhận Set tối đa 20fps cho JPEG nên DTT luôn yêu cầu ~50 frame/5s ≈
   10fps tối thiểu). `SetVideoEncoderConfiguration` của ONVIF hiện chỉ lưu
   SOAP state (không forward xuống DVR thật — compromise đã có sẵn từ trước
   cho H264 để tránh gián đoạn stream), nên DVR luôn chạy đúng fps mặc định
   bất kể client Set gì.
2. Lần đầu nhờ DVR nâng fps default lên 15, **không có hiệu lực** — vì
   profile `0_mjpeg`/`1_mjpeg` đã được tạo trong DB từ lần test trước đó
   ("Anything previously stored in the DB for this profile token wins" —
   đọc trực tiếp code); đổi hằng số nguồn chỉ áp dụng cho profile MỚI tạo
   lần đầu, không áp dụng lại lên hàng đã có sẵn trong DB. DVR team sau đó
   cập nhật đúng giá trị lưu trong DB (không phải chỉ đổi constant) → có
   hiệu lực thật.
3. DTT (`r16.xml`) xác nhận nguyên nhân bằng số liệu chính xác: đo được
   ~5.1fps thật dù đã "nâng" — khớp hoàn toàn giả thuyết DB cũ. Sau khi DVR
   sửa đúng dòng DB (`r17.xml`): đo được 15-16fps thật, đủ margin qua ngưỡng.

**Bằng chứng DTT — tiến trình từng bước** (tất cả file trong
`docs/dtt-result/`):
- `r12.xml`/`r13.xml` (22/09, trước khi có MJPEG): 8-9 case JPEG fail đúng
  dự kiến ("Profile with JPEG Video encoder configuration not found").
- `r14.xml` (24/09, ngay sau khi DVR thêm MJPEG + onvif-module vá xong):
  11 fail — JPEG chuyển sang fail kiểu mới ("Frames waiting timeout", đã
  tìm thấy profile) + 2 case H264 fail thoáng qua (cold-start, tự hết).
- `r15.xml`: 10 fail — xác nhận DVR MJPEG thật chạy ổn định ~5.1fps
  ("Only 26 frames captured (5.1 FPS)"), không đủ ngưỡng 50 frame/5s.
- `r16.xml`: 8 fail — H264 cold-start đã tự hết (`RTSS-1-1-43/44` pass lại);
  JPEG vẫn 5.1fps y hệt (fix DB chưa có hiệu lực, xem bài học #2 trên).
- `r17.xml`: **24/25 PASS** — toàn bộ 8 case JPEG
  (`RTSS-1-1-31/32/33/34/35/36/45`, `MEDIA-2-1-9`) pass với margin an toàn
  (15-16 FPS thật, DVR đã cập nhật đúng DB). **Chỉ còn 1 fail:
  `RTSS-1-1-48`, không liên quan JPEG** (xem mục riêng bên dưới).

**Kết luận**: yêu cầu Device MANDATORY của Profile S (MJPEG streaming) đã
đóng hoàn toàn, có bằng chứng DTT thật `r17.xml`. Không còn việc gì cần làm
thêm cho hạng mục này trừ khi có regression mới.

#### `RTSS-1-1-48` — OPEN, root cause đã xác định, chưa xử lý (2026-09-24)

Không liên quan JPEG/MJPEG — case này về `SetVideoEncoderConfiguration`/
`GetStreamUri` cho **H.264 profile `1_sub`** (sub-stream kênh 1, không phải
main). Fail tái lập ổn định qua nhiều lần chạy (`r12` → `r17`):
`GetStreamUri(1_sub)` trả `rtsp://192.168.8.125:554/live/ch1`, nhưng
`DESCRIBE` vào path đó luôn `404 Not Found`.

**Root cause xác định chắc chắn** (đọc trực tiếp log khởi động DVR thật,
`journalctl -u dvr_new`):

```
[DVR_CONTROLLER] [DVR_CTRL-DB] Loaded sub profile from DB | channel: 1 | token: 1_sub | enabled: false
[DVR_CONTROLLER] [DVR_CTRL-STREAM-OFF] Stream disabled, encoder not started | channel: 1 | stream: 1 (sub)
```

So với kênh 0 cùng vai trò (`0_sub`): `enabled: true`, encoder khởi động
bình thường, mount RTSP hoạt động. **Đây thuần là 1 dòng dữ liệu trong
database của DVR bị đánh dấu `enabled=false`** cho riêng `1_sub` — không
phải bug code, không phải race condition ở tầng RTSP mount. DVR đọc đúng cờ
này và chủ động không khởi động encoder (đúng thiết kế), nên không có gì để
mount → `ch1` luôn 404 bất kể restart bao nhiêu lần vì DB luôn nạp lại đúng
giá trị cũ.

**Cách sửa đã xác định, chưa thực hiện** (theo yêu cầu người dùng, chưa cần
làm gấp): DVR đã có sẵn REST endpoint (`SetVideoEncoderConfiguration` phía
DVR, `routes_live_profiles.cpp`) nhận field `"Enabled": true/false`, tự lưu
qua `context_->setProfileEnabled()` + `db->saveProfileEnabled()` — chỉ cần
1 lệnh gọi API bật lại `enabled=true` cho token `1_sub` (khớp `0_sub`),
**không cần DVR sửa code gì cả**. Chưa rõ vì sao cờ này bị tắt (có thể do 1
lần gọi API/migration nào đó trước đây vô tình đổi) — nếu cần điều tra tiếp
thì nên hỏi DVR team đã từng chạy script/API nào đụng tới `1_sub` gần đây.

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

- **Imaging: đi qua MGMT** (`ImagingSettingsApiController`), MGMT tự gọi
  xuống HAL/ISP bên trong — `onvif-module` KHÔNG tự nói chuyện với HAL.
- **Zoom (PTZ): cũng đi qua MGMT** (`LensApiController`, `/Config/ZoomFocus`),
  KHÔNG phải "BUS control IPC + HAL" trực tiếp như ghi ban đầu — đã đính
  chính sau khi đọc thật (xem mục nghiên cứu bên dưới). MGMT đã có sẵn cầu
  nối `IHalBridge`/`DirectHalBridge` xuống `libhal.so`.
- **Pan/tilt cơ khí: KHÔNG làm** — xem quyết định + bằng chứng ở mục
  "Nghiên cứu Phase 5" bên dưới. Đường thật (nếu có) là thẳng HAL qua BUS
  control IPC, không qua MGMT — nhưng chưa dùng tới vì bỏ hẳn pan/tilt.

### Công việc

- Map ImagingSettings và ImagingOptions đúng field MGMT thật trả về (không
  phải "range/default/step của HAL" — MGMT là lớp trung gian duy nhất
  `onvif-module` được phép gọi).
- Chỉ advertise control thực sự hỗ trợ (Zoom có, Pan/Tilt không).
- Tách Zoom (PTZ service, node chỉ có trục Zoom) khỏi Focus (Imaging
  service, đã có sẵn khung `Move/Stop/GetStatus`) — 2 service ONVIF khác
  nhau, không phải cùng 1 chỗ.
- Sửa bug `isValidToken()` hardcode token mock cũ trong `ImagingService.cpp`
  trước khi làm gì khác (xem chi tiết bên dưới).
- Dựng mới hoàn toàn `PtzService.cpp` (hiện chỉ có header rỗng, chưa có
  file .cpp, chưa đăng ký service, `DeviceService` đang chủ động trả fault
  PTZ) — không phải "chỉnh sửa", mà là code từ đầu.
- Kiểm tra behavior khi HAL/MGMT unavailable (map lỗi sang SOAP Fault đúng
  chuẩn, không fallback mock âm thầm — nguyên tắc #6 README).

### Gate

- [x] Get/Set imaging dùng state thật qua MGMT (`imaging = real` đã bật,
      DTT xác nhận — 2026-09-28).
- [x] PTZ Zoom command đến MGMT/HAL thật, xác nhận qua log MGMT
      `LensService HAL-DIRECT-OK set_zoom` trên `.194` (không advertise
      Pan/Tilt) — 2026-09-28.
- [x] Imaging test không regression — toàn bộ `IMAGING-*` PASS ở r20-r25,
      bao gồm `IMAGING-1-1-14` persistence check.
- [x] `DEVICE-1-1-6` (PTZ Capabilities) PASS lại với dữ liệu PTZ thật
      (không còn dựa vào fault như trước Phase 5) — xác nhận bằng DTT, xem
      "Triển khai Phase 5" bên dưới.
- [ ] `ContinuousMove`/`Home Position` — biết là sẽ luôn fail DTT ở trạng
      thái hiện tại (giới hạn cố ý, xem chi tiết bên dưới), **user quyết
      định tạm hoãn** (2026-09-28), không phải điều kiện chặn Gate.

### Nghiên cứu Phase 5 (2026-09-25) — đã xong, CHƯA CODE, ghi lại để đối chiếu sau khi làm

#### Xác nhận phạm vi theo đúng nguyên tắc #2.1 — tra cả 4 profile (S/T/M/G), không chỉ 1

- **Profile S** (`ONVIF Profile S Specification v1.3`, tải từ onvif.org):
  mục 8.3 "PTZ (if supported)" → `Device CONDITIONAL`, tường minh.
- **Profile T** (`ONVIF_Profile_T_Specification_v1-0.md` có sẵn local):
  toàn bộ mục 7.21 Absolute PTZ Move, 7.22 Continuous PTZ Move, 8.1-8.4 PTZ
  Configuration/Presets/Home Position — **tất cả** đều `Device CONDITIONAL`
  kèm "(if supported)"; `GetServiceCapabilities|PTZ|C`. Spec còn ghi rõ:
  *"Some devices only support Pan/Tilt and not Zoom (or vice versa)...
  device zoom operations are listed as Conditional"* — xác nhận 1 PTZ node
  chỉ có trục Zoom là hợp lệ theo chuẩn.
- **Profile M** (`onvif-profile-m-specification-v1-1.pdf`, tải trực tiếp từ
  onvif.org — bản spec thật, không phải "Client Test Specification" đang
  có sẵn local): grep toàn văn 49 trang, **0 kết quả "PTZ"** — không
  mandatory, không conditional, không liên quan.
- **Profile G** (`ONVIF_Profile_G_Specification_v1-0.pdf`): đúng 1 lần
  nhắc "PTZ", nằm trong định nghĩa thuật ngữ "Metadata" (ví dụ nội dung
  metadata), không phải yêu cầu chức năng.
- **Kết luận**: không profile nào trong 4 profile dự án target bắt buộc
  PTZ. Quyết định: **bỏ hẳn pan/tilt cơ khí**, chỉ làm Zoom (qua PTZ node
  chỉ-Zoom) + Focus (qua Imaging service) + Imaging thường.

#### Xác nhận phần cứng thật — không chỉ dựa vào "không bắt buộc"

Đọc trực tiếp board profile đang chạy thật trên `.125`
(`/opt/dvr_apps/board_profiles/active-profile.yaml` →
`deployments/xavier-nx-hwv-model-125lab.yaml` →
`boards/tomotech-ai-camera-125lab.yaml`, mục `peripherals`):

```yaml
lens_context:   # kênh 0 (main/context)
  board_location: sub_board_cv25
  driver: lens_cv25_tmc2300
  lens_spec_file: FOCTEK_CS-P1150IR_8MP.json

lens_alpr:      # kênh 1 (ALPR)
  board_location: main_carrier_board
  interface: i2c
  driver: lens_pca9635
  zoom_pin: [4, 5, 7, 6]
  focus_pin: [0, 1, 3, 2]
  iris_pin: [4, 5, 7, 6]
  lens_spec_file: FOCTEK_CS-P1150IR_8MP.json
```

**Có motor zoom/focus/iris thật** (chân GPIO thật qua PWM PCA9635, ống
kính varifocal thương mại FOCTEK CS-P1150IR 8MP) cho **cả 2 kênh**.
**Không có mục nào cho pan/tilt** trong toàn bộ `peripherals` (so với các
mục khác đều liệt kê đủ: GPS, radar, laser, IR-cut, IR-LED...) — xác nhận
độc lập, không chỉ dựa vào phía HAL source code.

Đối chiếu thêm bên `HAL` source (`D:\Elcom\Ovif-mock\AlvisOS\HAL`): driver
Pelco-D UART pan/tilt (`src/common/drivers/ptz_pelco_d_uart.cpp`) viết đầy
đủ giao thức thật, nhưng chỗ gọi vào nó
(`src/ambarella/ExternalPeripheralHAL_amba.cpp`, nhánh `"ptz_control"`)
còn nguyên `return false; // TODO` — chưa nối dispatch. Quét toàn bộ
`board_profiles/**/*.yaml`: không profile nào khai báo thiết bị UART PTZ.
→ Dù có bắt buộc theo spec, pan/tilt **cũng chưa dùng được** trên bất kỳ
hardware nào đang deploy — 2 lý do độc lập cùng dẫn tới 1 kết luận.

#### API thật phía MGMT — đã đọc 2 tài liệu handoff nội bộ của MGMT

`D:\Elcom\NewVersion\frontend\MGMT\src\backend\docs\handoff_imaging_settings.md`
và `handoff_lens_api_mapping.md` (viết bởi team MGMT, đối chiếu trực tiếp
source DVR cũ + MGMT-BE hiện tại, rất chi tiết) — không cần suy diễn từ
code, tài liệu đã có sẵn.

**Imaging** — `ImagingSettingsApiController`:
```
GET/PUT /mgmt/v1/Config/ImagingSettings   (VideoSourceToken, ImagingSettingId optional)
```
Trả mảng `[{Type:"DAY",...},{Type:"NIGHT",...}]`, mỗi phần tử field:
`Brightness/ColorSaturation/Contrast/Sharpness`, `Exposure{Mode,...}`,
`WhiteBalance{...}`, `DayNight{IrCutFilterMode,...}`, `WDR/BLC/HLC/DNR`,
`IspAdvance{...}`. Set là partial-update (mọi field optional).

**Zoom/Focus** — `LensApiController`:
```
GET  /mgmt/v1/Config/ZoomFocus?VideoSourceToken=0|1
PUT  /mgmt/v1/Config/ZoomFocus
     body: {"VideoSourceId":"0"|"1","ZoomFocusCamera":{"FocusMode":int,"FocusValue":double,"ZoomValue":double}}
GET  /mgmt/v1/Config/LensInfo   → MaxZoom/MinZoom/StepZoom, MaxFocus/MinFocus/Stepfocus
```
`FocusMode`: 0=AUTO_FOCUS (lái theo `ZoomValue`), 1=MANUAL_FOCUS (lái theo
`FocusValue`), 2=RUN_AF (chạy autofocus), 3=ZF_SYNC (mới, lái zoom kèm
focus tự theo đường cong lens). `ZMActive`/`FMActive` = motor đang bận
(map sang `MoveStatus` ONVIF). **`RUN_AF` chỉ chạy thật ở kênh context (0)**
— driver `LensCv25I2cBridge` có implement `autoFocus()`, driver PCA9635
của kênh ALPR (1) kế thừa `return false` từ base class, MGMT tự fallback
về AUTO_FOCUS khi bị từ chối. Cần phản ánh đúng khác biệt này trong
Options/response, không quảng bá autofocus như nhau cho cả 2 kênh.

#### Kiến trúc code — theo đúng pattern Device/Network đã có, không phát minh mới

`include/backend/IMgmtClient.h` đã có khuôn mẫu rõ ràng
(`getHostname()/setHostname()`, ném `MgmtValidationError` khi input sai
vs `runtime_error` khi lỗi vận hành/kết nối) — chỉ cần thêm method cùng
kiểu cho Imaging + ZoomFocus, không cần lớp client mới. `AlvisBackendFacade.cpp`
hiện **100% hardcode Imaging/PTZ về mock** (`return mock("X").method(...)`,
không đọc `capabilities.imaging`/`capabilities.ptz` gì cả) — cần thêm
nhánh `real("imaging")`/`real("ptz")` gọi `IMgmtClient`, đúng y hệt cách
Device/Network đã làm.

**Zoom và Focus đi 2 service ONVIF khác nhau, không phải cùng chỗ:**
- **Focus** → thuộc **Imaging service** (không phải PTZ) —
  `ImagingService.cpp` đã có sẵn khung `Move/Stop/GetStatus/GetMoveOptions`
  (hiện là state mock cục bộ `g_focus` map, xem comment gốc "motorized
  lens mock" §7.16) — chỉ cần đổi nguồn dữ liệu sang gọi
  `IMgmtClient::getZoomFocus()/setZoomFocus()`, field `FocusValue`/`FocusMode`.
- **Zoom** → thuộc **PTZ service** — `PtzService.h` hiện chỉ 4 dòng
  placeholder, chưa có `.cpp`, chưa đăng ký ở `OnvifServer.cpp`
  (dispatch if/else chỉ có `/onvif/media`, `/onvif/imaging`, else
  `DeviceService`), `DeviceService.cpp` đang chủ động trả fault PTZ
  (`tt__CapabilityCategory::PTZ` → `reqUnsupported = true`, comment "Fixed
  Camera, không hỗ trợ PTZ trong Profile T"). Cần dựng mới hoàn toàn: PTZ
  node chỉ khai `ZoomSpaces/PositionGenericSpace` (AbsoluteMove) +
  `ZoomSpaces/VelocityGenericSpace` (ContinuousMove), map `ZoomValue`
  (1.0–4.0 tỉ lệ quang MGMT) sang thang normalized ONVIF.

#### Bug thật tìm thấy — độc lập với việc có làm real hay không, phải sửa trước

`ImagingService.cpp::isValidToken()`:
```cpp
return tok == "src_main" || tok == "src_sub1" || tok == "src_sub2" || tok == "video_source_token";
```
Đây là token thời mock cũ. Từ khi Media1/Media2 đã chuyển sang backend
thật, `GetVideoSources` trả token thật (`"0"`/`"1"`, theo `sourceToken`
DVR — xem `MediaLegacyHandler.cpp::handleGetVideoSources`). Nghĩa là
**ngay hiện tại**, 1 client theo đúng flow ONVIF chuẩn (`GetVideoSources`
rồi dùng token đó gọi Imaging) sẽ luôn bị từ chối "Invalid
VideoSourceToken" — token thật không khớp danh sách hardcode cũ. Phải sửa
đọc token động từ `backend_->getProfiles()` (giống các service khác đã
làm), không phụ thuộc việc có nối MGMT thật hay chưa.

#### Rủi ro cần theo dõi khi code thật

- **`IMAGING-1-1-14`** (Set→Get, kiểm tra persistence) hiện pass dễ vì
  toàn bộ là echo cache cục bộ. Tài liệu MGMT tự ghi nhận: *"Live.Sharpness
  is lossy — sensor giữ 12 nấc (-6..5) cho thang 0-100 của API, nên lưu 50
  đọc lại thành 45"* — field nào không round-trip chính xác cần cân nhắc
  giữ echo cache thay vì đọc lại giá trị MGMT trả (matching pattern SOAP
  state only đã dùng cho Media1 Set).
- Model `ImagingTypes.h` chia sẻ hiện chỉ có 6 field (brightness/contrast/
  saturation/sharpness/backlightComp/wideDynRange) — muốn Exposure/
  WhiteBalance/IrCutFilter cũng thành real (thay vì tiếp tục echo cache
  cục bộ như hiện tại) cần mở rộng struct này (và sync bản
  `mock-camera-backend`, đúng nguyên tắc CLAUDE.md #2).
- Bật PTZ (dù chỉ Zoom) sẽ đổi hành vi `GetCapabilities`/`GetServices` —
  cần verify lại `DEVICE-1-1-6` bằng DTT thật sau khi code xong, không
  giả định vẫn pass nguyên trạng.

#### Việc CHƯA làm (đúng yêu cầu — dừng ở nghiên cứu, chưa code)

> ⚠️ Mục này mô tả trạng thái **lúc nghiên cứu (2026-09-25)**, đã bị
> supersede bởi mục "Triển khai Phase 5" ngay bên dưới — Imaging + PTZ đã
> code xong, build, và `config/onvif.conf` đã chuyển `imaging = real`,
> `ptz = real` thật (không còn mock nữa). Giữ nguyên đoạn dưới đây làm
> lịch sử quyết định, không xoá.

Toàn bộ mục này là kết quả nghiên cứu, dùng để đối chiếu khi bắt tay code
thật. `config/onvif.conf` hiện `imaging = mock`, `ptz = mock` — **giữ
nguyên**, chỉ đổi sang `real` sau khi đã code xong + build + test DTT xác
nhận, đúng quy trình rollout đã áp dụng cho Device/Network/Media (không
đổi config trước khi có code tương ứng — đổi bây giờ cũng vô nghĩa vì
`AlvisBackendFacade` chưa đọc capability này cho Imaging/PTZ, chưa có
nhánh `real()` nào để kích hoạt).

### Triển khai Phase 5 (2026-09-28) — Imaging real + PTZ zoom-only, DTT r18→r25

#### Code đã viết

- **`IMgmtClient.h`/`HttpMgmtClient.h`/`HttpMgmtClient.cpp`** — 5 method
  mới theo đúng khuôn mẫu Device/Network sẵn có: `getImagingSettings()`/
  `setImagingSettings()` (`GET/PUT /mgmt/v1/Config/ImagingSettings`, lọc
  entry `Type=DAY`), `getZoomFocus()`/`setZoomFocus()` (`GET/PUT
  /mgmt/v1/Config/ZoomFocus`, tham số query `VideoSourceId` — không phải
  `VideoSourceToken` như tài liệu handoff ghi lỏng lẻo, đã đối chiếu trực
  tiếp source MGMT), `getLensBounds()` (`GET /mgmt/v1/Config/LensInfo`,
  lọc entry `Enable=true`). Ba struct mới `ZoomFocusMode`/`ZoomFocusState`/
  `LensBounds` thêm vào `IMgmtClient.h`.
- **`ImagingService.h`/`.cpp`** — `isValidToken()` hết static, đọc động từ
  `backend_->getProfiles()` (đúng bug đã ghi nhận ở mục nghiên cứu, sửa
  trước khi bật real).
- **`AlvisBackendFacade.cpp`** — thêm nhánh `real("imaging")`/`real("ptz")`
  cho `getImagingSettings`/`setImagingSettings`/`ptzAbsoluteMove`/
  `ptzStop`/`getPtzStatus` (route `IMgmtClient`, giữ nguyên `ptzRelativeMove`/
  `ptzContinuousMove`/`gotoHomePosition`/`setHomePosition` đi mock — không
  implement real cho các op này, xem lý do ở mục "Giới hạn cố ý" bên dưới).
  Zoom normalize/denormalize giữa thang ONVIF `[0,1]` và thang vật lý MGMT
  `[MinZoom,MaxZoom]` (đọc từ `getLensBounds()` mỗi lần) nằm trong facade,
  `PtzService` không cần biết physical range.
- **`PtzService.h`/`.cpp` (mới hoàn toàn)** — PTZ node **chỉ có trục Zoom**
  (không Pan/Tilt), implement thật 11/28 operation: `GetServiceCapabilities`,
  `GetNodes`, `GetNode`, `GetConfigurations`, `GetConfiguration`,
  `GetConfigurationOptions`, `SetConfiguration`, `GetCompatibleConfigurations`,
  `AbsoluteMove`, `Stop`, `GetStatus`. 17 operation còn lại (Presets, Tours,
  Home Position, ContinuousMove, RelativeMove...) trả `ter:ActionNotSupported`
  — xem "Giới hạn cố ý" bên dưới về hệ quả DTT của quyết định này.
- **`DeviceService.cpp`/`OnvifServer.cpp`/`Makefile`** — đăng ký PTZ service
  đúng pattern sẵn có (`registry_`/dispatch if-else theo path
  `/onvif/ptz`), thêm `soapPTZBindingService.cpp` vào `GEN_SRCS`.
- **`Media2Service.cpp`** — thêm hàm export `resolveDynProfileSourceToken()`
  (tra `g_dynProfiles` nội bộ file, cho `PtzService` gọi cross-file).
- **`MediaLegacyHandler.h`/`.cpp`** — thêm hàm export tương tự
  `resolveLegacyDynProfileSourceToken()` (kho `g_dynProfiles` RIÊNG của
  Media1, độc lập với Media2) + 2 handler hoàn toàn mới
  `handleAddPTZConfiguration`/`handleRemovePTZConfiguration` (op Media1
  legacy chưa từng được implement, vì trước Phase 5 chưa từng có PTZ để
  gọi tới) + field `ptzToken` trong `DynProfile` + echo `<tt:PTZConfiguration>`
  trong `profileXml()` (đúng vị trí `xsd:sequence`: sau
  `VideoEncoderConfiguration`, trước `MetadataConfiguration`).
- **`DeviceIOHandler.h`/`.cpp`/`DeviceIOService.h`** — `GetVideoSources`
  (namespace `deviceIO`) hết hardcode token mock `"src_main"`, đọc động từ
  `backend_->getProfiles()` (bug độc lập, không phải PTZ nhưng phát hiện
  qua test PTZ — xem "Bug tìm thấy trong lúc chạy DTT").

#### Bug tìm thấy trong lúc chạy DTT (r18 → r25), theo đúng thứ tự phát hiện

1. **`resolveProfileToSource()` chỉ tra `backend_->getProfiles()` (DVR
   fixed profile), không biết profile tạo động qua `CreateProfile`** — vì
   dynamic profile chỉ tồn tại trong state nội bộ `onvif-module`
   (`g_dynProfiles`), DVR không hề biết. Test PTZ luôn tạo 1 profile tạm
   rồi gắn PTZ config vào đó (`PTZ-3-1-1` và tương tự) → luôn fault
   `ter:NoProfile`. **2 kho riêng biệt** (Media1 `MediaLegacyHandler.cpp`
   và Media2 `Media2Service.cpp` không share state) → phải sửa **2 lần**
   (r19 sửa Media2 trước — pass được `MEDIA2_PTZ-*`; r20 mới phát hiện
   Media1 cũng cần y hệt — pass được `PTZ-*`).
2. **Sai thứ tự field XML `tt:PTZConfiguration`** — tự gây ra khi thêm
   persist `DefaultPTZTimeout` (đợt sửa `PTZ-2-1-9`), đặt nhầm TRƯỚC
   `DefaultAbsoluteZoomPositionSpace` trong khi `xsd:sequence` yêu cầu
   ngược lại. DTT báo lỗi schema rõ ràng ("invalid child element"), phát
   hiện ở r21, sửa xong r22.
3. **Media1 chưa từng có `AddPTZConfiguration`/`RemovePTZConfiguration`**
   — không phải bug do sửa nhầm, mà là **chưa ai viết** (op này chỉ có ý
   nghĩa khi có PTZ để gắn vào, trước Phase 5 không tồn tại). DTT gọi op
   này giữa chừng flow `PTZ-3-1-1` → `ter:ActionNotSupported` (r20-r23).
   Viết mới hoàn toàn ở r23→r24.
4. **`AbsoluteMove` không validate range zoom `[0,1]`** — code cũ dùng
   `clamp01()` âm thầm kẹp giá trị ngoài range rồi vẫn gọi backend, thay vì
   từ chối bằng fault `ter:InvalidArgVal/ter:InvalidPosition` như DTT
   (`PTZ-3-1-2`) kỳ vọng. Phát hiện + sửa r24→r25.
5. **`DeviceIOHandler::GetVideoSources` hardcode `"src_main"`** — DTT
   dùng chính response này để "đoán" 1 token KHÔNG hợp lệ cho test negative
   (`IMAGING-2-1-16`), nhưng vì token trả về là mock cũ nên DTT đoán trúng
   1 token thật (`"1"`) làm token âm tính → server chấp nhận đúng (vì `"1"`
   thật sự hợp lệ) thay vì trả fault như DTT mong đợi. Sửa cùng đợt.

#### Kết quả DTT — r18 (159 fail, chủ yếu do DVR tự restart giữa chừng khi
chạy full-suite + WS-Discovery tắt, không liên quan Phase 5) → r19/r20/r21
(dò lần lượt 5 bug ở trên) → r22 (pass gần hết) → **r25: 9/12 case trong
scope PTZ đã liệt kê PASS**, bao gồm toàn bộ core: `AbsoluteMove` chạy
**thật** xuống HAL (xác nhận qua log MGMT — `LensService HAL-DIRECT-OK
set_zoom`), validate fault message, `SetConfiguration`, generic zoom/pan-tilt
spaces, Home Position bản Media1, Imaging invalid-token.

#### Giới hạn cố ý — 3 case còn fail vĩnh viễn, đã quyết định tạm hoãn (2026-09-28)

`PTZ-3-1-4`, `MEDIA2_PTZ-1-1-3` (ContinuousMove) và `MEDIA2_PTZ-3-1-3`
(Home Position) fail với lý do **thiết kế cố ý**, không phải bug:
`AlvisBackendFacade` không implement real cho `ptzContinuousMove`/
`gotoHomePosition`/`setHomePosition` vì MGMT không có khái niệm velocity
(nguyên tắc README #5 — không quảng bá capability backend thật không làm
được). **Phát hiện quan trọng**: `TestInfo` của các case này chỉ có
`RequiredFeatures: [MediaService, PTZService]` — DTT **không** đọc
`SupportedPTZSpaces`/`HomeSupported` trong `GetNode` để quyết định skip
test, nó chỉ hỏi "PTZService có tồn tại không?" rồi coi ContinuousMove/Home
là baseline "Must" bất kể node khai gì. Nghĩa là: **ngay khi bật
PTZService (dù chỉ định làm zoom-only), 2 op này sẽ luôn fail DTT vĩnh
viễn** trừ khi implement thật (kể cả no-op hợp lệ) — đúng pattern JPEG
"Device MANDATORY" đã gặp ở Phase 4. User quyết định **tạm skip, để sau**
(2026-09-28) — không phải regression, không cần sửa gấp.

#### Bug ngoài phạm vi onvif-module — báo cáo team MGMT/HAL (2026-09-28)

Trên camera `.194`, phát hiện **MGMT bị treo đồng bộ toàn bộ endpoint
(kể cả xác thực Digest) trong nhiều giây** ngay sau khi `AbsoluteMove` ra
lệnh motor zoom di chuyển quãng xa. Bằng chứng log `journalctl -u mgmt`:
lệnh ghi `set_zoom` qua UART (subboard CV25, giao thức Pelco-D, chip
SC16IS750) hoàn tất trong ~12ms, nhưng chuỗi đọc trạng thái **ngay sau đó
im lặng hoàn toàn** — mỗi lệnh poll cách nhau đúng ~3.0s (khớp
`request_timeout_ms` phía onvif-module) thay vì <1s như chu kỳ poll bình
thường. Vì `LensApiController::handleGetZoomFocus`/`handleGetLensInfo` gọi
đồng bộ vào chuỗi poll UART này, HTTP worker bị block theo → cascade fail
sang cả những request auth không liên quan chạy đúng lúc đó (`PTZ-3-1-4`,
`PTZ-3-1-5` từng fail lây kiểu này ở r24). **Không phải bug onvif-module**
— nghi vấn: firmware subboard CV25 không phản hồi poll khi đang chấp hành
lệnh motor (nên đổi sang trả "busy" ngay thay vì im lặng), hoặc
`LensApiController` nên đọc cache `readState()` định kỳ thay vì tự poll
UART mỗi request. Cách tái hiện: `PUT /mgmt/v1/Config/ZoomFocus` với
`ZoomValue` cách xa vị trí hiện tại, gọi `GET` ngay sau đó — đo thời gian
phản hồi. Đã bàn giao chi tiết cho team MGMT/HAL, chưa có fix, không chặn
Phase 5 (chỉ ảnh hưởng độ ổn định khi test dồn dập, hành vi thật của
`AbsoluteMove` là đúng).

#### Môi trường test đã đổi 2 lần trong lúc verify (không phải vấn đề code)

`.125` (DHCP cấp lại IP `.102` sau khi DTT tự chạy test
`SET NETWORK INTERFACE CONFIGURATION` — side effect thật của `network =
real`, không phải bug) → `.102` → `.194` (clone máy hoàn toàn mới để test).
Mỗi lần đổi cần cập nhật `device_ip` trong `config/onvif.conf` (dùng để
build `XAddr` trong mọi response ONVIF — quan trọng thật, không phải giá
trị trang trí, xem thảo luận nên đổi sang tự đọc `Host:` header của
request thay vì hardcode, **chưa làm**, để dành refactor sau).

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

### Nghiên cứu Phase 6 (2026-09-30) — đã xong, CHƯA CODE, ghi lại để đối chiếu sau khi làm

#### Bối cảnh: Profile M đã pass conformance với MOCK từ trước (không liên quan phase này)

Đối chiếu `docs/11-profile-m-plan.md` + `docs/16-profile-m-t-conformance-tong-ket.md` (project cũ,
trước khi có tích hợp MGMT): **Profile M đã đạt 244/244 với mock-camera-backend** (2026-07-22) —
toàn bộ SOAP layer (AnalyticsService, Media2 metadata config, event topic) đã tồn tại và đúng
chuẩn, kể cả phần khó nhất — Metadata RTP Streaming (7.5, mandatory) — đã giải bằng gortsplib
custom RTSP server bơm XML `<tt:MetadataStream>` giả định kỳ (xem `docs/15-profile-m-t-gortsplib-handover.md`).
**Việc của Phase 6 KHÔNG phải viết lại SOAP layer từ đầu** (khác hẳn PTZ ở Phase 5, vốn chưa có
file `.cpp` nào) — mà là **thay nguồn dữ liệu fake bằng dữ liệu Core/VPU thật**, y hệt tinh thần
các phase trước.

#### Nguồn thật — đối chiếu trực tiếp bằng cách đọc code MGMT + query DB thật trên `.194`

Không có source Core/VPU checkout cục bộ (chỉ có source MGMT và DVR cũ, xem `README.md` mục 3) —
khác hẳn Phase 3/5 (MGMT source đọc được đầy đủ). Kết luận dưới đây dựa trên: đọc code MGMT thật +
query trực tiếp Postgres `dvr_db` trên `.194` (tài khoản `cameraai`, tìm được từ `strings` binary
`nse`) — không suy đoán.

**(a) MGMT SỞ HỮU config Area/App/Rule (ROI, không phải kết quả detection)** — domain
`app_manager` (`src/domains/app_manager/`), lưu trong SQLite riêng của MGMT (KHÔNG phải Postgres
`dvr_db` — 2 bảng `areas` trùng tên nhưng khác hệ hoàn toàn, xem bên dưới). Endpoint thật:
```
GET  /mgmt/v1/Config/AppsManagerInfo      — apps + areas + type_areas 1 lần gọi
POST /mgmt/v1/Config/AppsManager          — CRUD apps/areas
POST /mgmt/v1/Config/AppsManagerExport    — trigger đẩy config sang Core (xem (b))
GET  /mgmt/v1/Config/CaptureContextImage / CaptureAlprImage
WS   /ws/mgmt/v1/Config/ZoomFocusAndAreas — live overlay zoom + areas
```
`Area` (ROI vẽ trên ảnh, gắn `type_area_id`/`app_id`, path SVG, `details` JSON tự do như
`AffectObjects[].Label`) + `TypeArea` (loại ROI: `ALPR Area`, `RedLightViolation`,
`WrongDirection`...) + `App` (`ALPR`, loại `TRAFFIC`...) — bản chất chính là **Rule Configuration**
theo nghĩa ONVIF (8.6): 1 Area+App+TypeArea ≈ 1 Rule (ROI + điều kiện + loại vi phạm cần phát hiện).
→ **Đây là phần khả thi làm real NGAY, có API thật, đã implement** (đọc chi tiết:
`docs/design_ai_config_export_payload.md` của MGMT).

**(b) MGMT → Core chỉ đẩy CONFIG 1 chiều, KHÔNG nhận detection event ngược lại** — xác nhận từ
chính doc thiết kế của MGMT (`design_ai_config_export_payload.md`): `AppsManagerExport` gửi message
`{"cmd":"ai_config.export", ...}` xuống Core, Core ghi file `_draft/layout.json` cho pipeline AI
đọc, trả về `{"result":1}` — **hết**. Không có API nào ở chiều ngược lại (Core/VPU → MGMT) để
MGMT biết có event/detection gì vừa xảy ra. Tức là **MGMT không phải nguồn thật cho Detection/Event
runtime** (khớp README gốc: "Rule, alarm và event | Core + BUS event", KHÔNG phải MGMT).

**(c) Postgres `dvr_db` (DB CŨ, hệ legacy) có bảng `event_tbl` khớp gần đúng schema AIEvent** —
tìm thấy bằng cách query trực tiếp (`psql -h localhost -U cameraai -d dvr_db`, credential lấy từ
`strings /opt/nse_apps/nse`). Cột `evt_key`/`evt_type`/`roi_id`/`start_time`/`end_time`/`details`
(JSONB)/`category` khớp gần như 1:1 với schema `core_events` mô tả trong
`docs/Draft_ Cấu trúc bảng — AI Event, Rule, Alarm.pdf` (tài liệu thiết kế Core-01/02, có ghi chú
"tên cột cũ (legacy) là evt_key" — xác nhận `event_tbl` chính là bảng legacy được nhắc tới).
**Nhưng `event_tbl` đang HOÀN TOÀN RỖNG (0 dòng) trên `.194`** — tức camera test hiện tại
**không có pipeline AI nào đang chạy/ghi event thật** để onvif-module có dữ liệu mà relay, dù
schema đã sẵn sàng. `nse` (process duy nhất có kết nối tới `dvr_db`) không mở port TCP nào — nhiều
khả năng không phải HTTP server, giao tiếp qua cơ chế khác (CLI/IPC) chưa xác định được thêm nếu
không có source.

**(d) Bảng Postgres `camera_events`** (211,941 dòng, có dữu liệu thật) — nhưng là **event log vận
hành chung chung** (`topic`/`source_name`/`source_value`/`content_name`/`content_value`), phần lớn
là log khởi động service (`operations/application/onvif_server/status/start`) — **không phải
AI detection event**, không dùng được cho mục đích Phase 6.

#### Cập nhật cùng ngày (2026-09-30, sau khi có source Core/VPU thật) — đảo ngược kết luận "Blocked" ở trên

User cung cấp thêm 2 source trước đó chưa biết tồn tại cục bộ:
`D:\Elcom\Ovif-mock\AlvisOS\CORE` và `D:\Elcom\Ovif-mock\AlvisOS\HAL` (branch `hieu-dev`, chứa
plugin VPU: `human_detect`, `vehicle_detect`, `license_plate_detect`, `traffic_sign_detect`,
`pothole_detect`, `manhole_detect`, `fighting_recognition`, `ocr`). Đọc trực tiếp source (không
suy đoán) đảo ngược phần lớn kết luận "Blocked" phía trên — chỉ giữ lại đúng phần về MGMT.

**Đối chiếu lại đúng ONVIF Profile M Specification v1.0** (đọc kỹ toàn văn, không chỉ tóm tắt cũ):
Rule Configuration (mục 8.6) và 3 recognition event (Face/LicensePlate/LineCrossing, 8.13-8.15)
đều **CONDITIONAL**, không phải mandatory như bảng cũ ngụ ý. Phần **MANDATORY** thật sự liên quan
Analytics là mục 7.10 "Analytics Module configuration"
(`GetSupportedAnalyticsModules`/`GetAnalyticsModules`/`CreateAnalyticsModules`/`DeleteAnalyticsModules`
trên 1 `VideoAnalyticsConfiguration`) — khái niệm gần nhưng KHÔNG đồng nhất với "Rule".

**Đối chiếu `g13.xml`** (log DTT full chạy với mock, 2026-08-05): toàn bộ nhóm `ANALYTICS-4-1-*`
(Analytics Module Config, mục 7.10), `MEDIA2-8-1-*`/`MEDIA2-9-1-*` (Metadata/Analytics Profile
config, 7.7-7.9), `MEDIA2_RTSS-4-1-*`/`4-2-1` (Metadata Streaming, 7.5) đều **PASSED** — khớp
`16-profile-m-t-conformance-tong-ket.md` (244/244). **Không có test case nào tên `RULE`/`Recognition`/
`LineCounting`** trong toàn bộ `g13.xml` — xác nhận thiết bị chưa từng quảng bá các feature
conditional này, nên chưa có baseline behavior nào cần giữ nguyên khi làm thật.

**Đọc source Core thật** (`AlvisOS/CORE`, README + `core-service.md` + đọc trực tiếp
`ai_event_api_controller.cpp`, `ai_event_ipc_controller.cpp`, `rule_api_controller.cpp`,
`core_config.json`):

- Core là service HTTP+IPC thật (drogon), lắng nghe **port 8092** (`core_config.json`), DB
  **SQLite `core.db`** (đã migrate khỏi Postgres/SOCI — xem `handoff_sqlite_migration.md`).
- Pipeline đầy đủ, có bằng chứng code, KHÔNG phải suy đoán:
  ```
  VPU plugin (vd human_detect_plugin.cpp: Detection{label, confidence, bbox, track_id})
    -> publish lên BUS-02 (Unix socket /tmp/cam_event_bus.sock)
    -> Core AIEventIpcController::handleAiEvent()
         -> RuleEngine::matchRule()  (auto-pass nếu channel chưa có Rule nào — đã có fallback)
         -> AlarmManager::raise()    (cooldown/dedup/schedule — 1 phần theo README, không phải rỗng)
    -> raise()==true mới ghi AIEventRepository -> core.db (bảng `core_events`)
  ```
- REST contract thật, đọc trực tiếp từ `include/controllers/*.hpp` (không phải từ file thiết kế
  PDF cũ nữa):
  ```
  GET    /core/v1/AiEvents            (?channel=&start_time=&end_time=&max_items=)
  GET    /core/v1/AiEvents/{event_key}
  DELETE /core/v1/AiEvents/{event_key}
  GET    /core/v1/AiEvents/{event_key}/Download   (evidence .tar: ảnh + video + details.json)
  GET    /core/v1/TrafficMonitoring , POST .../Filter   (ALPR-only, tóm tắt biển số)
  GET    /core/v1/EventsApp , POST .../Filter           (full AIEvent, non-ALPR)
  GET/POST/PUT/DELETE /core/v1/Rules              (CRUD Rule — CORE-01)
  GET/POST/PUT/DELETE /core/v1/AlarmConfigs       (CRUD AlarmConfig — CORE-02)
  GET/POST/PUT/DELETE /core/v1/MetadataExportConfigs, /MetadataExportRecords  (FTP export, không liên quan ONVIF)
  ```
- `AIEvent` field thật (đọc từ `toJson()`): `event_key, name, channel, start_time, end_time,
  created_at, details(JSON tự do), roi_id, img_urls[], video_url, object_label, object_type,
  object_size, confidence` — map khá thẳng sang ONVIF `tt:Object/Appearance`:
  `object_label`→`Class/Type`, `confidence`→`Likelihood`, `img_urls`/`video_url`→Image
  sending (8.3), `roi_id`→liên kết Rule/ROI. `Detection.bbox` (VPU, không lưu trực tiếp ở
  `AIEvent` — chỉ có trong `details` JSON tự do nếu producer gửi kèm) → `Shape/BoundingBox`.
- `Rule` field thật (đọc từ `rule_api_controller.cpp`): `id, name, description, enabled, channel,
  plugin_id, roi_id_list[], conditions(JSON), action, priority, cooldown_ms, schedule, version,
  extensions` — gần với khái niệm ONVIF "Rule" (8.6, conditional) hơn là "Analytics Module"
  (7.10, mandatory); `plugin_id` (HumanDetection/VehicleDetect/...) mới là ứng viên hợp lý cho
  "Analytics Module" — cần thiết kế map 2 tầng (Module = catalog `plugin_id`, Rule = instance
  cấu hình cụ thể) khi thực sự code, không gộp làm một.

**Đã trả lời câu hỏi "MGMT đã thực sự handle chưa"**: **CHƯA**. Đọc lại chính source MGMT xác nhận
domain `app_manager`/`AiConfigExportService` (`design_ai_config_export_payload.md`) chỉ đẩy CONFIG
1 chiều MGMT→Core (`{"cmd":"ai_config.export"}`, Core trả `{"result":1}` rồi ghi `layout.json`) —
**không có API nào ở chiều Core→MGMT** để đọc lại AiEvents/Rules/AlarmConfigs. Grep toàn bộ
`AlvisOS/MGMT` (repo cũ hơn, 2026-08-05) và `NewVersion/frontend/MGMT` (repo hiện hành,
2026-09-30) cho `8092|core/v1|AiEvents|AlarmConfigs` — chỉ khớp đúng 1 dòng README ghi chú quy ước
đặt tên REST, không có lời gọi HTTP thật nào tới Core.

**Kết luận đã đảo ngược**: Phase 6b (Detection/Event thật) **KHÔNG còn Blocked** về mặt contract —
đã đọc được đầy đủ REST API + luồng dữ liệu thật của Core, đủ để thiết kế mapping ONVIF chính xác
mà không cần đoán. Cái còn thiếu duy nhất là **runtime evidence** (build/test thật trên 1 camera
đang chạy đúng bản Core này) — camera `.194`/`.102` hiện tại chạy firmware CŨ hơn (`nse` +
Postgres `dvr_db`, không phải `core.db`/port 8092), nên chưa thể build-test Phase 6 trên 2 máy đó
cho tới khi có 1 camera đã cập nhật lên đúng thế hệ firmware chứa Core này.

**Đề xuất lại thứ tự Phase 6** (thay cho đề xuất 6a/6b cũ):
- **6a — Analytics Module Configuration** (7.10, MANDATORY): map catalog `plugin_id` (đọc từ
  HAL/VPU plugin có sẵn — human_detect, vehicle_detect, license_plate_detect, traffic_sign_detect,
  pothole_detect, manhole_detect, fighting_recognition) vào
  `GetSupportedAnalyticsModules`/`GetAnalyticsModules`. Không phụ thuộc runtime Core, có thể làm
  ngay bằng cách đọc danh sách plugin tĩnh.
- **6b — AiEvents/PullPoint event thật** (7.5 nội dung + 8.7-8.12 field): route qua Core REST
  `GET /core/v1/AiEvents` (port 8092) — code onvif-module có thể viết ngay theo contract đã đọc,
  nhưng **cần 1 camera đang chạy đúng bản Core này để build-test thật**, chưa có trên `.194`/`.102`.
- **6c — Rule Configuration** (8.6, CONDITIONAL, không bắt buộc): map `/core/v1/Rules` — làm sau
  6a/6b nếu muốn tăng coverage, không cần cho mandatory baseline.
- Metadata RTP streaming hạ tầng: tái dùng cơ chế gortsplib cũ (đã hoạt động), chỉ đổi nguồn XML
  frame từ giả sang đọc thật từ `/core/v1/AiEvents` mới nhất theo channel.

#### Nghiên cứu bổ sung Phase 6 (2026-10-02 → 2026-10-05) — TẠM HOÃN, chưa code, ghi lại để làm tiếp sau

**Trạng thái:** `PAUSED`. Chưa sửa dòng code nào của `onvif-module` cho Phase 6. Lý do tạm hoãn
(2026-10-05): build thử trên camera `.70` bị crash do CPU bị chiếm nhiều (nhiều dev dùng chung
máy). Phần dưới là toàn bộ kiến thức đã xác minh/suy luận được, để quay lại làm không phải
nghiên cứu lại từ đầu. Mức chắc chắn được ghi rõ: **[verified]** = đã đọc source/SSH trực tiếp,
**[suy luận]** = kết luận từ bằng chứng gián tiếp, **[chưa verify]** = chưa kiểm chứng.

##### A. Camera `.70` — thiết bị đầu tiên chạy đúng thế hệ Core mới

- **[verified]** `.70` = board **Ambarella CV25** (hostname `Oclea`, aarch64, Linux 5.4 nhúng,
  BusyBox), KHÁC các Jetson `.124/.125/.194/.102`. **Không có `apt`/`dpkg`**, không có
  `gsoap`/`wsdl2h`/`soapcpp2`; có `g++`, `make`, `git`. Root `/` chỉ 2.3GB (còn ~279MB) — build ở
  `/home`/`/opt` có nguy cơ làm đầy đĩa và hại Core/DVR/MGMT đang chạy. Ổ lớn là `/media`
  (57GB, còn ~9–11GB); trên máy đã có tiền lệ build ở `/media/build_sqlite`, `/media/build_htop`.
  Repo `onvif-tools` đã được pull tại `/media/onvif/onvif-tools/` (module ở
  `.../onvif-module/onvif-module`).
- **[verified]** Bố cục service thật: `/opt/services/core/` (Core, `bin/core`, `core_config.json`,
  log `bin/core_private.log`), `/opt/services/mgmt-be/` (+ `mgmt-test/`), `/opt/dvr_apps/` (DVR,
  mediamtx, board_profiles), `/opt/gateway_apps/`, **VPU: `/opt/bin/vpu`**, model plugin tại
  `/opt/ai/models/{license_plate_detect,ocr}/` (mỗi plugin `manifest.json` + `.so` + `classes.txt`),
  config VPU `/opt/configs/config.json`. `/home/alvis` gần như trống (chỉ `.pid` do chạy tay).
- **[verified]** `core.service`/`dvr_new.service`/`mgmt.service` là systemd; **VPU lúc kiểm tra
  (2026-10-02) chạy tay** (`./vpu --config config.json`, gắn `pts/3`, KHÔNG systemd) → nếu
  reboot/đóng phiên SSH sẽ không tự chạy lại. **Cần hỏi backend team** VPU có được đóng gói
  systemd không trước khi dựa vào nó cho Phase 6b.
- **[verified]** `onvif-module` KHÔNG chạy trên `.70` (chưa deploy): không process, không port
  8000/8001/554. Core nghe `0.0.0.0:8092` (`core_config.json`) → về nguyên tắc `onvif-module`
  chạy trên máy khác vẫn gọi được Core `.70` qua mạng **[chưa verify]**.

##### B. Core thật trên `.70` — luồng ALPR đã chạy end-to-end (2026-10-02)

- **[verified]** Sau khi backend bật VPU lúc 13:35: log Core có chuỗi
  `ALPREVENT-RX` → `FRAME-OK shm=cam_vpu_alpr_shm 1920x1080` → `EVENT-REPO-CREATE-OK` →
  `ALPREVENT-SAVE-OK ... images=4`; `/media/tmon_event` từ 0 file/4KB lên 176 file `.jpg`/40MB
  (ảnh thật). Có **dedupe** `ALPREVENT-DUP repeated_plates=... event not saved` — chỉ lưu khi có
  thay đổi, không lưu mọi lần RX (RX ~150–300ms/lần). Event bus trên log:
  `/tmp/cam_event_bus.sock` (không thấy file trong `ls /tmp/*.sock` — nhiều khả năng abstract
  socket, không phải lỗi). Core đăng ký 3 nhóm topic: `/ai_event`, `ALPR_EVENT_TOPIC`,
  `/hardware/voltage|temperature`.
- **[verified]** Dữ liệu ban đầu (29–30/09, ~17 giờ, cùng biển `30G43903`, confidence
  `0.9014173150062561` trùng 16 chữ số) là **demo/bơm test**: `img_urls` trỏ tới file không tồn
  tại (`find /media/tmon_event -name '*.jpg'` = 0), UI MGMT hiện "No image". Core tin `img_urls`
  có sẵn trong payload (`extractRootField(details,"img_urls",...)`) và không kiểm tra file tồn tại.
- **[verified]** Lỗi `Failed to connect to frame pool "CAM_SHM"` ở MỌI lần Core khởi động là pool
  mặc định khác; luồng ALPR dùng shm riêng **`cam_vpu_alpr_shm`** và đọc frame thành công. Đừng
  kết luận "FramePool hỏng" chỉ từ dòng `CAM_SHM`.
- **[verified]** `GET /core/v1/AiEvents` — JSON thật của 1 event ALPR (bbox là **pixel**, không
  chuẩn hoá; `plate_text` nằm trong `details.detections[]`; **không có `track_id`/`ObjectId`**):
  ```json
  {"event_key":"<uuid>","name":"ALPR_EVENT","channel":"0","object_label":"30G43903",
   "roi_id":"10","start_time":1790923076799191,"end_time":...,"created_at":...,
   "details":{"event_type":"ALPR","roi_id":10,"roi_name":"License Plate Tracker 01",
     "timestamp_us":89900000,
     "detections":[{"plate_text":"30G43903","confidence":0.9014,"x":839.32,"y":669.66,"w":138.5,"h":30.0}]},
   "img_urls":[".../..._lp.jpg",".../..._pano.jpg",".../event_ctxt_...jpg",".../event_osd_...jpg"],
   "video_url":""}
  ```
  `start_time` là epoch **microsecond UTC** (đổi sang `xs:dateTime` có `Z`). Frame 1920×1080
  (kênh `0`). Trường `channel="0"` trùng quy ước token kênh DVR đã dùng ở Phase 4 (`"0"`,
  `"0_sub"`...). `/core/v1/EventsApp` (non-ALPR) trả rỗng — chưa có event non-ALPR nào.
- **[verified]** Hai đường xử lý KHÁC chính sách trong `AIEventIpcController`:
  - `handleAlprEvent()` (topic `ALPR_EVENT_TOPIC`): **không qua RuleEngine/AlarmManager** (comment
    trong code: "every well-formed ALPR event gets saved"). Vì vậy `Rules`/`AlarmConfigs` rỗng
    (`[]`) mà vẫn có `AiEvents`.
  - `handleAiEvent()` (topic `/ai_event`, dành cho plugin khác như human/vehicle): cửa Rule
    **auto-pass** nếu kênh chưa có Rule (cố ý, có comment); nhưng cửa
    `AlarmManager::raise()` → `findConfig()` yêu cầu khớp CHÍNH XÁC `(channel, type_name)` trong
    `AlarmConfigs`, không khớp thì `raise()` trả `false` và event **bị huỷ trước
    `repository.create()`**. ⇒ khi nối `human_detect`/`vehicle_detect`, **phải tạo ít nhất 1
    `AlarmConfig` khớp** (`POST /core/v1/AlarmConfigs`) thì event mới xuất hiện trong `AiEvents`.
    Nếu test Phase 6b thấy "REST đúng, code đúng nhưng không có object" → kiểm tra mục này đầu tiên.
  - Ghi chú đính chính: nhận định trước đây coi `RuleEngine::loadRules()` là "bug dead-cache" và
    `EventBus::serialize_event()` không copy `dets` đọc từ snapshot source; log `.70` cho thấy
    `dets=0` và dữ liệu đi bằng `payload` JSON cho ALPR (khớp). Với nhánh `/ai_event` **[chưa
    verify]** trên bản chạy thật.
- Ý nghĩa 3 bảng: `AiEvents` = nhật ký kết quả cuối; `Rules` = lọc theo nội dung/ROI/plugin (áp
  trước); `AlarmConfigs` = chính sách tần suất/lịch/cooldown (áp sau, đồng thời là cổng ghi cho
  đường `/ai_event`). ALPR cố ý không gate vì mỗi lần đọc biển là 1 dữ kiện riêng.

##### C. 7 plugin VPU/HAL ↔ ONVIF (đọc source HAL, branch `dev` — đầy đủ hơn `hieu-dev`)

- **[verified]** Hợp đồng plugin (`include/hal/model_plugin.h`, `vpu_types.h`, chỉ có ở branch
  `dev`): `IModelPlugin::process(FrameRef) -> DetectionResult`; `Detection{label, confidence, bbox
  pixel, text (OCR), ...}`. Cần lưu ý repo HAL tham chiếu header/`PluginManager` của project `VPU`
  riêng (Makefile ghi `see VPU/Makefile`) — **source orchestrator VPU (PluginManager, gán
  `track_id`, ghép bbox+OCR, publish BUS) KHÔNG có trong checkout cục bộ**; mọi mô tả về nó là
  **[suy luận]**. Từng có bug ABI `Tensor` lệch layout giữa HAL và `Camera-SW/interfaces/vpu_types.h`
  (đã sửa trong comment, rủi ro tái diễn nếu 2 bản lệch).
- **[verified]** `license_plate_detect` CHỈ cho bbox vùng biển (label `license_plate`); chữ biển do
  plugin **`ocr`** riêng (CRNN, input 168×48) — VPU orchestrator ghép 2 kết quả **[suy luận]**.
  Tên `name()` không đồng nhất (`human_detect`, `vehicle_detect`, `LicensePlateDetection`,
  `TrafficSignDetection`, `PotholeDetection`, `ManholeDetection`, `fighting_recognition`); còn
  `potmanhole_trafficsign_detect` là bản gộp 3 loại. `traffic_sign` dùng ~50 biển báo VN
  (`P.127`, `W.224`...), `pothole`/`manhole` thuần vendor.
- Mức ghép vào ONVIF: **`human_detect` (Type=Human), `license_plate_detect` (License Plate
  recognition, có field `PlateText` chuẩn), `vehicle_detect` (Type=Vehicle)** có ý nghĩa chuẩn;
  `traffic_sign`/`pothole`/`manhole` ghép được nhưng chỉ là chuỗi vendor tự đặt; **`fighting_recognition`
  không có bbox** (phân loại cả clip) → không hợp `tt:Object`, nên khai như **Event** (ví dụ
  `IsFighting`), không đưa vào Metadata stream. Khi khai `AnalyticsModuleDescription` nên
  `fixed="true"` (khả năng phần cứng cố định, đúng nguyên tắc Fixed vs Configurable ở Phase 3).
  ONVIF không cố định tên `AnalyticsModule`; `tt:ClassCandidate/tt:Type` là chuỗi tự do.
- Chuẩn hoá bbox: ONVIF `tt:BoundingBox` là **0.0–1.0** (`left=x/W, top=y/H, right=(x+w)/W,
  bottom=(y+h)/H`), VPU/Core lưu **pixel** → cần W×H của VideoSource đang stream (lấy động từ
  DVR `GetProfiles`, không hardcode 1920×1080).
- **Khoảng trống dữ liệu còn lại cho Metadata** (không ảnh hưởng Event): không có
  `track_id`/`ObjectId` trong `AIEvent`; `bbox`/`plate_text` chỉ nằm trong `details` JSON tự do
  (hiện đã có cho ALPR, quy ước chưa được cam kết bằng schema chính thức → nên xin Core team xác
  nhận quy ước/đưa thành field).

##### D. Trạng thái code `onvif-module`/mock hiện tại (đối chiếu để biết cần làm gì)

- **[verified]** `AnalyticsService::handleGetSupportedAnalyticsModules()` **hardcode** 2 type giả
  (`CellMotionEngine`, `ObjectDetection`), bỏ qua request; `GetSupportedMetadata` hardcode
  `SampleFrame` bbox toàn 0; `AnalyticsModuleStore` là map RAM thuần (CRUD instance, bắt đầu rỗng) —
  giữ nguyên được, chỉ cần đổi **catalog**. `GetServiceCapabilities` đã đúng `RuleSupport="false"`.
  `Media2MetadataService` cũng mock (token cố định `vac_main`/`metadata_config`).
- **[verified]** Metadata RTP stream của mock **không nằm trong `onvif-module`** mà là chương trình
  Go `mock-camera-backend/rtsp/gortsplib-relay/main.go`, bơm XML giả định kỳ — chỉ chứng minh cơ chế
  RTP/SDP, DTT không kiểm nội dung. **Chưa có code production cho Metadata thật**; bản C++
  `AlvisOS/DVR/src/utility/ffmpeg_engine/ffmpeg_out_wrap.cpp::openMetadataStream()/sendMetadataFrame()`
  dựng đúng SDP `vnd.onvif.metadata` nhưng là dead code (không nơi nào gọi). Đây là câu hỏi kiến
  trúc còn mở: Metadata thật sẽ chạy bằng code nào.
- **[verified]** `MockSubscriptionManager` đã có đủ cơ chế Event (CreatePullPoint, PullMessages,
  Renew, Unsubscribe, SetSynchronizationPoint, `pending` theo subscription, filter topic) nhưng chỉ
  phát topic Profile T nền (`VideoSource/MotionAlarm`, `GlobalSceneChange`, tampering) và
  Media/Recording (`fireProfileChanged`, `fireConfigurationChanged`, `fireRecording*`).
  **Không có topic nhận diện AI nào** (chưa có `LicensePlateDetection`...). Nghĩa là mock chưa từng
  phải chọn Metadata hay Event cho dữ liệu AI — quyết định này là mới.
- DTT không kiểm nội dung (Profile M từng 244/244 với số giả) nhưng khai topic rồi thì khi điều kiện
  thật xảy ra phải bắn; không nên khai capability/topic mà backend không phát (nguyên tắc #5).

##### E. Quyết định kiến trúc: Metadata hay Event — đề xuất làm **Event trước** (chờ chốt)

- Khác biệt cốt lõi: **Metadata** là track RTP trong CÙNG phiên RTSP với video (cần SETUP/PLAY,
  VMS tự khớp `UtcTime` với PTS frame và vẽ box đè; tắt video là mất); **Event** đi kênh SOAP/HTTP
  riêng (PullPoint), không liên quan RTSP, VMS không cần mở video vẫn nhận, tồn tại như dòng trong
  danh sách sự kiện. ONVIF không ép chọn: Metadata streaming (mechanism) bắt buộc cho Profile M,
  còn dùng kênh nào cho từng loại kết quả AI là quyết định sản phẩm; có thể làm cả hai.
- Lý do nghiêng Event cho ALPR: (1) hạ tầng Event trong `onvif-module` có sẵn ~90%, chỉ thiếu 1
  hàm `fire...` + khai topic; (2) dữ liệu ALPR rời rạc (Core dedupe) khớp Event; (3) không cần đồng
  bộ UtcTime↔PTS/normalize theo video; (4) UI MGMT hiện hiển thị ALPR dạng danh sách sự kiện;
  (5) tránh phải giải câu hỏi "Metadata production chạy bằng code nào". `human_detect`/`vehicle_detect`
  (liên tục theo frame) về sau hợp Metadata hơn. **Chờ người có thẩm quyền sản phẩm chốt.**

##### F. Thiết kế dự kiến khi quay lại (theo pattern `IMgmtClient`/`HttpMgmtClient`, không phá DTT)

1. **6a (không phụ thuộc runtime, làm được ngay):** thay catalog hardcode trong
   `handleGetSupportedAnalyticsModules()` (và cân nhắc `GetSupportedMetadata`) bằng danh sách plugin
   tĩnh (7 plugin, `fixed="true"`, `ParentTopic` tương ứng). Giữ nguyên `AnalyticsModuleStore`.
2. **6b-Event:** thêm `ICoreClient`/`HttpCoreClient` (host/port từ `onvif.conf`, mặc định Core
   `:8092`, timeout, map lỗi sang SOAP Fault) + thread polling `GET /core/v1/AiEvents?channel=&start_time=<cursor>`
   mỗi ~1–2s với cursor `start_time` (tránh xử lý lặp); mỗi `AIEvent` mới → `fireLicensePlateRecognized(...)`
   trong subscription manager, topic dự kiến `tns1:RuleEngine/LicensePlateDetection`, Source =
   `VideoSourceConfigurationToken` (channel `"0"`), Data = `PlateText`, `Confidence` (+ bbox
   normalize nếu muốn), `PropertyOperation`, `UtcTime` từ `start_time`. Khai topic trong
   `GetEventProperties` đúng cấu trúc đã pass DTT (chỉ node gốc có `tns1:`, leaf không prefix).
   Capability `events`/`analytics` gate `real/mock` như các phase trước; production không fallback
   dữ liệu giả.
3. **6b-Metadata (làm sau):** chỉ khi có plugin theo frame; trước đó chốt code Metadata production.
4. **6c Rule (CONDITIONAL):** vẫn không làm.
5. Backend cần (không phải việc onvif-module): VPU chạy thường trực (systemd); với plugin non-ALPR
   tạo `AlarmConfig`; thống nhất schema `bbox`/`track_id`/`plate_text` trong `AIEvent`.

##### G. Triển khai/build trên `.70` — bài học và phương án khi quay lại

- **[verified]** Cấu trúc Makefile: `make full` phụ thuộc `check-gen` (chỉ kiểm
  `generated/soapC.cpp` tồn tại, `GEN_DIR=generated`), KHÔNG tự chạy `gen`; `make gen` LUÔN sinh lại
  từ WSDL bằng `wsdl2h`+`soapcpp2` (không kiểm file đã có) nên lỗi `wsdl2h not found` trên `.70`.
  `external/gsoap/` là runtime gSOAP (`stdsoap2.cpp`...), còn `generated/` (soapC.cpp,
  soap*BindingService.cpp...) mới là code sinh từ WSDL và chỉ là **text C++ portable** — copy nguyên
  `generated/` từ máy đã `make gen` thành công (vd `.102`/`.125`) rồi chạy **`make full`** (không chạy
  `make gen`). Lúc dừng chưa xác nhận lần build `make full` hoàn tất; lần chạy gần nhất bị crash vì
  CPU bận.
- **[verified]** Makefile link `gstreamer-1.0`, `gstreamer-rtsp-server-1.0`, `openssl` qua
  `pkg-config` → **[chưa verify]** `.70` có dev lib/`pkg-config` các gói này hay không (có thể là
  blocker tiếp theo).
- Phương án giảm rủi ro khi quay lại: (a) KHÔNG build nặng trên `.70` lúc nhiều dev đang dùng, tránh
  `make -j`, build ngoài giờ cao điểm; (b) **chạy `onvif-module` trên máy khác (vd Jetson `.125`) cho
  dev, trỏ `ICoreClient` tới `http://192.168.8.70:8092`** — không cần build trên `.70` **[chưa
  verify khả năng mạng]**; (c) nếu cần chạy ngay trên `.70`, build ở máy khác rồi copy binary có rủi
  ro lệch phiên bản thư viện (rootfs Oclea ≠ Ubuntu Jetson) **[chưa verify]**. Mọi build/chạy phải
  ở `/media`, không dùng `/home` hoặc `/opt` của `.70` (đĩa `/` chật).
- Quy ước vận hành của dự án vẫn giữ: không đè repo thật trên server bằng `sftp.put`; sửa code ở
  local rồi commit/push, camera chỉ `git pull`.

##### H. Câu hỏi còn mở / việc cần xác nhận trước khi code

1. Product chốt Metadata hay Event (hay cả hai) cho ALPR — đề xuất Event trước.
2. Backend: VPU có chạy systemd thường trực không; `Core`/VPU có cập nhật cho các camera khác
   (`.124/.125`) để test không (hiện chỉ `.70` có Core mới + VPU).
3. Core team: quy ước/cam kết schema cho `bbox`, `track_id`, `plate_text` trong `AIEvent`; ALPR có
   cố ý không qua Rule/Alarm.
4. `onvif-module` sẽ chạy ở đâu cho dev Phase 6 (trên `.70` hay máy khác gọi sang Core `.70`).
5. Metadata thật dùng code nào (Go gortsplib hay C++ `sendMetadataFrame()` của DVR).
6. Channel→VideoSource/Profile token mapping chính thức; polling interval; xử lý nhiều `event_type`
   (`details.event_type`) khi các plugin khác online.

##### I. Lệnh kiểm tra nhanh trạng thái `.70` (chạy trên máy đó)

```bash
curl -s "http://127.0.0.1:8092/core/v1/AiEvents?max_items=5"          # event đã lưu
ps aux | grep -iE "vpu|core|dvr|mgmt" | grep -v grep                  # có VPU/service nào chạy
grep -iE "ALPREVENT|AIEVENT" /opt/services/core/bin/core_private.log | tail -30   # luồng nhận/lưu/dedupe
find /media/tmon_event -name "*.jpg" | wc -l                           # ảnh evidence thật có không
```

### Gate

- Metadata XML valid schema.
- ObjectId ổn định và bounding box đúng hệ tọa độ.
- Topic phát ra tồn tại trong `GetEventProperties`.
- Profile M tests pass với dữ liệu thật.

## 10. Phase 7 — Profile G Recording, Search và Replay

### Nguồn thật

- Recording/track/job/index: DVR (`AlvisOS/DVR`, REST `:8200` + WS `:8210`).
- Dữ liệu video đã ghi: file MP4 do DVR ghi tại `/media/records/<camera>/`.
- **Replay RTSP: DVR KHÔNG có — phải xây mới** (xem mục "Quyết định D4").
- Historical event/metadata index: DVR + Core (hoãn, xem "Ngoài phạm vi đợt 1").

### Trạng thái hiện tại (đối chiếu code, 2026-10-07)

**Phía `onvif-module`** (`onvif-module/onvif-module`):

- `RecordingService.cpp` / `SearchService.cpp` / `ReplayService.cpp` là **stub tĩnh**, comment đầu
  file ghi rõ "Data model tĩnh → không cần backend/IPC". Dữ liệu cố định: `Recording_0`
  (`VIDEO_0` + `META_0`), `Job_0`, `profile_main`, trạng thái giữ trong biến global của file.
- `config/onvif.conf` `[capabilities]`: `recording = mock`, `search = mock`, `replay = mock`.
- `ReplayService.cpp`: `GetReplayUri` kiểm `RecordingToken == "Recording_0"` rồi trả
  `rtsp://<host>:8555/replay` (cổng hardcode của relay mock).
- Các service này chưa đi qua `ICameraBackend` hay `IDvrClient` (`IDvrClient` hiện chỉ có Media2:
  `getProfiles/getStreamUri/getSnapshotUri`).
- Tunnel RTSP-over-HTTP (`OnvifServer.cpp:81-144, 397-423`) proxy **mọi** kết nối có header
  `x-rtsp-tunnelled` tới **một** cổng `cfg_.rtspPort` (không phân biệt path). Với DVR thật, cổng này là
  MediaMTX (live). Replay cần cổng khác nên tunnel phải chọn đích theo path.
- Event Recording (`RecordingConfig/*`, `JobState`) đi qua `MockSubscriptionManager`
  (`fireRecordingConfigChanged`, `fireRecordingJobState`).

**Mock replay hiện tại là giả.** `mock-camera-backend/rtsp/gortsplib-relay/main.go`: path `replay`
chỉ **relay lại luồng live `main`** (`srcPath = "main"`, dòng 175), rồi gắn extension `0xABAC` và
giờ lấy từ `Range`. Không đọc dữ liệu đã lưu, nên chỉ chứng minh được phần giao thức đúng chuẩn
(DTT 313/313), chưa chứng minh được phát lại từ archive. Phần tái dùng được: `parseClockRange`,
`ntpTimestamp`, dựng extension `0xABAC`, xử lý `Immediate` + cờ D, `PayloadMaxSize = 1434`.

**Phía DVR** (đọc source, chưa chạy thử trên thiết bị):

| Hạng mục | Thực tế |
|---|---|
| Nơi lưu | `/media/records/<tên camera>` (`dvr_controller.cpp:291`); `/media/dvr_data/storage/<ch>` chỉ là mặc định dự phòng |
| Định dạng | MP4 video-only, H.264/H.265, mỗi file mở đầu bằng keyframe có SPS/PPS (`record_writer.cpp:196-214`), `+faststart` (`ffmpeg_out_wrap.cpp:444`) |
| Tên file | `<profile>_YYYYMMDD_HHMMSS[_EVENT].mp4`, giờ **địa phương** |
| Chỉ mục | Quét thư mục + đọc `moov` (`file_utils.cpp:84`), cache 10 giây, thời lượng số giây nguyên |
| REST | `RecordPlaybackfilter`, `GetRecordingList`, `DeleteVideoByID`, `ExportVideo`, `SetOnOffVideoRecorder`, `Get/SetWeeklyScheduleRecordOnOff`, `GetListVideoSourceRecorder` |
| WS `:8210` | Chỉ đẩy trạng thái ghi (`IsRecording`, `Manual`, `Scheduled`, `Writing`) và nhận `SetRecord` |
| Phát lại | **Không có.** RTSP chỉ có `/live/ch<N>`. Web UI nhiều khả năng tải file qua HTTP tĩnh `[chưa verify]` |
| Retention | `StorageManager` xóa file cũ nhất khi đĩa đầy, không phát event |

### Quyết định kiến trúc (đề xuất — cần user chốt trước khi code)

| # | Vấn đề | Đề xuất | Lý do / phương án khác |
|---|---|---|---|
| D1 | Recording theo đơn vị nào | **1 Recording / 1 VideoSource** (`rec_<VideoSourceId>`, ví dụ `rec_0` = context, `rec_1` = alpr) | Đúng spec (Recording là vật chứa theo nguồn). Không dùng 1 Recording / 1 file |
| D2 | Track | **(đã điều chỉnh 2026-10-07 theo thông tin của user)** Mỗi sensor có 2 luồng main + sub nhưng **chỉ ghi 1 luồng tại một thời điểm**. Đề xuất: Recording có **2 track video** `VIDEO_main`, `VIDEO_sub` (theo `recordable` của từng stream); dữ liệu của track nào chỉ có ở những lúc luồng đó được ghi (suy ra từ tiền tố tên file `_main_`/`_sub_`). Không audio, không metadata | Spec cho phép Recording nhiều track và track có khoảng trống. Gộp main/sub vào 1 track sẽ đổi độ phân giải/codec giữa chừng khi người dùng chuyển luồng. Hiện `.194` chỉ có file `_main_` (7632 context + 6857 alpr, không file `_sub_` nào) nên đợt đầu replay có thể chỉ cần phục vụ track có dữ liệu. **Chưa chốt** |
| D3 | Recording Job ↔ DVR | Job (sự tồn tại, token, nguồn, priority) do **onvif-module lưu** (JSON ở thư mục bền vững); `Mode` **là** trạng thái ghi tay của DVR, điều khiển bằng `SetOnOffVideoRecorder` | **Đã sửa sau khi đọc source + kiểm `.194` (2026-10-07):** ghi tay của DVR **có bền qua restart** (`saveManualRecord` / `resumeManualRecords`, `dvr_controller_record.cpp:62-80`; trên `.194` DVR khởi động 08:37:14 và tự ghi lại lúc 08:37:18 dù lịch tắt). Nên không cần onvif-module "áp lại Mode". `testAPI.md` ghi "không phải cấu hình" chỉ đúng với `ResetDvrConfiguration`. **Bổ sung (2026-10-07):** ghi tay chỉ cho **1 luồng mỗi sensor** (`startManualRecord` tắt ghi tay của luồng kia, `dvr_controller_record.cpp:257-264`), nên `SourceToken` của Job = profile của luồng đang ghi; đổi sang luồng kia = đổi nguồn của Job. Ghi theo lịch thì code **không** loại trừ nhau (`startScheduledRecording` không tắt luồng khác) và ghi sự kiện chỉ ở main — `GetRecordingJobState` phải phản ánh đúng trạng thái thật, không giả định luôn 1 luồng |
| D4 | Replay RTSP server | **Phương án A**: process mới (Go + gortsplib), đọc trực tiếp MP4 của DVR | Tái dùng được code giao thức của mock. B (mở rộng gst-rtsp-server của DVR): legacy `REPLAY_RTSP.cpp` chỉ phát 1 file cố định, không có Range clock. C (URL tĩnh/HTTP): không đúng chuẩn |
| D5 | Replay lấy chỉ mục ở đâu | Gọi REST `GetRecordingList`/`RecordPlaybackfilter` của DVR (nguồn sự thật duy nhất) | Quét thư mục riêng sẽ lệch logic với DVR (múi giờ, `_EVENT`, file đang ghi) |
| D6 | Xác thực replay | Replay server **gọi lại cùng đường xác thực** mà onvif-module dùng với MGMT (`verifyHttpDigest`); không lưu mật khẩu riêng | Mock dùng credential tĩnh, không dùng cho thật. Cách cụ thể (replay server gọi MGMT hay onvif-module mở endpoint loopback) chốt ở bước 7.4 |

Mock vẫn là baseline: mỗi capability có cờ `mock|real` trong `[capabilities]`, `mock` giữ nguyên hành vi hiện tại (313/313).

### Ánh xạ ONVIF ↔ DVR

| ONVIF | Nguồn dữ liệu / cách làm | Ghi chú |
|---|---|---|
| `GetRecordings` | Danh sách `VideoSourceId` có `recordable` (từ `GetListVideoSourceRecorder`) | Token ổn định `rec_<id>`; không dùng `Id` của `RecordPlaybackfilter` (đánh số lại mỗi lần có file mới) |
| `GetRecordingConfiguration` / `SetRecordingConfiguration` | Lưu `MaximumRetentionTime` trong store của onvif-module; không ép DVR | DVR chỉ có xóa theo đĩa đầy, chưa có retention theo thời gian |
| `GetTrackConfiguration` / `SetTrackConfiguration` | Một track `VIDEO_main`, `Description` lưu cục bộ | |
| `GetRecordingJobs` / `GetRecordingJobConfiguration` | Store của onvif-module (D3) | |
| `CreateRecordingJob` / `DeleteRecordingJob` | Tạo/xóa bản ghi trong store; `Delete` kèm đưa Mode về Idle | Mandatory theo Profile G; cần kiểm kỹ hành vi DTT mong đợi |
| `SetRecordingJobMode` | `Active` → `SetOnOffVideoRecorder{Enable:true}`; `Idle` → `Enable:false` | Nếu lịch của DVR vẫn đang ghi thì trạng thái thật vẫn là đang ghi: **trả đúng trạng thái thật**, không nói dối |
| `GetRecordingJobState` | `GetListVideoSourceRecorder.IsRecording` (+ WS `Writing`) | `Writing=false` khi yêu cầu ghi nhưng đĩa tháo → báo lỗi qua `State.Error` |
| `GetRecordingOptions` | Cố định theo khả năng thực | `DynamicRecordings=false`, `DynamicTracks=false` |
| `GetRecordingInformation` | Từ danh sách file: `EarliestRecording` = giờ bắt đầu thật của file đầu (`mtime − duration`, **không** lấy từ tên file), `LatestRecording` = giờ đóng (`mtime`) của file đã **đóng** gần nhất | File đang ghi chưa đọc được (`+faststart`), `LatestRecording` trễ tối đa 1 đoạn `FileDuration` |
| `GetRecordingSummary` | Tổng hợp từ cùng nguồn | |
| `FindRecordings` + `GetRecordingSearchResults` + `EndSearch` | Phiên tìm kiếm lưu trong onvif-module (SearchToken, `KeepAliveTime` ≥ 10 giây, tự hủy khi hết hạn) | Mock hiện trả hết 1 lần; real vẫn có thể hoàn tất ngay nhưng phải có vòng đời token đúng |
| `FindEvents` + `GetEventSearchResults` | **Tự suy ra** từ ranh giới file: `tns1:RecordingHistory/Track/State` (`IsDataPresent`) và `.../Recording/State` (`IsRecording`); `IncludeStartState` sinh event ảo tại `StartPoint` | Theo thứ tự thời gian. Ngưỡng coi là "có khoảng trống" giữa 2 file cần đo thật (V3) |
| `GetMediaAttributes` | Từ file: codec, độ phân giải, giờ bắt đầu/kết thúc | |
| `GetReplayUri` | `rtsp://<host>:<replay_port>/replay/<RecordingToken>` | Host lấy từ request như hiện tại; cổng đọc từ config, không hardcode |
| `Get/SetReplayConfiguration` | Giữ `SessionTimeout` cục bộ | |
| Event `RecordingConfig/JobState` | Phát khi Mode đổi (từ SOAP hoặc từ WS `:8210`) | Dùng `MockSubscriptionManager`; **phụ thuộc quyết định Event vs Metadata của Phase 6** (đang hoãn) |

### Các bước triển khai

**7.0 — Xác minh dữ liệu thật trên camera (bắt buộc, trước mọi dòng code)**

Chạy trên camera `.102` (SSH) rồi ghi kết quả vào mục này:

```bash
ls -l --time-style=full-iso /media/records/*/ | tail -40
```

```bash
ffprobe -v error -show_format -show_streams -show_entries packet=pts_time,flags -of compact <file.mp4> | head -60
```

| Mã | Cần biết | Vì sao |
|---|---|---|
| V1 | Cấu trúc thư mục thật: stream sub nằm ở đâu? (export dùng `<cam>_<role>`, ghi lại dùng `<cam>`); `scanRecordFiles` chỉ quét `context`/`alpr` | Quyết định Recording/Track có thấy được sub hay không |
| V2 | Codec, khoảng cách keyframe, `time_base`, vị trí `moov` | Tính độ trễ vào điểm phát, thiết kế seek |
| V3 | Khoảng cách thật giữa 2 file liên tiếp; độ dài file so với giờ thật | Ngưỡng báo khoảng trống; độ chính xác NTP (PTS do DVR **dựng theo FPS**, `ffmpeg_out_wrap.cpp:422-424`) |
| V4 | Múi giờ tiến trình DVR so với `GetSystemDateAndTime` | Tên file là giờ địa phương, ONVIF cần UTC (`Z`) |
| V5 | Tiến trình replay có đọc được `/media/records` không (user, quyền) | |
| V6 | Có build được Go (arm64) cho CV25, kích thước binary, CPU/RAM còn lại | Máy dùng chung nhiều dev; từng sập khi build trên `.70` |
| V7 | Web server nào đang phục vụ `VideoUrl` | Hiểu playback hiện tại để không phá |
| V8 | Danh sách case DTT Profile G (Recording Control, Search, Replay) trong `g13.xml` và điều kiện dữ liệu mỗi case cần | Archive thật phải đủ để các case không rơi vào N/A hoặc FAIL |

#### Kết quả bước 7.0 trên camera `192.168.8.194` (2026-10-07, SSH chỉ đọc)

Dữ liệu đo từ thiết bị thật (aarch64, DVR chạy bằng `root`, `onvif-module` chạy bằng `alvis`).

| Mã | Kết quả | Hệ quả cho kế hoạch |
|---|---|---|
| V1 | Chỉ có `/media/records/context` (7439 mp4), `/alpr` (6934 mp4), `/overlay` (rỗng). **Không có file sub** (`_sub_` = 0). `/media/dvr_data/storage/0` là thư mục cũ (26/09), không còn ghi. API `GetListVideoSourceRecorder` có thêm nguồn `2 "Overlay"` báo `IsRecording:true` nhưng không có file. Số file `_EVENT` ở context: 1 | Hiện chỉ có luồng main được ghi, nhưng sensor có cả main và sub (chỉ ghi 1 luồng một lúc) nên D2 đã được điều chỉnh (xem bảng quyết định). **Không đưa nguồn Overlay vào `GetRecordings`**; chỉ lấy nguồn có dữ liệu thật (0, 1) |
| V2 | alpr: H.264 2464×2056, 30 fps, **keyframe mỗi 1,0 s**, `time_base 1/90000`, `moov` ở **đầu file** (`+faststart` đúng). context: H.264 640×360, `r_frame_rate 15`, nhưng **chỉ 1 keyframe/file** (8/8 file kiểm tra) | Điểm vào replay của alpr tốt (≤ 1 s). Với context, điểm vào chỉ là **đầu file** (có thể lùi tới ~40 s so với mốc yêu cầu) |
| V3 | **(đã hiệu chỉnh sau khi đối chiếu log DVR, 2026-10-07)** File context có `ffprobe duration` nhỏ hơn khoảng "tên file → `mtime`" (ví dụ `103317`: 41 s so với 15,5 s). Nguyên nhân **không phải PTS sai** mà là **tên file là giờ MỞ segment, còn frame đầu đến muộn hơn**: DVR bỏ các frame P cho tới keyframe đầu tiên (`DVR03-REC-KEYFRAME … skipped: 366` = 24,4 s ở 15 fps). Kiểm 4 file: keyframe đầu theo log `10:33:42,43` / `10:34:33,03` / `10:35:23,63` / `10:36:14,23`; `mtime − duration` cho `10:33:42,48` / `10:34:33,08` / `10:35:23,67` / `10:36:14,27` → **khớp trong ~50 ms**. Số frame bỏ ÷ 15 cũng khớp thời gian chờ (24,4 s / 5,0 s / 15,6 s / 26,2 s). Vậy trong các file đo được, **frame đến đều 15 fps và PTS theo số đếm khớp thời gian thật**. Keyframe của context cách nhau **≈ 50,6 s** (GOP rất dài). Các cảnh báo `FPS-LOW` (fps 0) phần lớn là đếm lúc **đang chờ keyframe**, không phải encoder tụt fps. `REC-EMPTY` (xóa segment không có keyframe) tạo khoảng trống thật | (a) **Giờ bắt đầu thật của file = `mtime − duration`** (sai số ~50 ms), không phải giờ trong tên file. (b) API `StartTime`/`EndTime` hiện sai: `start` lấy từ tên file (sớm hơn thật 5–26 s), `end = start + duration` (sớm hơn `mtime`). Phải tính lại ở `onvif-module`. (c) Khoảng trống thật giữa các file context là 30–50 s (mất do chờ keyframe + segment bị xóa); ngưỡng báo khoảng trống ≥ 5 s. (d) **Chưa chứng minh được** PTS lệch giờ thật; vẫn có thể lệch nếu encoder dừng/giật (pacing có cơ chế bỏ qua stall, `encoder_worker.cpp:372`) — cần đo trên file có lỗi thật |
| V4 | `/etc/localtime` = **Asia/Manila (UTC+8)**; tên file và `mtime` cùng `+0800`; epoch trong API đúng (`1791341027` = `02:43:47Z`). API trả `"TimeZone":"Asia/Saigon"` **cố định, sai với máy này** | Chuyển tên file → UTC phải dựa vào múi giờ **hệ thống** của máy (hoặc dùng `mtime`/epoch), không tin trường `TimeZone` của API |
| V5 | File `mp4` thuộc `root:root`, mode `644`; thư mục `755` | `alvis` **đọc được** → replay server chạy bằng `alvis` là đủ, không cần root |
| V6 | Máy có `ffprobe`, `ffmpeg`, `gst-launch-1.0`; **không có `go`** | Replay server phải cross-compile (Go `GOOS=linux GOARCH=arm64`) trên máy dev rồi copy binary. Có `gst-launch-1.0` + `rtspsrc onvif-mode` để kiểm replay ngay trên camera |
| V7 | `RecordPlaybackfilter` với 14373 file mất **2,0 s** (khi cache nguội). Giữ 3,3 ngày dữ liệu (`20261004_023534` → nay), đĩa `/media` 79% (46 GB trống) | Search/Replay không được gọi API này cho mỗi yêu cầu; cần cache chỉ mục riêng, lọc theo khoảng thời gian |
| V8 | Chưa làm — cần đọc `g13.xml` lấy danh sách case Profile G | Còn mở |

**Điều chỉnh sau V3 (đã sửa lại 2026-10-07, bản trước kết luận sai):** DVR gán PTS = số thứ tự frame ÷ FPS (`ffmpeg_out_wrap.cpp:502-514`, đúng theo code), nhưng trên các file đo được, frame đến đều nên PTS khớp giờ thật. Do đó **không bắt buộc sửa DVR** để có NTP từng frame:

1. **Không sửa DVR (đề xuất đợt 1):** NTP của frame = `giờ bắt đầu file + PTS`, với `giờ bắt đầu file = mtime − duration` (khớp log DVR ~50 ms trên 4 file; so với **overlay** thật thì sai số ±1,5 s, xem mục "Vấn đề giờ bắt đầu/kết thúc của DVR" bên dưới). Giới hạn đã biết: nếu encoder giật/dừng giữa file thì NTP lệch tương ứng (chưa quan sát được trường hợp này).
2. **Sửa DVR (tùy chọn, độ chính xác cao hơn):** ghi giờ bắt đầu thật của frame đầu (ms) vào metadata/file phụ, và/hoặc PTS theo giờ thu từng frame. Chạm code DVR → cần chủ sở hữu đồng ý. Chỉ cần nếu đo thấy lệch thực tế.
3. **Cần đo thêm để quyết định:** so `mtime − duration` với giờ thật ở nhiều file hơn, gồm cả alpr (GOP 1 s, file nào cũng bắt đầu ngay) và file có cảnh báo lỗi.

#### Vấn đề giờ bắt đầu/kết thúc của DVR — đã báo team DVR (2026-10-07)

**Phát hiện:** `RecordPlaybackfilter` (và `GetRecordingList`) trả `StartTime`/`EndTime` lệch so với thời điểm hình thật. `StartTime` lấy từ **tên file** = lúc DVR **mở** file (`record_writer.cpp:126`, `file_utils.cpp:134-158`); frame đầu vào file muộn hơn vì DVR bỏ frame P cho tới keyframe đầu (`record_writer.cpp:200-201`); `EndTime = StartTime + duration` (`file_utils.cpp:190-193`) nên lệch theo. `Time` (độ dài) thì đúng.

**Bằng chứng (camera `.194`):**

| Nguồn đối chiếu | Kết quả |
|---|---|
| Đồng hồ **overlay** trên hình, file `ctx_main_20261007_145209` | API 14:52:09 → 14:52:46; hình thật 14:52:12 → 14:52:49 (**sớm 3 s**, cả đầu lẫn cuối) |
| Overlay, file `ctx_main_20261007_145249` | API 14:52:49 → 14:53:15; hình thật 14:53:02 → 14:53:29 (**sớm 13–14 s**) |
| Overlay, file alpr `anpr_main_20261007_143726` (người dùng tự kiểm) | API đúng đến ~2 s (alpr có keyframe mỗi giây nên frame đầu đến ngay) |
| `ctx_main_20261007_103547` (chưa có overlay lúc đó) | API 10:35:47 → 10:36:00; `mtime` 10:36:28, `duration` 13,7 s → hình thật ≈ 10:36:14 → 10:36:28. Lọc API theo 10:36:15–10:36:25 **không trả file**; lọc 10:35:50–10:35:55 **lại trả file này** |

Cơ chế: sai lệch = thời gian chờ keyframe đầu tiên. Context có GOP dài (~50 s trước khi bật overlay) nên lệch 3–27 s; alpr lệch <1 s.

**Độ chính xác của cách tính lại (đối chiếu overlay):** `giờ bắt đầu thật ≈ mtime − duration`, `giờ kết thúc thật ≈ mtime`. Sai số **±1,5 s** (overlay chỉ đọc đến giây; `mtime` là giờ đóng file, có thể sau frame cuối tới ~1 s). *Đính chính:* con số "khớp ~50 ms" ở trên chỉ là so với dòng log DVR, không phải với hình thật.

**Trạng thái:** user đã trao đổi với team DVR và đang nhờ sửa (2026-10-07). Hướng sửa đã đề xuất: (1) `scanRecordFiles` dùng `end = mtime`, `start = mtime − duration` cho file đã đóng; (2) hoặc lưu giờ frame đầu thật (file phụ / đặt tên theo frame đầu). **Chưa biết** team DVR chọn cách nào hay đã sửa chưa — phải đo lại trên bản DVR mới trước khi tin vào API.

**Phạm vi ảnh hưởng tới Profile G** (không chỉ Replay):

| Phần ONVIF | Bị ảnh hưởng? | Vì sao |
|---|---|---|
| Recording Control (`SetRecordingJobMode`, `GetRecordingJobState`, cấu hình job) | **Không** | Chỉ bật/tắt và đọc trạng thái ghi, không dùng giờ file |
| `GetRecordings`, tracks, `GetRecordingOptions` | **Không** | Không dùng giờ file |
| **Search**: `GetRecordingInformation` (`EarliestRecording`/`LatestRecording`), `GetRecordingSummary`, `FindRecordings`, `GetMediaAttributes` | **Có** | Lấy giờ từ chỉ mục file; sai 3–27 s ở context |
| **Search**: `FindEvents` (`IsDataPresent`, ranh giới dữ liệu) | **Có** | Event dựng từ ranh giới file; ranh giới dịch sớm, và khoảng trống thật (30–50 s do chờ keyframe) bị báo sai chỗ |
| **Replay** (`Range: clock`, NTP từng frame, cờ D) | **Có, nặng nhất** | Hình phát ra bị gắn giờ sai nếu tin tên file; lọc theo giờ bỏ sót hoặc trả nhầm file |

Tức vấn đề này không chỉ "phục vụ Replay": Search sai giờ thì VMS vẽ timeline sai và client tin nhầm khoảng có dữ liệu, dù Replay chưa viết.

**Cách xử lý trong `onvif-module` (không phụ thuộc DVR sửa hay chưa):** một hàm duy nhất tính `[start, end]` của mỗi file từ `mtime` và `duration`, dùng chung cho Search và Replay; không dùng `StartTime`/`EndTime` của API. Khi DVR sửa xong và đo lại thấy API đúng (trong ±1,5 s), có thể chuyển sang dùng API nhưng giữ hàm tính lại làm kiểm chứng chéo.

**Điều cần đo lại sau khi DVR sửa:** so `StartTime`/`EndTime` của API với đồng hồ overlay ở frame đầu/cuối cho ít nhất 10 file context + 10 file alpr; kiểm API lọc theo khoảng thời gian trả đúng file (phép thử 10:36:15–10:36:25 ở trên).

**Cấu hình luồng đã đổi sau các lần đo trên (quan sát 15:4x, `.194`):** context main hiện **3840×2160, 30 fps, keyframe mỗi 1 s** (~21 MB/30 s), context sub **1920×1080, 15 fps, keyframe mỗi ~2 s** (~50 MB/30 s). Các số đo độ lệch ở trên (3–27 s) là của cấu hình cũ (640×360, GOP ~50 s). Với GOP ngắn, độ lệch tên file ↔ frame đầu chỉ còn ≤ vài giây (ví dụ `ctx_sub_20261007_154838`: tên 15:48:38, `mtime − duration` = 15:48:40,7), nhưng **vẫn khác 0** nên vẫn cần tính lại từ `mtime`/`duration` hoặc DVR sửa.

**Chuyển luồng main ↔ sub đã quan sát thật (2026-10-07 ~15:48:38, `.194`):** `ctx_main_20261007_154835` đóng sau 2,8 s, `ctx_sub_20261007_154838` mở ngay sau; file sub nằm **cùng thư mục** `/media/records/context` với tiền tố `ctx_sub_`; API trả `StreamType: "sub"`, `VideoSourceId: "0"`. Độ phân giải/codec đổi tại ranh giới (4K → 1080p) nên **không được gộp 2 luồng vào 1 track** (D2). Tại ranh giới có file ngắn (2–3 s) và một khoảng trống ~7 s ở cả context (`ctx_sub_154908` 2 s, rồi `154918`) lẫn alpr (`anpr_main_154856` 15 s, rồi `154918`) quanh 15:49:10 — **nguyên nhân chưa rõ** (có thể do thay đổi cấu hình làm DVR cắt/khởi động lại recorder); cần hỏi team DVR. Người dùng cho biết chuyển luồng hiếm xảy ra trong thực tế.

**Phát hiện phụ (chưa điều tra):** sau khi bật overlay, file context có 3 keyframe (pts 0; 3,4; 4,07 s) và dung lượng tăng mạnh (file mới ~22 MB so với 2–6 MB trước) — có thể cấu hình encoder luồng context đã đổi; cần hỏi team DVR.

**7.1 — Hợp đồng và lớp truy cập DVR**

- Mở rộng `IDvrClient` (hoặc thêm `IRecordingBackend`/`ISearchBackend`/`IReplayBackend` như `02-INTEGRATION_MATRIX.md` đã ghi): `listRecordedFiles(sourceId, from, to)`, `getRecorderStatus()`, `setManualRecord(sourceId, enable)`.
- Cài đặt trong `HttpDvrClient`; timeout và ánh xạ lỗi theo pattern Phase 4 (không fallback mock âm thầm).
- Chuẩn hóa ở một chỗ duy nhất: tên file → UTC (V4), loại file `_EVENT`, bỏ file đang ghi.
- Không sửa `interface/` chung khi chưa đồng bộ với mock backend (quy ước #9 của CLAUDE.md).

**7.2 — Recording Control thật**

- Tách khỏi biến global: tạo `RecordingStore` (interface) với 2 cài đặt: `MockRecordingStore` (hành vi hiện tại, giữ baseline) và `DvrRecordingStore` (D1–D3).
- `RecordingService` hiện 435 dòng; tách handler theo nhóm (config / job) trước khi thêm logic để giữ giới hạn 600 dòng.
- Store job bền vững (D3): JSON ở thư mục ghi được, ghi nguyên tử (chỉ lưu sự tồn tại/cấu hình job). `Mode` luôn đọc từ DVR, không lưu bản sao.
- Event `JobState` / `RecordingConfig` phát khi thay đổi (SOAP hoặc WS `:8210`).

**7.3 — Recording Search thật**

- `SearchSessionStore`: SearchToken, `KeepAliveTime`, hết hạn, dọn dẹp; `EndSearch`; fault đúng cho token sai/hết hạn.
- `FindRecordings`: lọc theo Scope (`IncludedRecordings`, `RecordingInformationFilter`), trả `RecordingInformation` từ chỉ mục.
- `FindEvents`: dựng event ảo từ ranh giới file (bảng ánh xạ ở trên), sắp theo thời gian, `IncludeStartState`, `MaxMatches`.
- Cache chỉ mục phía onvif-module để mỗi phiên tìm kiếm không quét lại nhiều file (DVR cache 10 giây).

**7.4 — Replay RTSP server (phần khó nhất)**

- Process Go + gortsplib (`alvis-onvif-replay`), systemd riêng, cổng cấu hình trong `onvif.conf`.
- Luồng một phiên (đã mô tả trong chat 2026-10-07):
  1. `DESCRIBE` có `Require: onvif-replay` → SDP kèm `a=x-onvif-track:<TrackToken>`.
  2. `PLAY` với `Range: clock=<UTC>-<UTC>` (đóng/mở), `Rate-Control` (bắt buộc hỗ trợ `no`), `Frames: intra|all`, `Scale`, `Immediate: yes`.
  3. Tra chỉ mục (D5) → các file giao với khoảng thời gian; mở file đầu, **tìm keyframe tại hoặc trước** mốc bắt đầu bằng bảng sample MP4.
  4. Đọc sample (không giải mã lại) → đóng gói RTP H.264/H.265, `PayloadMaxSize` 1434.
  5. Gắn extension `0xABAC` (độ dài 3, NTP 8 byte, cờ C/E/D/T, CSeq byte thấp) lên **gói đầu của mỗi access unit**; NTP = giờ bắt đầu file + (PTS − PTS đầu), phải đơn điệu tăng.
  6. Hết file → sang file kế; có khoảng trống → đặt cờ D ở gói đầu của đoạn mới; hết dữ liệu → **ngừng gửi, không PAUSE**.
  7. `PAUSE` bắt buộc; transport RTP/UDP và RTP/RTSP/TCP; WebSocket không làm.
- Mỗi phiên có trạng thái replay riêng (mock đang dùng 1 `replayState` chung cho cả handler — không dùng được khi nhiều client).
- Thư viện đọc MP4: chọn thuần Go (không cgo) để cross-build dễ — **[chưa verify]** cần chọn và thử trên file DVR thật ở bước V2.
- Xác thực (D6) và sửa tunnel: `proxyRtspHttpTunnel` chọn đích theo path (`/replay…` → cổng replay, còn lại → `cfg_.rtspPort`).
- Tái dùng từ mock: `parseClockRange`, `ntpTimestamp`, dựng extension, `Immediate`/cờ D. Thay hoàn toàn nguồn dữ liệu (không còn relay live).

**7.5 — Nối `ReplayService` và cấu hình**

- `GetReplayUri` kiểm `RecordingToken` với `RecordingStore`, trả URI dùng cổng từ config; fault `ter:NoRecording` khi không tồn tại; fault `ter:InvalidStreamSetup` nếu StreamSetup không hỗ trợ.
- Khai báo capability đúng: `ReversePlayback=false`, `RTP_RTSP_TCP=true`, `RTSPOverWebSocket=false`.
- `onvif.conf`: thêm khóa cho cổng/binary replay; bật từng cờ `recording/search/replay = real` độc lập (hybrid).

**7.6 — Kiểm thử**

- Chuẩn bị archive: ghi lịch ≥ vài chục phút, có **ít nhất 1 khoảng trống** (dừng rồi ghi lại), có ranh giới giữa 2 file, có file `_EVENT`.
- DTT Profile G trên backend thật; mock phải giữ 313/313 (chạy lại sau mỗi thay đổi).
- Kiểm tra giao thức replay ngoài DTT: `gst-launch-1.0` với `rtspsrc onvif-mode=true` + `rtponvifparse`, Wireshark (kiểm `Require`, `Range`, extension `0xABAC`, cờ D), OxDM, Happytime ONVIF Client.
- Kiểm tra bằng VMS thật: phát lại đúng khoảng UTC, qua ranh giới file, qua khoảng trống, tua, PAUSE.
- Kiểm tra độ bền: restart DVR/onvif-module/replay server khi đang replay; tháo đĩa.

**7.7 — Tài liệu**

- Cập nhật `02-INTEGRATION_MATRIX.md` (các hàng Recording/Search/Replay) khi từng capability đạt gate; không đánh `REAL_VERIFIED` nếu mới compile/smoke test.

### Rủi ro và giới hạn đã biết

- **File đang ghi chưa đọc được** (`+faststart`): vùng thời gian mới nhất (tối đa một đoạn `FileDuration`) không replay được; `LatestRecording` luôn trễ. Giải pháp triệt để (MP4 phân mảnh) cần sửa DVR — ngoài phạm vi đợt 1, cần chủ sở hữu DVR đồng ý.
- **Độ chính xác khoảng 1 giây** (giờ từ tên file, thời lượng số giây nguyên): khoảng trống dưới 1 giây không phát hiện được.
- **Giờ bắt đầu file trong tên file không phải giờ frame đầu (đã xác nhận trên `.194`, V3)**: tên = lúc mở segment; frame đầu đến sau khi chờ keyframe (5–26 s ở context). Phải dùng `mtime − duration`. Nếu bỏ sót, replay lệch 5–26 s và Search báo sai khoảng có dữ liệu. Đây là rủi ro đúng-sai lớn nhất của Phase 7, nhưng sửa được hoàn toàn ở `onvif-module`.
- **Chỉ mục của DVR (`RecordPlaybackfilter`) sai giờ bắt đầu/kết thúc** vì lấy `start` từ tên file; không dùng thẳng, tính lại từ `mtime`/`duration`.
- **PTS theo số đếm**: đúng khi frame đến đều (đã kiểm), có thể lệch nếu encoder giật giữa file. Chưa gặp, cần đo thêm; nếu gặp mới cần sửa DVR.
- **GOP rất dài ở context (~50,6 s)**: DVR bỏ toàn bộ frame P trước keyframe đầu nên mỗi lần mở file mất 5–26 s hình, và segment không có keyframe bị xóa (`REC-EMPTY`). Đó là mất dữ liệu ở **phía ghi** của DVR (cần chủ DVR biết), không phải việc của ONVIF; replay chỉ phản ánh đúng khoảng trống. Điểm vào replay là đầu file nên có thể lùi tới vài chục giây so với mốc yêu cầu (spec cho phép bắt đầu ở clean point trước mốc).
- **Múi giờ**: tên file là giờ địa phương; API DVR gán cứng `Asia/Saigon`. Sai múi giờ tiến trình → lệch toàn bộ Range.
- **`Id` của DVR không ổn định**; mọi token ONVIF phải dựa vào `VideoSourceId` và giờ/tên file.
- **Retention**: DVR xóa file khi đĩa đầy không báo; Search phải phản ánh theo lần quét sau. Event `DataDeletion` là conditional → không làm đợt 1.
- **Tài nguyên**: replay đọc đĩa trong khi DVR đang ghi; CPU máy dùng chung. Giới hạn số phiên replay đồng thời (đề xuất cấu hình, mặc định thấp).
- **Phụ thuộc Phase 6**: event Recording dùng chung `MockSubscriptionManager`; nếu Phase 6 đổi cách phát event thì cập nhật cùng lúc.
- **DTT cần dữ liệu**: các case Replay/Search đòi recording có dữ liệu thật tại thời điểm test.

### Ngoài phạm vi đợt 1

Audio và metadata recording, nhiều track (main + sub), dynamic recording/track (`CreateRecording`, `CreateTrack`, `DeleteTrack`), Receiver làm nguồn, reverse playback, RTSP qua WebSocket, event `DataDeletion`, tìm event metadata lịch sử từ Core. Khai báo đúng trong capabilities để DTT đánh dấu N/A thay vì fail.

### Gate

- [ ] 7.0 hoàn tất (V1–V7 đã đo trên `.194` 2026-10-07; còn V8 và xác nhận lại trên `.102`); D1–D6 đã được user chốt; đo thêm độ lệch `mtime − duration` so với giờ thật trên nhiều file (kể cả alpr) để quyết định có cần sửa DVR không.
- [ ] `recording = real`: GetRecordings/Jobs/State/Mode khớp trạng thái ghi thật của DVR; Mode bền qua restart onvif-module và DVR.
- [ ] `search = real`: token lifecycle đúng (KeepAlive, hết hạn, `EndSearch`); `FindRecordings`/`FindEvents` khớp danh sách file thật.
- [ ] `replay = real`: VMS phát lại **đúng khoảng UTC** từ archive thật, qua ranh giới file và khoảng trống; Wireshark xác nhận `0xABAC`, cờ D, `Immediate`, `Rate-Control: no`.
- [ ] Recording/Search/Replay token nhất quán (`rec_<id>` dùng xuyên suốt).
- [ ] DTT Profile G pass với archive thật; mock vẫn 313/313.
- [ ] Không có replay payload đi qua onvif-server; tunnel RTSP-over-HTTP định tuyến đúng cổng replay.
- [ ] `02-INTEGRATION_MATRIX.md` cập nhật kèm evidence.

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
