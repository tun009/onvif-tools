# Scope declaration & Profile claim

## Bối cảnh

ONVIF test tool 24.12 xác định device claim profile nào **thông qua Scopes** (`onvif://www.onvif.org/Profile/<X>`). Khi run Conformance test, tool chạy full test suite cho MỌI profile mà device claim.

## Trạng thái hiện tại (2026-07-03)

Camera mock chỉ target **Profile T**. Đã comment scope Profile S để tool skip Profile S conformance test group (~30-40 test Media1 legacy).

### File đã sửa

| File | Vị trí | Sửa |
|------|--------|-----|
| `include/services/DeviceService.h` | `SystemState::scopes` | Comment `onvif://www.onvif.org/Profile/Streaming` |
| `src/services/DiscoveryService.cpp` | `scopesLine()` default fallback | Comment cùng dòng để đồng bộ Hello + ProbeMatch |

### Scopes đang active

```
onvif://www.onvif.org/type/NetworkVideoTransmitter
onvif://www.onvif.org/type/video_encoder
onvif://www.onvif.org/name/MockCam-4K
onvif://www.onvif.org/hardware/JetsonOrinNX-8GB
onvif://www.onvif.org/Profile/T
```

### Scope đã comment (giữ trong code, có thể uncomment lại)

```
// onvif://www.onvif.org/Profile/Streaming    ← Profile S
```

## Effect

- Tool feature detection: **Profile S: NOT SUPPORTED**
- Conformance test: skip ~30-40 test Profile S (Media1 legacy VideoConfig/AudioConfig)
- Profile T pass rate không đổi (~118-120/313)
- Tổng pass rate tăng tương đối do denominator giảm test fail

## Khi nào uncomment lại Profile/Streaming

Uncomment 2 dòng trên (DeviceService.h + DiscoveryService.cpp) nếu:

1. **Cần chứng chỉ Profile S** đồng thời với Profile T (một số VMS/CMS yêu cầu Profile S cho backward compatibility)
2. **Camera cần "đa profile"** hỗ trợ cả client cũ dùng Media1 (ver10/media/wsdl)
3. **Test integration** với hệ thống chỉ hiểu Profile S

**LƯU Ý**: Uncomment scope Profile S mà không implement đầy đủ Media1 API sẽ khiến ~30-40 test Profile S fail. Cần cân nhắc:
- Hoặc uncomment + implement đầy đủ Media1 (Audio Config + VideoConfig legacy)
- Hoặc giữ comment (như hiện tại)

## Cách uncomment

1. Mở `include/services/DeviceService.h`, tìm dòng comment có Profile/Streaming
2. Bỏ `// ` ở đầu dòng
3. Mở `src/services/DiscoveryService.cpp` file, tìm dòng tương tự trong hàm `scopesLine()`
4. Bỏ `// ` ở đầu dòng
5. `make full` + restart onvif-server
6. Chạy lại tool test — verify Profile S: SUPPORTED

## Liên kết

- Profile T spec: [ONVIF_Profile_T_Specification_v1-0.md](ONVIF_Profile_T_Specification_v1-0.md)
- Profile S spec (chưa có trong docs, tham khảo onvif.org)
- Conformance plan: [06-profile-t-conformance-plan.md](06-profile-t-conformance-plan.md)
