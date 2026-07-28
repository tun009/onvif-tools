# 19 — Profile G: Kế hoạch xử lý DTT 25.12 `g1.xml`

> Baseline: DTT 25.12, chạy 2026-07-28. Report nguồn: `g1.xml`.
> Trạng thái trước triển khai: **Feature Definition PASSED**, Profile S/M/T/G đều
> **SUPPORTED**; conformance **267/313 passed, 46 failed**.
>
> Scope đã chốt: 1 recording tĩnh `Recording_0`, tracks `VIDEO_0` + `META_0`,
> job `Job_0`, non-dynamic, không onboard audio. Manual XML; không regenerate gSOAP;
> không đụng `interface/`, `external/gsoap/` hay serve-loop.

## 1. Phân bố và nguyên nhân gốc

| Nhóm | Fail | Nguyên nhân gốc |
|---|---:|---|
| Replay | 21 | 19 case chết ngay DESCRIBE vì URI `127.0.0.1`; 1 invalid-token; 1 replay config không lưu |
| Recording | 16 | Options schema/caps (5), invalid token (4), track/config state (2), event schema/data (5) |
| Search | 7 | EndSearch schema/state (1), audio filter (2), event results rỗng (4) |
| Event | 2 | Subscribe filter làm connection reset trong full run |

46 failure quy về 14 root cause:

1. Replay URI dùng localhost và không theo transport — 19 case cascade.
2. `GetRecordingOptions` thiếu wrapper `<trc:Options>` — 4 case cascade.
3. `GetServices(IncludeCapability=true)` còn `Options=false`, trong khi
   `GetServiceCapabilities` là true — 1 case.
4. Recording invalid-token faults (`NoRecording`, `NoTrack`, `NoRecordingJob`) — 4 case.
5. Replay invalid token thiếu `NoRecording` — 1 case.
6. `GetTrackConfiguration` luôn trả Video, không theo `TrackToken` — 1 case.
7. `SetRecordingConfiguration` chỉ ACK, không lưu — 1 case.
8. `SetReplayConfiguration` chỉ ACK, không lưu `SessionTimeout` — 1 case.
9. `EndSearchResponse` trả `DataPointer`; DTT yêu cầu `Endpoint` — 1 case.
10. `FindRecordings` bỏ qua Audio XPath filter — 2 case.
11. `GetEventSearchResults` rỗng — 4 case.
12. Recording event TopicSet sai/thiếu — 4 case.
13. `SetRecordingJobMode` không phát JobState notification — 1 case.
14. Event Subscribe connection reset — 2 case regression/full-run.

## 2. Đợt 1 — SOAP schema, capability, state và fault

Mục tiêu: xử lý các lỗi rẻ, deterministic, chưa đụng streaming; kỳ vọng giảm khoảng
13–14 fail.

### 2.1 Recording schema/capability

- Đồng bộ `Options="true"` trong cả hai nguồn:
  - Device `GetServices(IncludeCapability=true)`.
  - Recording `GetServiceCapabilities`.
- `GetRecordingOptionsResponse` phải có phần tử ngoài `<trc:Options>` theo đúng
  serializer/schema DTT 25.12; không đặt `JobOptions/TrackOptions` trực tiếp dưới response.
- Validate lại shape bằng chính request/response trích từ `g1.xml`.

### 2.2 Token validation/fault

- Recording:
  - invalid RecordingToken → `Sender/InvalidArgVal/NoRecording`.
  - invalid TrackToken → `Sender/InvalidArgVal/NoTrack`.
  - invalid JobToken → `Sender/InvalidArgVal/NoRecordingJob`.
- Replay invalid RecordingToken → `Sender/InvalidArgVal/NoRecording`.
- Dùng `FaultBuilder`; không tự ghép fault tùy tiện nếu helper đã có.

### 2.3 Minimal in-memory state

- `GetTrackConfiguration`: `VIDEO_0` trả Video; `META_0` trả Metadata.
- `SetRecordingConfiguration`: lưu và Get trả lại cấu hình đã set (ít nhất các field
  DTT so sánh trong case 4-1-11).
- `SetReplayConfiguration`: lưu `SessionTimeout`; Get trả đúng giá trị mới.
- `EndSearchResponse`: đổi `DataPointer` thành `Endpoint`; sau EndSearch, session phải
  chuyển trạng thái ended để case tiếp theo nhận behavior/fault đúng.

## 3. Đợt 2 — Recording Events và Search

### 3.1 TopicSet/notifications

- `RecordingConfiguration` và `TrackConfiguration`: `IsProperty=false`.
- `JobState/RecordingJobToken`: type `tt:RecordingJobReference`.
- Thêm topic `tns1:RecordingConfig/RecordingJobConfiguration`.
- `SetRecordingJobMode` cập nhật state và sinh JobState notification cho `Job_0`.
- Sau mỗi thay đổi event phải chạy lại full suite để tránh regression Profile M/T/S.

### 3.2 Search

- `FindEvents/GetEventSearchResults`: trả ít nhất 1 event metadata hợp lệ; hỗ trợ
  forward/backward, MaxMatches và endpoint trong/bằng/ngoài recording endpoints.
- Parse tối thiểu `RecordingInformationFilter`:
  - Video → Recording_0.
  - Audio / Video+Audio: quyết định sau khi nghiên cứu gating DTT; không vội thêm audio
    vì sẽ mở thêm Media/Replay audio scope.
- Search token có lifecycle rõ: active → completed/ended.

### 3.3 Event regression

- Đối chiếu `/tmp/onvif-server.log` tại EVENT-2-1-18/25.
- Tái tạo Subscribe Notify filter single + OR trong full-run state.
- Không sửa TopicSet mù; xác định crash/close ở parser, auth, body segmentation hay
  subscription state trước.

## 4. Đợt 3 — Replay endpoint tới được DESCRIBE/SETUP

Mục tiêu: gỡ blocker chung 19 case để lộ failure streaming thật.

- `GetReplayUri` dùng device IP thật, không `127.0.0.1`.
- URI theo transport:
  - UDP / RTSP TCP → `rtsp://<device-ip>:8555/replay`.
  - HTTP → URI tunnel qua cổng 8080 theo convention Media hiện có.
- Thêm path `/replay` vào gortsplib relay, trả SDP nhất quán với recording tracks.
- Validate DESCRIBE, SETUP qua UDP, interleaved TCP và HTTP tunnel.

## 5. Đợt 4 — Replay streaming thật

Chỉ làm sau khi DTT qua DESCRIBE/SETUP:

- `Require: onvif-replay` trên SETUP/PLAY.
- `Range: clock=<UTC>` và response Range/RTP-Info.
- `Rate-Control: yes/no`.
- Pause/Play có và không Range; immediate header.
- RTP extension profile `0xABAC`, length 3 words:
  - NTP 8 byte.
  - flags C/E/D/T.
  - low byte PLAY CSeq.
  - padding 2 byte.
- Extension chỉ ở RTP packet đầu mỗi access unit.
- Scope reverse vẫn false; không làm reverse playback.
- Test thật `ServerStream.WritePacketRTP` có giữ Extension fields không.

## 6. Quy trình triển khai bắt buộc

1. Sửa code ở local.
2. Copy repo server sang `/tmp/build_check`, chỉ upload file changed vào bản copy.
3. `make full` trong `/tmp/build_check`; không SFTP đè repo thật.
4. Commit ở local.
5. Push local; nếu local không có credential, chuyển **git bundle commit** vào `/tmp`
   server, fast-forward đúng cùng hash rồi push bằng SSH key server.
6. Server `git pull`/fast-forward từ commit đã push; build repo thật.
7. Restart đúng service bị ảnh hưởng bằng PID + `kill -9`; verify qua kết nối SSH mới.
8. Chạy DTT full; cập nhật baseline và root-cause map trong file này.

## 7. Trạng thái thực hiện

| Đợt | Trạng thái | Kết quả |
|---|---|---|
| Feature Definition | DONE | PASSED; Profile G SUPPORTED |
| Đợt 1 — SOAP/state/fault | IMPLEMENTED — AWAITING DTT | Build-check + self-check 11/11; chưa có baseline DTT mới |
| Đợt 2 — Events/Search | PENDING | — |
| Đợt 3 — Replay connectivity | PENDING | — |
| Đợt 4 — Replay RTP | PENDING | — |
