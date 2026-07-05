# Media1 (ver10/media/wsdl) legacy declaration

## Bối cảnh

ONVIF có 2 phiên bản Media service:
- **Media1** (`http://www.onvif.org/ver10/media/wsdl`) — dùng cho Profile S, Profile G legacy
- **Media2** (`http://www.onvif.org/ver20/media/wsdl`) — dùng cho Profile T, Profile A modern

Camera target **Profile T** dùng Media2. Không cần Media1.

## Trạng thái hiện tại (2026-07-05)

Media1 đã được **tạm comment** trong `GetServices`:

| File | Vị trí | Sửa |
|------|--------|-----|
| `src/services/DeviceService.cpp` (IncludeCapability=true block) | Media1 svc() call | Comment toàn bộ block |
| `src/services/DeviceService.cpp` (IncludeCapability=false / add lambda) | Media1 add() call | Comment 1 dòng |

## Lý do trước đây giữ Media1

Trước khi có DeviceIO service, test tool ONVIF dùng `trt:GetVideoSources` (Media1 endpoint) để phát hiện VideoSource cho các test Imaging. Nếu không declare Media1 → tool báo "Neither media, nor I/O supported" → Imaging tests fail.

## Lý do giờ bỏ Media1

Đã implement **DeviceIO service** (`http://www.onvif.org/ver10/deviceIO/wsdl`) với `GetVideoSources` (spec Profile T §7.10.3 mandatory).

Tool ưu tiên DeviceIO `GetVideoSources` khi cả 2 service được declare. Đã xác nhận trong `result45.xml`:
```
IMAGING-1-1-1 step 4 request:
<GetVideoSources xmlns="http://www.onvif.org/ver10/deviceIO/wsdl" />
```

Vì thế Media1 không còn cần thiết.

## Effect khi bỏ Media1

- Tool feature detection: KHÔNG thấy Media1 service
- Conformance test: skip toàn bộ **Media Configuration** group (~38 test) và **RTSS-1-1-27..53** Media1 legacy (~24 test)
- Profile T pass rate: tăng đáng kể do denominator giảm
- Imaging test vẫn hoạt động bình thường qua DeviceIO

## Khi nào uncomment lại Media1

Uncomment nếu:
1. **Cần chứng chỉ Profile S** (Media1 mandatory cho Profile S)
2. **Camera cần backward compat** với VMS/CMS cũ chỉ hỗ trợ Media1
3. **Test tool cũ** không hỗ trợ DeviceIO

Uncomment 2 vị trí:
- `src/services/DeviceService.cpp` IncludeCapability=true block (khoảng dòng 368)
- `src/services/DeviceService.cpp` IncludeCapability=false path (khoảng dòng 420)

Sau đó `make full` + restart onvif-server.

**LƯU Ý**: Uncomment Media1 mà không implement đầy đủ Media1 API (Audio Config, Video Config, VideoSource Config legacy...) sẽ khiến ~60 Media1 test fail. Cần cân nhắc:
- Hoặc uncomment + implement đầy đủ Media1 (tốn 5-7 ngày)
- Hoặc giữ comment (như hiện tại) — target Profile T only

## Liên kết

- Profile T spec: [ONVIF_Profile_T_Specification_v1-0.md](ONVIF_Profile_T_Specification_v1-0.md)
- Scope Profile S note: [08-scope-profile-note.md](08-scope-profile-note.md)
- Conformance plan: [06-profile-t-conformance-plan.md](06-profile-t-conformance-plan.md)
