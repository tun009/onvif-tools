# Refactor Plan — Kiến trúc scale cho Multi-Profile (M / G / A)

> 📅 Tạo: 09/07/2026
> 🎯 Mục tiêu: Refactor routing của `onvif-module` từ God Object sang Service Registry, để mở rộng Profile M / G / A mà **không sửa core**.

---

## 1. Vấn đề hiện tại

Sau khi Profile T pass 204/210, code lộ ra 6 điểm yếu khi chuẩn bị thêm Profile M/G/A:

| # | Vấn đề | File | Hậu quả khi scale |
|---|--------|------|-------------------|
| 1 | **God Object routing** — 1 khối if-else route mọi request | `OnvifServer.cpp` (~100 dòng if-else) | Mỗi profile mới phải sửa file core |
| 2 | **3 dispatch pattern không nhất quán** | Media2Service (gSOAP binding) / MediaLegacyHandler (manual XML) / MockSubscriptionManager (singleton) | Dev phải học 3 kiểu, không có convention |
| 3 | **God files 1000+ dòng** | Media2Service.cpp (1155), DeviceService.cpp (1140), MediaLegacyHandler.cpp (990) | Không maintainable |
| 4 | **Auth nhúng trong routing** | `OnvifServer.cpp` dòng ~316-373 | Mỗi profile phải nhớ pass qua auth |
| 5 | **Fault build bằng string concat** | Rải rác nhiều nơi | Lặp code, khó đồng bộ |
| 6 | **Không có Service Registry** | — | Muốn biết support service nào phải đọc if-else |

### 1.1. Minh họa vấn đề God Object

Khi thêm đủ Profile M/G/A, `OnvifServer.cpp` sẽ thành:

```cpp
if (path.find("/onvif/deviceIO")) {...}
if (path.find("/onvif/media")) {...}
else if (path.find("/onvif/imaging")) {...}
else if (path.find("/onvif/analytics")) {...}    // ← Profile M
else if (path.find("/onvif/recording")) {...}    // ← Profile G
else if (path.find("/onvif/search")) {...}        // ← Profile G
else if (path.find("/onvif/replay")) {...}        // ← Profile G
else if (path.find("/onvif/accesscontrol")) {...} // ← Profile A
else if (path.find("/onvif/doorcontrol")) {...}   // ← Profile A
else {...}
```

→ 10+ nhánh, file core phình lên, mỗi thêm profile = sửa chỗ nguy hiểm nhất.

---

## 2. Kiến trúc đề xuất — Service Registry

### 2.1. Sơ đồ Before / After

**BEFORE** (hiện tại):
```
                  ┌──────────────────────────────┐
   Request ─────► │  OnvifServer.cpp (God Object)│
                  │  ┌────────────────────────┐  │
                  │  │ if /media   → media2   │  │
                  │  │ if /imaging → imaging  │  │
                  │  │ if /event   → event    │  │
                  │  │ else        → device   │  │
                  │  │ + auth check inline    │  │
                  │  │ + fault build inline   │  │
                  │  └────────────────────────┘  │
                  └──────────────────────────────┘
```

**AFTER** (đề xuất):
```
   Request ─► OnvifServer ─► AuthMiddleware ─► ServiceRegistry.route(path)
                                                       │
                          ┌────────────────────────────┼─────────────────┐
                          ▼            ▼            ▼            ▼         ▼
                    DeviceService Media2Service AnalyticsSvc RecordingSvc ...
                    (IOnvifService)(IOnvifService)(IOnvifService) ...
                          │
                          └─► FaultBuilder (khi lỗi)
```

OnvifServer chỉ còn: nhận request → auth → registry route → gửi response. **~15 dòng thay 100 dòng if-else.**

### 2.2. Interface mới — `IOnvifService`

File mới: `include/services/IOnvifService.h`

```cpp
#pragma once
#include <string>
struct soap;

// Interface chung cho MỌI ONVIF service (Device, Media2, Analytics, Recording...)
class IOnvifService {
public:
    virtual ~IOnvifService() = default;

    // URL path service này phụ trách, VD "/onvif/analytics"
    virtual std::string pathPrefix() const = 0;

    // Xử lý request → trả về SOAP response XML.
    // Trả "" nếu service không nhận diện được operation (để fallback).
    virtual std::string handle(struct soap* soap,
                                const std::string& rawRequest) = 0;

    // Service này có yêu cầu authentication không (đa số = true)
    virtual bool requiresAuth() const { return true; }

    // Tên service để log/debug
    virtual std::string name() const = 0;
};
```

### 2.3. Service Registry

File mới: `include/services/ServiceRegistry.h`

```cpp
#pragma once
#include "services/IOnvifService.h"
#include <memory>
#include <vector>
#include <string>

class ServiceRegistry {
public:
    // Đăng ký 1 service (gọi trong main.cpp)
    void registerService(std::unique_ptr<IOnvifService> svc);

    // Tìm service phụ trách path. Trả nullptr nếu không có.
    IOnvifService* route(const std::string& path) const;

    // Liệt kê service đã đăng ký (cho GetServices)
    std::vector<IOnvifService*> all() const;

private:
    std::vector<std::unique_ptr<IOnvifService>> services_;
};
```

File mới: `src/services/ServiceRegistry.cpp`

```cpp
#include "services/ServiceRegistry.h"

void ServiceRegistry::registerService(std::unique_ptr<IOnvifService> svc) {
    services_.push_back(std::move(svc));
}

IOnvifService* ServiceRegistry::route(const std::string& path) const {
    // Longest-prefix match để tránh nhầm /onvif/media vs /onvif/media2
    IOnvifService* best = nullptr;
    size_t bestLen = 0;
    for (const auto& svc : services_) {
        const auto& prefix = svc->pathPrefix();
        if (path.find(prefix) != std::string::npos && prefix.size() > bestLen) {
            best = svc.get();
            bestLen = prefix.size();
        }
    }
    return best;
}

std::vector<IOnvifService*> ServiceRegistry::all() const {
    std::vector<IOnvifService*> out;
    for (const auto& s : services_) out.push_back(s.get());
    return out;
}
```

### 2.4. AuthMiddleware — tách auth khỏi routing

File mới: `include/auth/AuthMiddleware.h`

```cpp
#pragma once
#include <string>
struct soap;

class AuthMiddleware {
public:
    AuthMiddleware(std::string username, std::string password);

    // Kiểm tra auth (HTTP Digest hoặc WS-Security UsernameToken).
    // Trả true nếu OK. Nếu fail, tự gửi 401 challenge / SOAP fault qua soap.
    // Caller check return value → nếu false thì skip dispatch.
    bool authenticate(struct soap* soap,
                      const std::string& headers,
                      bool serviceRequiresAuth);

private:
    std::string username_, password_;
};
```

Logic bên trong = rút từ `OnvifServer.cpp` dòng 316-373 hiện tại (HTTP Digest + WS-Security), gói vào 1 class.

### 2.5. FaultBuilder — thay string concat

File mới: `include/utils/FaultBuilder.h`

```cpp
#pragma once
#include <string>

// Build SOAP 1.2 fault XML với ONVIF subcode chuẩn.
class FaultBuilder {
public:
    // env:Sender fault với subcode 2 tầng (VD ter:InvalidArgVal/ter:NoProfile)
    static std::string sender(const std::string& subcode1,
                              const std::string& subcode2,
                              const std::string& reason);

    // env:Receiver fault (server-side error)
    static std::string receiver(const std::string& subcode1,
                                const std::string& reason);

    // Shortcut cho các fault hay dùng
    static std::string notAuthorized();
    static std::string actionNotSupported();
    static std::string invalidArgVal(const std::string& detail);
    static std::string noProfile();
};
```

Thay toàn bộ đoạn string concat `<SOAP-ENV:Fault>...` rải rác trong OnvifServer + các service.

---

## 3. OnvifServer sau refactor

`OnvifServer.cpp` — phần dispatch rút gọn:

```cpp
// Setup (trong constructor / init)
ServiceRegistry registry_;
AuthMiddleware auth_(cfg_.username, cfg_.password);

// Trong serve loop, sau khi soap_begin_serve + lấy path:
std::string path = soap->path;

// 1. Route
IOnvifService* svc = registry_.route(path);
if (!svc) {
    sendRaw(soap, FaultBuilder::actionNotSupported());
    continue;
}

// 2. Auth
if (!auth_.authenticate(soap, g_current_headers, svc->requiresAuth())) {
    continue;  // middleware đã gửi 401/fault
}

// 3. Dispatch
std::string resp = svc->handle(soap, g_current_headers);
if (resp.empty()) {
    sendRaw(soap, FaultBuilder::actionNotSupported());
} else {
    sendRaw(soap, resp);
}
```

→ **~20 dòng**, không phụ thuộc số lượng service. Thêm Profile M/G/A không đụng đây.

---

## 4. Thêm Profile mới sau refactor — chỉ 2 bước

### Bước 1: Tạo service file mới

`src/services/AnalyticsService.cpp` (Profile M):
```cpp
class AnalyticsService : public IOnvifService {
public:
    AnalyticsService(std::shared_ptr<ICameraBackend> b) : backend_(b) {}

    std::string pathPrefix() const override { return "/onvif/analytics"; }
    std::string name() const override { return "AnalyticsService"; }

    std::string handle(struct soap* soap, const std::string& req) override {
        if (req.find("GetSupportedAnalyticsModules") != npos)
            return handleGetSupportedModules(req);
        if (req.find("GetAnalyticsModules") != npos)
            return handleGetModules(req);
        if (req.find("CreateAnalyticsModules") != npos)
            return handleCreateModules(req);
        // ...
        return "";  // không nhận diện → fallback
    }

private:
    std::shared_ptr<ICameraBackend> backend_;
    // handler methods...
};
```

### Bước 2: Đăng ký 1 dòng trong main.cpp

```cpp
registry.registerService(std::make_unique<AnalyticsService>(backend));
// ← DUY NHẤT 1 dòng. KHÔNG đụng OnvifServer.cpp
```

**Đó là tất cả.** Profile G/A tương tự.

---

## 5. Migration Steps — làm dần, không phá 204/210

> ⚠️ KHÔNG refactor toàn bộ 1 lần — risk phá test đang pass. Làm từng bước, test sau mỗi bước.

### Phase 0 — Chuẩn bị (0.5 ngày)
- [ ] Branch mới `refactor/service-registry`
- [ ] Backup binary hiện tại (đang pass 204/210) làm reference
- [ ] Snapshot result72 làm baseline so sánh

### Phase 1 — Tạo scaffolding, chưa dùng (1 ngày)
- [ ] Tạo `IOnvifService.h`
- [ ] Tạo `ServiceRegistry.h/.cpp`
- [ ] Tạo `FaultBuilder.h/.cpp`
- [ ] Tạo `AuthMiddleware.h/.cpp` (copy logic từ OnvifServer, chưa wire)
- [ ] Build OK (chưa đổi hành vi) → test vẫn 204/210

### Phase 2 — Wrap 1 service pilot (1 ngày)
- [ ] Chọn service đơn giản nhất làm pilot: **DeviceIOHandler** (chỉ 108 dòng)
- [ ] Cho `DeviceIOHandler` implement `IOnvifService`
- [ ] Đăng ký vào registry
- [ ] Trong OnvifServer: route `/onvif/deviceIO` qua registry (giữ nguyên các path khác)
- [ ] Test → DeviceIO tests vẫn pass

### Phase 3 — Migrate lần lượt các service (3-4 ngày)
Thứ tự từ đơn giản → phức tạp, test sau MỖI service:
- [ ] ImagingService → registry (test IMAGING-* pass)
- [ ] MediaLegacyHandler → registry (test Profile S pass)
- [ ] EventService/MockSubscriptionManager → registry (test EVENT-* pass)
- [ ] Media2Service → registry (test MEDIA2-* pass) ← phức tạp nhất, làm cuối
- [ ] DeviceService → registry (test DEVICE-* pass)

### Phase 4 — Tách Auth + Fault (2 ngày)
- [ ] Wire `AuthMiddleware` vào serve loop, xóa auth inline khỏi OnvifServer
- [ ] Thay mọi string-concat fault bằng `FaultBuilder`
- [ ] Test full suite → vẫn 204/210

### Phase 5 — Dọn dẹp (1 ngày)
- [ ] Xóa if-else routing cũ trong OnvifServer
- [ ] Split các God file >800 dòng nếu còn (tách handler theo nhóm operation)
- [ ] Update `docs/02-kien-truc-du-an.md` với kiến trúc mới
- [ ] Merge về main sau khi confirm 204/210

**Tổng: ~9-10 ngày** (2 tuần với buffer).

---

## 6. Bảng so sánh Before / After

| Tiêu chí | Before | After |
|----------|--------|-------|
| Thêm profile mới | Sửa OnvifServer God Object | Thêm file + 1 dòng registry |
| Dispatch pattern | 3 kiểu lộn xộn | 1 interface `IOnvifService` |
| Auth | Nhúng trong serve loop | `AuthMiddleware` tách riêng |
| Fault | String concat rải rác | `FaultBuilder` tập trung |
| Test 1 service | Chạy cả server | Test isolated per service |
| Max file size | 1155 dòng | ~300-400 dòng/service |
| OnvifServer dispatch | ~100 dòng if-else | ~20 dòng |
| Scale M/G/A | Đau, dễ vỡ | Sạch, không đụng core |

---

## 7. Files thay đổi — tóm tắt

### Tạo mới
```
include/services/IOnvifService.h        ← interface chung
include/services/ServiceRegistry.h
src/services/ServiceRegistry.cpp
include/auth/AuthMiddleware.h
src/auth/AuthMiddleware.cpp
include/utils/FaultBuilder.h
src/utils/FaultBuilder.cpp
```

### Sửa
```
src/OnvifServer.cpp        ← xóa if-else + auth inline, dùng registry
src/main.cpp              ← đăng ký services vào registry
src/services/*.cpp        ← mỗi service implement IOnvifService (thêm 3 method)
```

### Không đụng
```
include/interface/         ← contract với backend GIỮ NGUYÊN
include/interface/types/   ← struct types GIỮ NGUYÊN
generated/                 ← gSOAP generated code GIỮ NGUYÊN
```

---

## 8. Rủi ro & giảm thiểu

| Rủi ro | Giảm thiểu |
|--------|-----------|
| Phá 204/210 trong lúc migrate | Test sau MỖI service, không migrate hàng loạt |
| gSOAP dispatch (Media2) khó wrap vào interface | Giữ `svc.dispatch()` bên trong `handle()`, chỉ đổi cách gọi |
| Longest-prefix match sai (media vs media2) | Unit test riêng cho `ServiceRegistry::route()` |
| Auth middleware bỏ sót edge case | Copy nguyên logic cũ, chỉ đổi vị trí, không viết lại |
| Regression khó trace | Branch riêng, so sánh result XML từng phase với baseline |

---

## 9. Quyết định cần chốt trước khi làm

1. **Dispatch pattern chuẩn**: chọn `manual XML` (như MediaLegacyHandler) hay giữ `gSOAP binding` (Media2Service)?
   - Đề xuất: **manual XML** — dễ kiểm soát, không phụ thuộc gSOAP binding phức tạp, đồng nhất với hướng Profile M sẽ làm.
2. **Thời điểm refactor**: trước hay sau khi làm Profile M?
   - Đề xuất: **TRƯỚC** — rẻ hơn refactor khi đã có 4-5 profile chồng chất.
3. **Có giữ backward-compat 2 pattern** trong giai đoạn chuyển tiếp không?
   - Đề xuất: **Có** — registry route được, service nào chưa migrate vẫn chạy pattern cũ, migrate dần.

---

## 10. Kết luận

- Kiến trúc hiện tại **chạy tốt cho Profile T** nhưng **không scale** cho M/G/A.
- Refactor **Service Registry + AuthMiddleware + FaultBuilder** giải quyết triệt để: thêm profile = thêm file, không đụng core.
- Làm **dần theo 6 phase**, test sau mỗi bước → không phá 204/210.
- Đầu tư ~2 tuần bây giờ, tiết kiệm rất nhiều khi thêm 4-5 profile sau.

> 💡 Nguyên tắc: "Refactor sớm 1 profile, tiết kiệm 5 profile sau."

---

# PHẦN II — CẤU TRÚC CHUẨN SAU REFACTOR

> ⭐ **Đây là tiêu chuẩn kiến trúc bắt buộc.** Mọi AI agent / developer khi thêm code vào `onvif-module` PHẢI tuân theo cấu trúc và quy ước dưới đây. Đọc kỹ trước khi tạo/sửa file.

---

## 11. Cây thư mục đầy đủ sau refactor

```
onvif-module/onvif-module/
│
├── Makefile                         # Build script (thêm file mới → thêm vào SRCS)
├── config/
│   └── onvif.conf                   # Cấu hình runtime: IP, port, username, password
│
├── external/
│   └── gsoap/
│       └── typemap.dat              # Mapping namespace prefix cho gSOAP (KHÔNG sửa tay)
│
├── scripts/                         # Script build gSOAP, deploy
│
├── include/
│   │
│   ├── OnvifServer.h                # [CORE] Khai báo HTTP server + serve loop
│   │
│   ├── core/                        # ⭐ MỚI — hạ tầng dùng chung
│   │   ├── IOnvifService.h          #   Interface chung cho MỌI service
│   │   ├── ServiceRegistry.h        #   Đăng ký + route service theo path
│   │   └── ServiceContext.h         #   Bọc dependency chung (backend, config) tiêm vào service
│   │
│   ├── auth/                        # Xác thực (độc lập với routing)
│   │   ├── AuthMiddleware.h         # ⭐ MỚI — orchestrate auth trước dispatch
│   │   ├── DigestAuthHandler.h      #   HTTP Digest (RFC 2617)
│   │   └── WsSecurityHandler.h      #   WS-Security UsernameToken
│   │
│   ├── utils/                       # Tiện ích dùng chung
│   │   ├── FaultBuilder.h           # ⭐ MỚI — build SOAP fault chuẩn ONVIF
│   │   ├── SoapResponse.h           # ⭐ MỚI — helper gửi raw XML qua gSOAP
│   │   └── SimpleJson.h             #   Parse/build JSON (dùng cho IPC)
│   │
│   ├── backend/                     # Cầu nối tới camera backend (mock/real DVR)
│   │   └── BackendConnector.h       #   IPC client qua Unix socket
│   │
│   ├── interface/                   # ⚠️ SHARED CONTRACT — KHÔNG tự ý sửa
│   │   ├── ICameraBackend.h         #   30 virtual methods (hợp đồng với backend)
│   │   ├── ipc/IpcProtocol.h        #   Format message IPC
│   │   └── types/                   #   Struct data (mapping từ ONVIF XSD)
│   │       ├── DeviceTypes.h
│   │       ├── MediaTypes.h
│   │       ├── ImagingTypes.h
│   │       ├── PtzTypes.h
│   │       ├── EventTypes.h
│   │       ├── AnalyticsTypes.h
│   │       └── MetadataTypes.h      # ⭐ MỚI (Profile M) — struct metadata config
│   │
│   └── services/                    # Mỗi ONVIF service 1 file header
│       ├── DeviceService.h          #   Profile T,S,M — device mgmt, network, users
│       ├── Media2Service.h          #   Profile T,M — Media2 (ver20)
│       ├── MediaLegacyHandler.h     #   Profile S — Media1 (ver10)
│       ├── ImagingService.h         #   Profile T — imaging settings
│       ├── EventService.h           #   Profile T,M — event subscription
│       ├── DeviceIOHandler.h        #   Profile T — video source I/O
│       ├── DiscoveryService.h       #   WS-Discovery
│       ├── PtzService.h             #   Conditional — PTZ
│       ├── AnalyticsService.h       # ⭐ Profile M — analytics module + metadata
│       ├── RecordingService.h       # ⭐ Profile G (tương lai)
│       ├── SearchService.h          # ⭐ Profile G (tương lai)
│       └── ReplayService.h          # ⭐ Profile G (tương lai)
│
└── src/                             # Implementation (.cpp) — gương của include/
    ├── main.cpp                     # Entry: tạo backend, đăng ký services, chạy server
    ├── OnvifServer.cpp              # [CORE] serve loop — route qua registry
    │
    ├── core/
    │   ├── ServiceRegistry.cpp
    │   └── ServiceContext.cpp
    ├── auth/
    │   ├── AuthMiddleware.cpp
    │   ├── DigestAuthHandler.cpp
    │   └── WsSecurityHandler.cpp
    ├── utils/
    │   ├── FaultBuilder.cpp
    │   └── SoapResponse.cpp
    ├── backend/
    │   └── BackendConnector.cpp
    └── services/
        ├── DeviceService.cpp
        ├── Media2Service.cpp
        ├── MediaLegacyHandler.cpp
        ├── ImagingService.cpp
        ├── EventService.cpp
        ├── DeviceIOHandler.cpp
        ├── DiscoveryService.cpp
        ├── PtzService.cpp
        └── AnalyticsService.cpp     # ⭐ Profile M
```

> Ghi chú: `⭐ MỚI` = file tạo trong đợt refactor. `⚠️` = vùng cấm sửa tự do.

---

## 12. Giải thích chức năng & ý nghĩa từng file

### 12.1. Tầng CORE — hạ tầng, không chứa logic ONVIF cụ thể

| File | Chức năng | Ý nghĩa / khi nào đụng |
|------|-----------|------------------------|
| `OnvifServer.h/.cpp` | HTTP server (bind port 8080), serve loop, gọi middleware + registry | **Hiếm khi sửa.** Chỉ đụng khi đổi hạ tầng HTTP/transport. Thêm profile KHÔNG đụng file này. |
| `core/IOnvifService.h` | Interface chung: `pathPrefix()`, `handle()`, `requiresAuth()`, `name()` | **Contract của mọi service.** Mọi service PHẢI implement. Đổi interface = ảnh hưởng toàn bộ service → cân nhắc kỹ. |
| `core/ServiceRegistry.h/.cpp` | Lưu danh sách service, route request theo longest-prefix path | Sửa khi đổi thuật toán routing. Thêm service KHÔNG sửa file này (chỉ gọi `registerService`). |
| `core/ServiceContext.h/.cpp` | Gói dependency chung (`backend`, `config`, `deviceIp`, `httpPort`) để tiêm vào service qua constructor | Thêm dependency dùng chung → thêm field ở đây. Tránh mỗi service tự lấy global. |

### 12.2. Tầng AUTH — xác thực, tách khỏi routing

| File | Chức năng | Ý nghĩa / khi nào đụng |
|------|-----------|------------------------|
| `auth/AuthMiddleware.h/.cpp` | Orchestrate: gọi trước dispatch. Chọn HTTP Digest hoặc WS-Security tùy request. Gửi 401/fault nếu fail | Sửa khi đổi policy auth (VD thêm token type mới). Service KHÔNG tự làm auth — middleware lo. |
| `auth/DigestAuthHandler.h/.cpp` | Validate HTTP Digest header (nonce, response hash MD5) | Sửa khi đổi thuật toán digest. |
| `auth/WsSecurityHandler.h/.cpp` | Validate WS-Security UsernameToken (PasswordDigest SHA1 + Nonce + Created) | Sửa khi đổi WS-Security handling. |

### 12.3. Tầng UTILS — tiện ích dùng chung

| File | Chức năng | Ý nghĩa / khi nào đụng |
|------|-----------|------------------------|
| `utils/FaultBuilder.h/.cpp` | Build SOAP 1.2 fault XML với ONVIF subcode chuẩn (ter:InvalidArgVal/ter:NoProfile...) | **Dùng MỌI NƠI cần trả fault.** Cấm string-concat fault thủ công. Thêm loại fault mới → thêm static method ở đây. |
| `utils/SoapResponse.h/.cpp` | Helper gửi raw XML response qua gSOAP (`soap_response` + `soap_send_raw` + `soap_end_send`) | Dùng khi service trả XML thủ công. Tránh lặp boilerplate gSOAP. |
| `utils/SimpleJson.h` | Parse/build JSON (header-only) | Dùng cho IPC payload với backend. |

### 12.4. Tầng BACKEND — cầu nối camera

| File | Chức năng | Ý nghĩa / khi nào đụng |
|------|-----------|------------------------|
| `backend/BackendConnector.h/.cpp` | IPC client: serialize request → gửi Unix socket → nhận response → deserialize. Implement `ICameraBackend`. **Cần thêm auto-reconnect** (xem sự cố stale socket) | Sửa khi đổi transport IPC hoặc thêm health-check/reconnect. |

### 12.5. Tầng INTERFACE — ⚠️ SHARED CONTRACT

| File | Chức năng | Ý nghĩa / khi nào đụng |
|------|-----------|------------------------|
| `interface/ICameraBackend.h` | 30 virtual methods — hợp đồng giữa ONVIF module ↔ backend (mock + real DVR) | ⚠️ **Sửa = ảnh hưởng CẢ 2 repo.** Thêm method mới phải sync với mock-camera-backend + real DVR. Thêm Profile M → thêm method metadata/analytics ở đây. |
| `interface/types/*.h` | Struct data mapping từ ONVIF XSD (StreamProfile, VideoEncoderConfig...) | ⚠️ Sync 2 repo. Thêm field theo XSD, không tự chế field. |
| `interface/ipc/IpcProtocol.h` | Format message IPC (MsgType enum, Message struct) | Thêm operation IPC mới → thêm MsgType. Sync 2 repo. |

### 12.6. Tầng SERVICES — logic ONVIF cụ thể

Mỗi file = 1 ONVIF service, implement `IOnvifService`. **Đây là nơi 90% code mới được thêm.**

| File | Profile | Chức năng |
|------|---------|-----------|
| `DeviceService` | T,S,M | GetDeviceInformation, network config, users, reboot, GetServices |
| `Media2Service` | T,M | GetProfiles, VideoEncoder/Source config, streaming URI, OSD (Media2 ver20) |
| `MediaLegacyHandler` | S | Media1 (ver10) ops cho Profile S backward-compat |
| `ImagingService` | T | GetImagingSettings/Set, focus, options |
| `EventService` | T,M | Subscribe, PullMessages, notification |
| `DeviceIOHandler` | T | GetVideoSources (Device I/O) |
| `DiscoveryService` | all | WS-Discovery multicast responder |
| `PtzService` | conditional | PTZ move/status/preset |
| `AnalyticsService` ⭐ | M | GetSupportedAnalyticsModules, metadata config, analytics module CRUD |

---

## 13. Quy ước bắt buộc cho AI agents / developers

> Đọc kỹ trước khi thêm/sửa code. Vi phạm = phá kiến trúc.

### QĐ-1: Thêm ONVIF operation vào service ĐÃ CÓ
- Sửa file service tương ứng trong `src/services/`
- Thêm nhánh nhận diện trong method `handle()`:
  ```cpp
  if (req.find("TênOperationMới") != std::string::npos)
      return handleTênOperationMới(req);
  ```
- Viết private method `handleXxx()` trả về XML string
- **KHÔNG** đụng `OnvifServer.cpp`

### QĐ-2: Thêm ONVIF SERVICE mới (VD Profile G Recording)
1. Tạo `include/services/RecordingService.h` — kế thừa `IOnvifService`
2. Tạo `src/services/RecordingService.cpp` — implement 4 method interface
3. Thêm vào `Makefile` (SRCS)
4. Đăng ký 1 dòng trong `main.cpp`:
   ```cpp
   registry.registerService(std::make_unique<RecordingService>(ctx));
   ```
5. **KHÔNG** đụng `OnvifServer.cpp`, `ServiceRegistry.cpp`

### QĐ-3: Trả SOAP fault
- **LUÔN** dùng `FaultBuilder`, VD `FaultBuilder::noProfile()`
- **CẤM** viết string `<SOAP-ENV:Fault>...` thủ công

### QĐ-4: Xác thực
- **KHÔNG** tự check auth trong service
- Set `requiresAuth()` return true/false → `AuthMiddleware` tự lo
- Op public (GetDeviceInformation, GetServices...) → không cần auth ở tầng service, đã config sẵn

### QĐ-5: Gọi backend
- **CHỈ** qua `backend_->method()` (từ `ICameraBackend`)
- **CẤM** service tự mở socket / gọi hardware trực tiếp
- Nếu cần method backend mới → thêm vào `ICameraBackend.h` + sync mock + real DVR

### QĐ-6: File size
- Service > 600 dòng → tách handler theo nhóm operation ra file phụ (VD `Media2Service_OSD.cpp`)
- Không để God file 1000+ dòng

### QĐ-7: Naming convention
- Service class: `XxxService` (PascalCase)
- Handler method: `handleXxx()` (camelCase, khớp tên ONVIF operation)
- File: khớp tên class (`AnalyticsService.h` ↔ `class AnalyticsService`)

### QĐ-8: Interface types
- Struct mới cho Profile M/G → đặt trong `interface/types/` theo domain (MetadataTypes, RecordingTypes...)
- Field mapping đúng ONVIF XSD, không tự chế

### QĐ-9: Vùng cấm sửa tự do
- `interface/` — sync 2 repo, cần review kỹ
- `external/gsoap/` — generated, sửa qua script
- `OnvifServer.cpp` serve loop — chỉ core team đụng

### QĐ-10: Test sau mỗi thay đổi
- Chạy DTT tool test case liên quan
- Đảm bảo không regression các test đang pass
- Verify build sạch trên server (`/tmp/` only theo memory rule)

---

## 14. Sơ đồ luồng chuẩn 1 request (sau refactor)

```
1. Client gửi SOAP POST → OnvifServer serve loop
                              │
2. soap_begin_serve → lấy path + g_current_headers
                              │
3. ServiceRegistry.route(path) → tìm IOnvifService
                              │
                    ┌─────────┴─────────┐
              không có svc          có svc
                    │                    │
        FaultBuilder::          4. AuthMiddleware.authenticate()
        actionNotSupported()          │
                              ┌────────┴────────┐
                         auth fail          auth OK
                              │                 │
                    gửi 401/fault    5. svc->handle(soap, request)
                                              │
                                     6. handler nhận diện operation
                                              │
                                     7. backend_->method()  (nếu cần data)
                                              │
                                     8. build XML (dùng FaultBuilder nếu lỗi)
                                              │
                                     9. SoapResponse::send(soap, xml)
                                              │
                                    10. Client nhận response
```

Mỗi bước 1 trách nhiệm rõ ràng — không lẫn lộn như God Object cũ.

---

## 15. Checklist review code mới (cho AI agent tự kiểm)

Trước khi coi 1 thay đổi là "xong", tự trả lời:

- [ ] Có sửa `OnvifServer.cpp` không? → Nếu CÓ mà không phải core change → **SAI**, dùng registry
- [ ] Fault có dùng `FaultBuilder` không? → Nếu string-concat → **SAI**
- [ ] Service có tự check auth không? → Nếu CÓ → **SAI**, để middleware
- [ ] Service có tự gọi hardware/socket không? → Nếu CÓ → **SAI**, qua `backend_`
- [ ] File có > 600 dòng không? → Nếu CÓ → cân nhắc tách
- [ ] Có sửa `interface/` mà chưa sync mock backend không? → Nếu CÓ → **SAI**
- [ ] Naming đúng convention (XxxService, handleXxx) không?
- [ ] Đã test DTT không regression chưa?

> Nếu tất cả ✅ → thay đổi tuân thủ kiến trúc chuẩn.
