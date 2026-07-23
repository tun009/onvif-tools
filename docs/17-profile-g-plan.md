# Kế hoạch triển khai Profile G (Recording / Search / Replay)

> Nghiên cứu: 2026-07-22. Nguồn: `ONVIF_Profile_G_Specification_v1-0.pdf` + khảo sát codebase.
> Trạng thái: **NGHIÊN CỨU** (chưa code — chờ quyết định hướng đi ở mục 7).

---

## 1. Profile G là gì

Thiết bị ghi hình video qua IP hoặc trên chính thiết bị (camera/encoder có on-board
recording, hoặc NVR). Client có thể cấu hình, yêu cầu, điều khiển việc ghi + tìm kiếm
+ phát lại. DUT của ta khai báo **Encoder + Fixed Camera** → đi hướng **on-board media
source** (mục 9.1.4 của spec), **KHÔNG cần Receiver Service** (9.1.5 chỉ dành cho NVR;
spec yêu cầu "ít nhất 1 trong 2").

3 service ONVIF mới cần thêm:
| Service | Namespace | Path đề xuất |
|---------|-----------|--------------|
| Recording Control | `ver10/recording/wsdl` (`trc`) | `/onvif/recording` |
| Recording Search | `ver10/search/wsdl` (`tse`) | `/onvif/search` |
| Replay Control | `ver10/replay/wsdl` (`trp`) | `/onvif/replay` |

Ngoài ra tận dụng lại Device + Media + Events đã có.

---

## 2. Danh sách chức năng bắt buộc (Device = M)

### 2.1 Recording Search (§7.3) — `/onvif/search`
`FindRecordings`, `GetRecordingSearchResults`, `FindEvents`, `GetEventSearchResults`,
`EndSearch`, `GetRecordingSummary`, `GetRecordingInformation`, `GetMediaAttributes`,
`GetServiceCapabilities`. Hỗ trợ **XPath dialect** làm search filter.

### 2.2 Replay Control (§7.4) — `/onvif/replay`
`GetReplayUri`, `SetReplayConfiguration`, `GetReplayConfiguration`,
`GetServiceCapabilities`. RTSP replay phải có **RTP header extension** (Streaming spec
6.2) + hỗ trợ **feature tag `onvif-replay`** (RTSP 6.3) + RTSP session timeout.
Reverse replay = tùy chọn (có thể bỏ).

### 2.3 Recording Control (§9.1, §9.2) — `/onvif/recording`
`GetRecordings`, `GetRecordingOptions`, `GetRecordingJobs`, `CreateRecordingJob`,
`DeleteRecordingJob`, `GetRecordingJobState`, `SetRecordingJobMode`,
`GetRecordingConfiguration`, `SetRecordingConfiguration`, `GetTrackConfiguration`,
`SetTrackConfiguration`, `GetRecordingJobConfiguration`, `SetRecordingJobConfiguration`,
`GetServiceCapabilities`.
Conditional (nếu khai "dynamic recording"): `CreateRecording`, `DeleteRecording`,
`CreateTrack`, `DeleteTrack`. → **Đề xuất khai KHÔNG hỗ trợ dynamic** để giảm scope
(spec cho phép — các op này là Conditional "if supported").

### 2.4 Media (§9.1.4) — ĐÃ CÓ (Profile M/T/S)
Toàn bộ profile/VideoSource/VideoEncoder/Metadata config đã implement rồi. Chỉ cần
đảm bảo Recording tham chiếu đúng các config này.

### 2.5 Device / Discovery / Events — ĐÃ CÓ, cần bổ sung
- Discovery: thêm scope `onvif://www.onvif.org/Profile/G`.
- GetServices: advertise thêm 3 service NS mới.
- Events (pull-point): đã có; cần **MaxPullPoints ≥ 2** và thêm topic (mục 6).

---

## 3. Hiện trạng codebase — ĐÃ CÓ vs CÒN THIẾU

| Hạng mục | Đã có | Còn thiếu cho G |
|---|---|---|
| Kiến trúc service | Registry (manual-XML `IOnvifService`) + gSOAP dispatch. Thêm service = 1 dòng `registerService` trong `OnvifServer.cpp:242` | 3 service mới |
| gSOAP binding | Device/Media2/Imaging (generated/ **gitignore**, sinh lúc build) | KHÔNG có recording/search/replay WSDL/binding/namespace |
| Backend `ICameraBackend` | 30 method (đã sync 2 repo, identical) | method recording/search/replay + opcode IPC |
| GetServices/Scopes | advertise 7 NS; scope S/T/M ([DiscoveryService.cpp:123](../onvif-module/onvif-module/src/services/DiscoveryService.cpp)) | advertise 3 NS; scope Profile/G |
| Streaming | RTSP **live** (MediaMTX 8554 + gortsplib relay 8555 + tunnel 8080) | **replay pipeline hoàn toàn mới** |
| Events TopicSet | VideoSource, Media ([MockSubscriptionManager.cpp:262](../onvif-module/onvif-module/src/services/MockSubscriptionManager.cpp)) | RecordingConfig, RecordingHistory |

---

## 4. Quyết định kiến trúc: MANUAL-XML, không đụng gSOAP/core

gSOAP hiện KHÔNG bind recording/search/replay. Có 2 lựa chọn:

**(A) Regenerate gSOAP stubs** từ 3 WSDL (thêm vào `download_wsdls.sh`, `patch_generated.sh`
namespace `trc/tse/trp`, Makefile binding). → Rủi ro CAO: đụng toolchain soapcpp2, có thể
vỡ Device/Media2 binding đang chạy ổn; `generated/` không track git (bài học stale binary).

**(B) Manual-XML registry service** (KHUYẾN NGHỊ) — giống hệt `MediaLegacyHandler`
(Media1 đang chạy tốt, hoàn toàn manual XML). Mỗi service mới:
- Kế thừa `IOnvifService`, khai `pathPrefix()` = `/onvif/recording|search|replay`.
- `handle(rawReq)` string-match tên operation → build XML response thủ công.
- Đăng ký 1 dòng `registry_.registerService(...)` trong `OnvifServer.cpp:242-254`.
- **KHÔNG đụng** `generated/`, serve-loop, vùng cấm. Không cần WSDL/soapcpp2.

→ Chọn (B). Nhất quán với pattern hiện có, rủi ro thấp, không regression Profile M/T/S.

---

## 5. RỦI RO LỚN NHẤT: Replay streaming (GetReplayUri)

Đây là phần khó tương đương hạ tầng streaming của Profile M/T. Live streaming đã có,
nhưng **replay chưa có gì** (grep `replay|recording|storage` → 0). Replay ONVIF yêu cầu:
1. Có **recording thật** (file/segment media) để phát lại theo Range thời gian.
2. RTSP server hỗ trợ **Range** (seek theo UTC/PTS), **Rate-Control/scale** (speed),
   **reverse** (tùy chọn).
3. **RTP header extension** (ONVIF Streaming 6.2): mỗi RTP packet mang NTP timestamp +
   C/E/D flags (frame boundary, discontinuity...). DTT Replay test decode cái này.
4. Feature tag **`onvif-replay`** trong RTSP (`Require: onvif-replay`).

MediaMTX/gortsplib hiện KHÔNG làm replay/RTP-extension. Các hướng:
- **(a)** Mở rộng gortsplib relay (Go) để serve 1 endpoint replay: đọc file ghi sẵn
  (hoặc sinh testsrc gán timestamp quá khứ), thêm RTP header extension + parse Range.
  Việc lớn nhưng khả kiểm soát (ta đã sửa relay cho M/T tunnel).
- **(b)** Ghi sẵn vài file MP4/segment ngắn rồi phát qua ffmpeg với timestamp giả.
- **(c)** Giai đoạn đầu: implement SOAP-layer trước (GetReplayUri trả URI hợp lệ),
  để phần streaming decode làm sau — giống cách ta làm M/T (SOAP xanh trước, streaming
  case xử lý cuối).

→ Cần khảo sát DTT Profile G để biết Replay test có decode RTP extension không, hay chỉ
kiểm URI + RTSP DESCRIBE/PLAY (giống mức nhẹ). **Đây là câu hỏi mở quan trọng nhất.**

---

## 6. Events cần thêm (TopicSet, FixedTopicSet=true → khai tĩnh đủ)

Trong `MockSubscriptionManager::handleGetEventProperties`:
- `tns1:RecordingConfig/JobState` (M), `/RecordingConfiguration`, `/TrackConfiguration`,
  `/CreateRecording|DeleteRecording|CreateTrack|DeleteTrack` (C — nếu dynamic).
- `tns1:RecordingHistory/Recording/State` (M), `tns1:RecordingHistory/Track/State` (M).
- Sinh notification thật cho `RecordingJobStateChange` khi CreateRecordingJob/SetMode.
- Lưu ý bài học prefix: leaf topic KHÔNG prefix `tns1:` (chỉ node gốc) — xem
  memory `event-topicset-prefix`, nếu sai → DTT NOT SUPPORTED.

---

## 7. Kế hoạch theo giai đoạn (đề xuất)

**Phase 0 — Khảo sát DTT Profile G** (cần user chạy DTT để lấy log như đã làm với M/T):
xác định Replay test decode RTP extension hay chỉ kiểm URI; số lượng recording/track
DTT kỳ vọng; format search filter (XPath) thực tế DTT gửi.

**Phase 1 — SOAP layer (an toàn, không đụng streaming):**
1. Backend: thêm data model fake — 1 recording (`SD_REC_0`?) với 1 video track (+ audio/
   metadata track tùy chọn), 1 recording job, trạng thái. Sync `interface/` cả 2 repo +
   opcode IPC mới.
2. `RecordingControlService` (manual XML): Get/Set Recording/Track/Job config + jobs +
   options + capabilities.
3. `RecordingSearchService`: FindRecordings/GetRecordingSearchResults/FindEvents/
   GetEventSearchResults/EndSearch (session token + trả kết quả 1 lần) + Summary/
   Information/MediaAttributes.
4. `ReplayControlService`: Get/SetReplayConfiguration + GetReplayUri (trả URI hợp lệ).
5. Device: advertise 3 NS trong GetServices + thêm capabilities.
6. Discovery: thêm scope Profile/G.
7. Events: thêm topic RecordingConfig/RecordingHistory + notification JobState.

**Phase 2 — Replay streaming (khó, làm cuối):**
- Dựng recording store + RTSP replay endpoint (gortsplib) + RTP header extension +
  onvif-replay feature tag + Range/scale.

**Phase 3 — Chạy DTT full, vá case còn lại** (giống quy trình M/T: SOAP xanh trước,
streaming case cuối cùng).

---

## 8. Ràng buộc & bài học áp dụng (từ M/T)

- **KHÔNG đụng** `generated/`, `external/gsoap`, serve-loop `OnvifServer.cpp` (trừ 1 dòng
  registerService). Dùng pattern manual-XML.
- `interface/` phải **sync HỆT** cả 2 repo khi thêm method backend.
- Deploy: build-check `/tmp` → commit (report tên+ID) → push → pull → **rebuild sạch** →
  restart **backend TRƯỚC, onvif SAU**; kill bằng `kill -9` theo PID (pkill -f không ăn).
- `generated/` không track git → luôn rebuild sạch, tránh stale binary (bài học m109).
- Chạy DTT xác nhận từng bước, tránh regression Profile M/T/S (244/244 đang xanh).

---

## 9. Quyết định scope (user chốt 2026-07-22)

- ✅ **Dynamic recording: KHÔNG hỗ trợ.** Khai fixed recording (1 recording dựng sẵn).
  Bỏ CreateRecording/DeleteRecording/CreateTrack/DeleteTrack + các event Create/Delete
  tương ứng. (Spec cho phép — Conditional "if supported".)
- ✅ **Audio: KHÔNG có on-board audio.** Bỏ toàn bộ Audio Source/Encoder config
  (~14 operation). Chỉ video + metadata track.
- ⏳ Chưa code / chưa chạy gì — user yêu cầu chỉ dừng ở nghiên cứu.

### Còn cần làm rõ khi bắt tay (Phase 0):
1. **Replay streaming**: DTT Profile G Replay test có decode RTP header extension +
   đếm frame không (như RTSS của M/T), hay chỉ kiểm URI/DESCRIBE? → chạy thử DTT lấy log.
2. Thứ tự khi làm: Phase 1 (SOAP) trước rồi Phase 2 (streaming), giống M/T.
