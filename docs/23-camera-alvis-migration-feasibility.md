# Đánh giá khả thi thay mock backend bằng camera backend thật (Camera-alvis)

> Nguồn: `docs/Camera-alvis/Tai_lieu_SRS_Camera_AI.docx`, `Tai_lieu_kien_truc_phan_mem_Camera_AI-V4_1.docx`, `so_do_khoi_kien_truc (1).svg`, đối chiếu code hiện tại của dự án ONVIF mock.

## 1. Kết luận nhanh

Việc thay dần mock backend bằng camera backend thật là **khả thi và đúng hướng kiến trúc**. SRS/SAD của Camera-alvis đã tách ONVIF stack (MGMT-02/OnvifStack) khỏi phần cứng và các service chức năng, giống hệt nguyên tắc mà dự án mock đang theo: onvif-module giữ tầng SOAP, backend cung cấp dữ liệu và stream.

Mức độ khả thi khác nhau theo profile:

```text
Profile S : cao      — Media/PTZ/Imaging map thẳng vào ICameraBackend
Profile T : cao      — H.265, metadata stream, motion/tamper
Profile M : cao      — VPU-07 đã dự kiến xuất tt:MetadataStream
Profile G : trung    — cần recorder/storage/search/replay thật ở DVR-03
```

Điểm mấu chốt: onvif-module gần như không cần đổi. Công việc chính là thay implementation phía sau IPC, và mở rộng hợp đồng backend cho những phần mock đang “giả tĩnh”, đặc biệt là Recording/Search/Replay của Profile G.

Rủi ro lớn nhất không phải SOAP mà là **DVR-03 RecordWriter và replay theo Range thật**: mock hiện phát live `/main` và dựng timeline giả, còn production phải đọc segment đã ghi theo thời gian.

## 2. Hai kiến trúc đang nói chuyện với nhau

### 2.1 Dự án ONVIF mock hiện tại

```text
DTT / VMS
   │ SOAP 8080
onvif-module (gSOAP + manual-XML services)
   │ IPC Unix socket /tmp/mock-camera.sock (header 16B + JSON)
mock-camera-backend (ICameraBackend → MockCameraBackend)
   │
GStreamer/ffmpeg → MediaMTX :8554 → gortsplib relay :8555
```

Hợp đồng backend hiện tại (`ICameraBackend.h`) gồm: Device, Media2, PTZ, Imaging, Analytics, Events. Replay/Recording/Search **không** nằm trong `ICameraBackend`; chúng được onvif-module trả bằng dữ liệu tĩnh (Recording_0, timeline giả), còn stream replay do relay tự chế từ `/main`.

### 2.2 Camera-alvis (SRS/SAD)

Bảy khối service: HAL, BUS & IPC (nền tảng) và DVR, VPU, Core, Gateway, MGMT (chức năng). ONVIF nằm trong MGMT-02, các service ONVIF ánh xạ:

```text
ONVIF-01 Device/Discovery
ONVIF-02 Media (S,T)        → DVR-02, DVR-04
ONVIF-03 PTZ (S)            → HAL-03/04
ONVIF-04 Recording (G)      → DVR-03, CORE-03
ONVIF-05 AnalyticsMeta (M)  → VPU-07, BUS-02
ONVIF-06 Event             → CORE-02
ONVIF-07 Security          → SEC
```

Điểm tương đồng quan trọng: cả hai đều coi ONVIF là một adapter mỏng đứng trước nguồn dữ liệu thật, và đều dùng IPC/bus để tách phần cứng. Nghĩa là onvif-module của dự án mock có thể đóng vai trò MGMT-02/OnvifStack, còn Camera-alvis DVR/VPU/Core đóng vai backend.

## 3. Bản đồ tương thích ONVIF ↔ Camera-alvis

| ONVIF service | Mock hiện tại | Camera-alvis | Khả năng thay |
|---|---|---|---|
| Device/Discovery | DeviceService + IPC device | ONVIF-01, MGMT-01 | Trực tiếp |
| Media/Media2 (S,T) | getProfiles/getStreamUri | DVR-02/04, ONVIF-02 | Trực tiếp |
| PTZ (S) | ptz* trong ICameraBackend | HAL-04, ONVIF-03 | Trực tiếp |
| Imaging | getImagingSettings… | HAL/Peripheral | Trực tiếp |
| Analytics/Metadata (M) | Analytics + metadata RTP | VPU-07, ONVIF-05 | Trực tiếp, cần metadata thật |
| Event | subscribe/callback + push | CORE-02, ONVIF-06 | Trực tiếp, cần event thật |
| Recording (G) | dữ liệu tĩnh Recording_0 | DVR-03, ONVIF-04 | Cần backend mới |
| Search (G) | timeline/event giả | DVR-03 index | Cần backend mới |
| Replay (G) | relay chế từ /main | DVR-03 archive reader | Cần backend mới |

Cột cuối cho thấy S/T/M chủ yếu là “đổi nguồn dữ liệu”, còn G là “bổ sung năng lực mới”.

## 4. Điểm khớp về hợp đồng dữ liệu

Camera-alvis đặt cấu trúc dùng chung ở `interfaces/`: `FrameBuffer`, `FrameRef` zero-copy, `Detection`, `Event`, `AudioFrame`, `PeriphCmd`. Dự án mock đặt hợp đồng ở `include/interface/` với `MediaTypes`, `AnalyticsTypes`, `EventTypes`, và IPC nhị phân 16 byte.

Hai mô hình không xung đột. Chúng ở hai tầng khác nhau:

```text
Camera-alvis interfaces  = hợp đồng nội bộ giữa HAL/DVR/VPU/Core (in-process, zero-copy)
Mock IPC contract        = hợp đồng giữa onvif-module và backend (cross-process, JSON)
```

Khi tích hợp, Camera-alvis DVR/VPU/Core là “bên trong” backend, còn IPC ONVIF là “vỏ ngoài”. Backend thật sẽ implement `ICameraBackend` (hoặc một interface mở rộng) bằng cách gọi xuống DVR/VPU/Core qua control IPC của Camera-alvis.

Một điểm cần thống nhất: Camera-alvis khuyến nghị `Detection.attrs` và `Event.payload` là điểm mở rộng mở. ONVIF Profile M cần map các trường này sang `tt:MetadataStream`. VPU-07 trong SAD đã ghi rõ nhiệm vụ này, nên đây là khớp nối tự nhiên, không phải phát sinh mới.

## 5. Đánh giá khả thi theo Profile

### 5.1 Profile S và T — khả thi cao

Camera-alvis DVR-02 mã hóa main + sub-stream, DVR-04 phục vụ RTSP theo profile. Đây đúng những gì onvif-module cần từ `getProfiles()` và `getStreamUri()`. H.265 và metadata stream của Profile T nằm trong DVR-02 và ONVIF-06.

Công việc migration chủ yếu là trỏ getStreamUri sang RTSP thật của DVR-04 thay vì MediaMTX mock, và map VideoEncoderConfig sang CodecHAL. Rủi ro thấp vì interface đã có sẵn.

### 5.2 Profile M — khả thi cao, phụ thuộc VPU

Metadata hiện tại của mock là RTP payload cố định do relay bơm định kỳ. Camera-alvis có nguồn thật: VPU sinh Detection, VPU-07 map sang ONVIF metadata, ONVIF-05 phát ra.

Cần đảm bảo thứ tự pts và schema hợp lệ như DTT yêu cầu. SAD đã đặt tiêu chí “thứ tự pts giữ; metadata hợp lệ schema ONVIF”, trùng với điều Profile M kiểm.

### 5.3 Profile G — khả thi trung bình, nhiều việc nhất

Đây là khác biệt lớn nhất giữa mock và production. Tài liệu `docs/21` đã nêu rõ: mock đọc live và dựng timeline giả. Camera-alvis DVR-03 RecordWriter mới là recorder thật: ghi mp4/MJPEG, xoay vòng, lập chỉ mục, mã hóa at-rest, pre/post-event.

Để Profile G production hoạt động thật, cần các năng lực mà mock chưa có:

```text
Storage index    : recording → segment → start/end → codec → offset
Archive reader   : đọc theo Range clock, tạo gap/discontinuity thật
Search engine    : filter/time-range trên dữ liệu thật, phân trang
Replay session   : mỗi RTSP session một cursor riêng
Packetizer       : access unit thật → RTP + 0xABAC + NTP/CSeq/D + RTCP
```

Tin tốt: toàn bộ tầng giao thức Profile G đã được dự án mock giải quyết và đạt 313/313. Byte layout 0xABAC, chuyển RTP timestamp sang NTP, bounded Range, Immediate D flag, `Require: onvif-replay` đều đã đúng chuẩn và có thể tái sử dụng. Việc còn lại là nối chúng vào nguồn dữ liệu thật của DVR-03 thay vì `/main`.

### 5.4 Audio — điểm lệch scope cần quyết định

Scope mock đã chốt no-audio, nên hai case SEARCH audio-filter chỉ pass bằng cách trả empty. Ngược lại, SRS Camera-alvis đặt audio là bắt buộc: FR-AUD-1 ghi/stream audio đồng bộ, FR-IF-2 mic/speaker, FR-HAL02-2 codec G.711/AAC/Opus, FR-AUD-2 talkback hai chiều.

Khi backend thật có audio, camera sẽ hỗ trợ audio track thật. Lúc đó không nên giữ hành vi “trả empty cho filter Audio”; recording thật có audio sẽ làm hai case đó pass theo nhánh “Audio Recording is supported”. Đây là thay đổi behavior có chủ đích, phải cập nhật cả Feature List/DoC khi submit.

## 6. Khoảng trống và xung đột cần xử lý

### 6.1 Hợp đồng backend còn thiếu Recording/Search/Replay

`ICameraBackend.h` chưa có nhóm method cho Profile G. Hiện onvif-module tự trả dữ liệu tĩnh. Khi lên production, cần mở rộng hợp đồng, ví dụ:

```text
getRecordings(), getRecordingInformation()
findRecordings(filter), getRecordingSearchResults(token)
findEvents(...), getEventSearchResults(token)
getReplayUri(recording, transport)
getRecordingJob*(...) nếu quản job thật
```

Vì `include/interface/` là vùng đồng bộ hai repo và cấm lệch, thay đổi này phải làm cẩn thận và versioned.

### 6.2 Replay stream phải rời khỏi relay tự chế

Relay hiện lấy `/replay` từ `/main`. Production cần DVR-03 xuất replay theo Range, hoặc onvif-module trả URI trỏ tới RTSP replay của DVR-04. Logic 0xABAC/NTP/Range nên được chuyển vào packetizer của DVR, không nằm mãi ở relay demo.

### 6.3 Nhiều process và ownership phần cứng

SAD chọn topology 6 process (một HAL daemon sở hữu phần cứng). Dự án mock hiện là backend + onvif + relay. Khi ghép, cần định nghĩa rõ ai start trước, ai giữ socket, và luồng khởi động, tránh lỗi onvif kết nối IPC khi backend chưa sẵn sàng như đã gặp ở mock.

### 6.4 Identity và conformance khi thật hóa

Khi thay backend, model/firmware sẽ là sản phẩm thật, không còn ALG2-B803/B808 giả. Cần chạy lại official Conformance Test trên firmware thật và cập nhật DoC/Feature List, như đã nêu ở `docs/22`.

### 6.5 Bảo mật qua Gateway

SRS yêu cầu mọi luồng ra ngoài đi qua Gateway (TLS, auth, audit). ONVIF media/replay của camera thật phải tôn trọng ràng buộc này, khác với mock hiện mở cổng trực tiếp. Cần thống nhất ONVIF endpoint nằm trong hay sau Gateway.

## 7. Roadmap thay dần mock backend

Nguyên tắc: giữ onvif-module ổn định, thay backend theo từng nhóm capability, sau mỗi phase chạy DTT để không tụt regression khỏi mốc hiện tại (M/T/S 244/244, G 313/313).

### Phase 0 — Chuẩn bị hợp đồng và khung tích hợp

Chốt interface mở rộng cho Profile G trong `include/interface/`. Định nghĩa adapter: một `AlvisCameraBackend implements ICameraBackend` gọi xuống DVR/VPU/Core qua control IPC của Camera-alvis. Thêm feature flag chọn Mock hay Alvis backend để cutover từng phần.

Tiêu chí: build hai backend song song; onvif-module không đổi hành vi khi vẫn chạy Mock.

### Phase 1 — Device, Media, PTZ, Imaging (Profile S/T)

Trỏ getDeviceInfo/getProfiles/getStreamUri/PTZ/Imaging sang Camera-alvis thật. RTSP lấy từ DVR-04. Giữ Analytics/Event/Replay tạm ở mock.

Tiêu chí: DTT Profile S và phần T liên quan media/PTZ pass trên backend thật; không regression.

### Phase 2 — Analytics và Event (Profile M/T)

Nối VPU-07 → metadata thật, CORE-02 → event thật vào onvif-module Analytics/Event. Bảo đảm pts và schema.

Tiêu chí: Profile M conformance trên metadata thật; Event PullPoint pass với event thật.

### Phase 3 — Recording và Search (Profile G control plane)

Thay dữ liệu tĩnh Recording/Search bằng DVR-03 index thật: recording thật, track thật, timeline và event lịch sử thật. Vẫn có thể tạm dùng replay demo cho tới Phase 4.

Tiêu chí: GetRecordings/Search/GetRecordingInformation phản ánh dữ liệu ghi thật; Search filter đúng semantics.

### Phase 4 — Replay data plane thật (Profile G)

Chuyển packetizer 0xABAC/NTP/Range/Immediate vào DVR-03 archive reader. Mỗi RTSP replay session có cursor riêng. Bỏ alias `/replay` từ `/main`.

Tiêu chí: Replay đọc đúng đoạn ghi theo Range; bounded/Immediate/PAUSE hoạt động trên dữ liệu thật; DTT Profile G pass.

### Phase 5 — Audio, Gateway, Security, hoàn tất

Bật audio track thật (ghi/stream/talkback), đưa ONVIF vào sau Gateway TLS/auth, hoàn tất Security service. Điều chỉnh Feature List no-audio → có audio.

Tiêu chí: A/V sync trong ngưỡng; audio filter Search theo nhánh supported; full S/T/M/G conformance trên firmware thật.

### Phase 6 — Conformance và submit

Chạy official Conformance Test trên firmware release, tạo DoC + Feature List + Test Report, submit theo `docs/22`.

## 8. Bảng rủi ro

| Rủi ro | Ảnh hưởng | Giảm thiểu |
|---|---|---|
| DVR-03 recorder/index chưa có | Chặn Profile G thật | Ưu tiên Phase 3–4, tái dùng logic 0xABAC đã đạt chuẩn |
| Sửa `include/interface/` lệch hai repo | Vỡ IPC contract | Versioned interface, đổi đồng thời hai repo, CI check |
| Replay per-session state | Nhiều client replay sai | Thiết kế session cursor riêng ngay từ Phase 4 |
| Audio đổi behavior Search | Khác Feature List/DoC | Quyết định scope audio trước khi submit |
| Ownership phần cứng nhiều process | Lỗi khởi động/tranh chấp | Chuẩn hóa thứ tự start, một chủ sở hữu HAL |
| Gateway TLS chắn ONVIF | Sai luồng conformance | Thống nhất vị trí ONVIF so với Gateway sớm |
| Hiệu năng thiết bị thấp (Rockchip) | Tụt FPS/latency | Đo tải sớm, giữ zero-copy như SAD |

## 9. Việc cần chốt với team

```text
1. onvif-module của dự án mock có được chọn làm MGMT-02/OnvifStack chính thức không,
   hay Camera-alvis tự viết ONVIF stack riêng?
2. Hợp đồng backend mở rộng cho Profile G đặt ở đâu và version thế nào?
3. DVR-03 recorder/storage/index đã có tới đâu trong Camera-alvis thực tế?
4. Replay production do DVR xuất RTSP hay onvif-module vẫn cầm packetizer?
5. Audio: bật đầy đủ cho tất cả model hay theo dòng sản phẩm?
6. ONVIF endpoint nằm trước hay sau Gateway?
7. Model/firmware release nào dùng để submit S/T/M/G?
```

## 10. Tổng kết

Kiến trúc hai bên đồng dạng: ONVIF là adapter mỏng trước nguồn dữ liệu, phần cứng bị cô lập sau IPC/bus. Vì vậy thay dần mock backend bằng Camera-alvis là hợp lý và ít rủi ro ở Profile S/T/M. Toàn bộ tri thức giao thức Profile G khó nhất đã được giải quyết và đạt 313/313, có thể tái sử dụng.

Khối lượng thật nằm ở DVR-03: recorder, storage index, archive reader và replay per-session. Nếu đi theo roadmap 6 phase, giữ onvif-module ổn định và chạy DTT sau mỗi phase, dự án có thể chuyển từ mock sang camera thật mà không đánh mất trạng thái conformance đang có.