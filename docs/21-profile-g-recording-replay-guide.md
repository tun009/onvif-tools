# Hiểu sâu ONVIF Profile G: Recording, Search, Replay và cách project triển khai

## 1. Bức tranh tổng thể

Profile G giải quyết bài toán “video đã ghi” của ONVIF. Một client như VMS cần làm được bốn việc: biết device có những recording nào, biết mỗi recording chứa track gì và tồn tại trong khoảng thời gian nào, tìm dữ liệu hoặc event trong recording, rồi yêu cầu device phát lại từ một thời điểm cụ thể.

Có thể hình dung Profile G như thư viện phim:

```text
Recording       = một bộ phim / một timeline lưu trữ
Track           = các lớp dữ liệu trong phim: video, audio, metadata
Recording Job   = quy tắc nối nguồn camera vào recording
Search          = tra cứu mục lục và các mốc sự kiện
Replay          = đầu phát đọc timeline theo thời gian
```

Một flow điển hình:

```text
Client
  │
  ├─ GetRecordings ───────────────► biết RecordingToken
  ├─ GetRecordingInformation ─────► biết thời gian và tracks
  ├─ FindEvents / FindRecordings ─► tìm đoạn cần xem
  ├─ GetReplayUri ────────────────► nhận RTSP URI
  └─ DESCRIBE → SETUP → PLAY ─────► nhận RTP replay
```

Profile G không định nghĩa file MP4 phải nằm ở đâu, database nào phải dùng, hay ổ đĩa phải tổ chức thế nào. Nó định nghĩa giao diện và hành vi quan sát được từ mạng. Device có thể lưu MP4, fragmented MP4, MPEG-TS, raw elementary stream hoặc format proprietary, miễn SOAP/RTSP/RTP tuân thủ chuẩn.

## 2. Bốn service phối hợp

### 2.1 Recording Control Service

Recording Control quản lý cấu trúc logic và việc đưa nguồn vào recording. Các khái niệm chính là Recording, Track, Recording Job và Source.

### 2.2 Recording Search Service

Search không điều khiển ghi. Nó trả lời các câu hỏi như “có recording nào?”, “recording kéo dài từ lúc nào đến lúc nào?”, “recording nào có video?”, “event MotionAlarm xuất hiện lúc nào?”.

### 2.3 Replay Control Service

Replay Control chỉ có vài SOAP operation. Nó cấp RTSP URI và cấu hình timeout. Việc phát dữ liệu thật diễn ra bằng RTSP/RTP, không đi qua SOAP.

### 2.4 Event Service

Event báo các thay đổi như recording bắt đầu/dừng, track có hoặc mất dữ liệu, recording job đổi mode, configuration thay đổi. Recording Search cũng dùng event lịch sử để dựng timeline.

## 3. Data model Recording

### 3.1 Recording

Recording là container logic có token duy nhất. Nó mô tả nguồn, nội dung, retention time và danh sách track.

Ví dụ đơn giản:

```text
RecordingToken: Recording_0
Source:         camera profile_main
Content:        Mock on-board recording
Retention:      P30D
Tracks:         VIDEO_0, META_0
```

Recording không đồng nghĩa trực tiếp với một file. Một recording có thể gồm nhiều file, nhiều segment và gap. Client chỉ thấy một timeline thống nhất.

Trong project, model nằm ở `RecordingService.cpp`:

```cpp
const char* REC = "Recording_0";
const char* JOB = "Job_0";
const char* SRC_PROFILE = "profile_main";
```

`defaultRecordingConfig()` trả Source, Content và `MaximumRetentionTime=P30D`. `GetRecordings` ghép configuration với hai track cố định.

### 3.2 Track

Track là một dòng dữ liệu độc lập trong recording. ONVIF dùng ba loại chính:

```text
Video
Audio
Metadata
```

Project khai:

```text
VIDEO_0  TrackType=Video
META_0   TrackType=Metadata
```

Không có Audio. Điều này hợp lệ: Audio là conditional, không phải mọi Profile G device đều phải hỗ trợ.

Track metadata có thể chứa analytics object, motion event, PTZ position hoặc event lịch sử. Trong project, relay tạo ONVIF metadata RTP payload định kỳ, còn Search Service dựng event lịch sử tương ứng.

### 3.3 Recording Job

Recording Job trả lời câu hỏi “nguồn nào đang được ghi vào recording nào, với mode gì?”. Có thể hiểu đây là dây nối giữa Media Profile/Receiver và Recording.

Project có một job:

```text
JobToken:      Job_0
Recording:     Recording_0
SourceToken:   profile_main
Source Type:   ONVIF Media Profile
Mode:          Idle hoặc mode DTT set
Priority:      10
```

Các operation chính đã triển khai:

```text
GetRecordingJobs
CreateRecordingJob
DeleteRecordingJob
GetRecordingJobConfiguration
SetRecordingJobConfiguration
GetRecordingJobState
SetRecordingJobMode
```

Dù device khai `DynamicRecordings=false` và `DynamicTracks=false`, Recording Job vẫn là phần cần triển khai cho Profile G device. “Không dynamic recording” có nghĩa không cho client tự tạo/xóa recording và track tùy ý; không có nghĩa được bỏ toàn bộ Recording Job.

### 3.4 Recording source

Nguồn có thể là on-board Media Profile hoặc Receiver. Project dùng Media Profile `profile_main`. Trong sản phẩm thật, Recording Job sẽ khiến recorder subscribe hoặc pull stream từ nguồn này rồi ghi segment xuống storage.

Project mock không có recorder/storage engine thật. Nó giữ model và state đủ đúng ở tầng giao thức, còn replay dùng stream live từ MediaMTX làm dữ liệu minh họa. Đây là khác biệt quan trọng giữa “conformance mock” và DVR production.

## 4. Recording operations trong project

`RecordingService.cpp` là manual-XML service. Mỗi request được dispatch theo operation name và trả SOAP envelope đúng namespace.

Capability hiện tại:

```xml
<trc:Capabilities
    DynamicRecordings="false"
    DynamicTracks="false"
    DeleteData="false"
    Encoding="H264"
    MaxRecordings="1"
    MaxRecordingJobs="1"
    Options="true"/>
```

### 4.1 Đọc recording

Ví dụ request rút gọn:

```xml
<trc:GetRecordings/>
```

Response logic:

```xml
<trc:RecordingItem>
  <tt:RecordingToken>Recording_0</tt:RecordingToken>
  <tt:Configuration>...</tt:Configuration>
  <tt:Tracks>
    <tt:Track>
      <tt:TrackToken>VIDEO_0</tt:TrackToken>
      <tt:Configuration>
        <tt:TrackType>Video</tt:TrackType>
      </tt:Configuration>
    </tt:Track>
    <tt:Track>
      <tt:TrackToken>META_0</tt:TrackToken>
      <tt:Configuration>
        <tt:TrackType>Metadata</tt:TrackType>
      </tt:Configuration>
    </tt:Track>
  </tt:Tracks>
</trc:RecordingItem>
```

### 4.2 Đọc/đổi configuration

`GetRecordingConfiguration` và `SetRecordingConfiguration` dùng state có mutex. Khi configuration đổi, project gọi `MockSubscriptionManager::fireRecordingConfigChanged()` để SOAP state và Event state cùng thay đổi.

Tương tự, `GetTrackConfiguration`/`SetTrackConfiguration` chỉ chấp nhận `VIDEO_0` hoặc `META_0`. Token khác trả SOAP Fault `ter:NoTrack`; recording token khác trả `ter:NoRecording` qua `FaultBuilder`.

### 4.3 Job mode và event

Khi client gọi:

```xml
<trc:SetRecordingJobMode>
  <trc:JobToken>Job_0</trc:JobToken>
  <trc:Mode>Active</trc:Mode>
</trc:SetRecordingJobMode>
```

Project cập nhật `g_job.mode`, sau đó phát Recording Job State event. Device thật còn phải start pipeline ghi dữ liệu; mock chỉ cập nhật state giao thức.

## 5. Recording Search

### 5.1 Vì sao Search là asynchronous

Search có thể quét nhiều TB dữ liệu nên ONVIF tách thành hai bước:

```text
FindRecordings(...)             → SearchToken
GetRecordingSearchResults(token) → kết quả + SearchState
EndSearch(token)                 → đóng session
```

`SearchState` có thể là Searching, Completed hoặc trạng thái tương ứng trong schema. Client có thể gọi lấy kết quả nhiều lần.

Project chỉ có một recording nên tối giản: Find trả token cố định, lần Get trả toàn bộ kết quả với `SearchState=Completed`.

### 5.2 RecordingInformation

Project trả:

```text
RecordingToken:     Recording_0
EarliestRecording:  2026-07-01T00:00:00Z
LatestRecording:    2026-07-28T00:00:00Z
Tracks:             VIDEO_0, META_0
RecordingStatus:    Stopped
```

Điều kiện nhất quán quan trọng:

```text
EarliestRecording = min(Track.DataFrom)
LatestRecording   = max(Track.DataTo)
```

Một regression từng xảy ra khi `MaximumRetentionTime` bị đặt vào `RecordingInformation` dù field đó thuộc RecordingConfiguration. DTT reject schema và nhiều Search case fail cùng lúc.

### 5.3 RecordingInformationFilter

Client có thể gửi XPath filter:

```xml
<RecordingInformationFilter>
  boolean(//Track[TrackType = "Video"])
</RecordingInformationFilter>
```

Với model project:

```text
Video           → Recording_0
Metadata        → Recording_0
Video+Metadata  → Recording_0
Audio           → empty
Video+Audio     → empty
```

Điểm dễ hiểu nhầm: camera không hỗ trợ Audio nhưng DTT vẫn chạy filter Audio. Nó không yêu cầu camera có Audio; nó kiểm tra engine Search có áp dụng predicate đúng không. Trả empty result là đúng chuẩn. Trả `Recording_0` Video+Metadata cho filter Audio là sai.

Project lưu `g_recordingSearchMatches` ở FindRecordings rồi quyết định có chèn `RecordingInformation` trong GetRecordingSearchResults hay không.

### 5.4 Event Search

Event Search dùng:

```text
FindEvents
GetEventSearchResults
EndSearch
```

Project dựng event lịch sử cho Recording State và Track State. Forward search trả thời gian tăng dần; backward search trả giảm dần. Khi `IncludeStartState=true`, project tạo virtual event cho biết trạng thái ngay tại boundary.

Ví dụ client hỏi tại `2026-07-10T00:00:00Z` track video có data không. Project trả virtual event:

```xml
<tt:Result>
  <tt:RecordingToken>Recording_0</tt:RecordingToken>
  <tt:TrackToken>VIDEO_0</tt:TrackToken>
  <tt:Time>2026-07-10T00:00:00Z</tt:Time>
  <tt:Event>
    ... IsDataPresent=true ...
  </tt:Event>
  <tt:StartStateEvent>true</tt:StartStateEvent>
</tt:Result>
```

## 6. Replay gồm hai tầng

Replay dễ nhầm vì có SOAP và RTSP.

### 6.1 Tầng SOAP

Client gọi `GetReplayUri` với RecordingToken và StreamSetup. SOAP chỉ trả địa chỉ:

```xml
<trp:GetReplayUriResponse>
  <trp:Uri>rtsp://192.168.8.36:8555/replay</trp:Uri>
</trp:GetReplayUriResponse>
```

Ngay cả khi client chọn `Transport.Protocol=HTTP`, URI vẫn là `rtsp://`. HTTP ở đây là RTSP-over-HTTP tunnel, không phải video download bằng HTTP.

`ReplayService.cpp` lấy hostname từ HTTP `Host` header để URI reachable từ máy DTT. Bản đầu trả `127.0.0.1`, khiến DTT kết nối loopback của máy Windows và không thể DESCRIBE.

Service còn hỗ trợ:

```text
GetServiceCapabilities
GetReplayConfiguration
SetReplayConfiguration
```

Session timeout mặc định là `PT60S`. Reverse playback được khai false.

### 6.2 Tầng RTSP/RTP

Sau khi có URI, client thực hiện:

```text
OPTIONS
DESCRIBE
SETUP
PLAY
PAUSE hoặc PLAY tiếp
TEARDOWN
```

Một session minh họa:

```text
DESCRIBE rtsp://device:8555/replay

SETUP rtsp://device:8555/replay/trackID=0
Transport: RTP/AVP/TCP;unicast;interleaved=0-1
Require: onvif-replay

PLAY rtsp://device:8555/replay/
Session: ...
Require: onvif-replay
Range: clock=20260701T000000.000Z-
```

`Require: onvif-replay` báo rằng client cần semantics replay ONVIF. Server không được trả 551 Unsupported nếu nó quảng bá Replay.

## 7. Range, Rate-Control, Immediate và Pause

### 7.1 Open Range

```text
Range: clock=20260701T000000.000Z-
```

Nghĩa là phát từ mốc bắt đầu tới khi hết dữ liệu hoặc client dừng.

### 7.2 Bounded Range

```text
Range: clock=20260701T000000.000Z-20260701T000004.000Z
```

Server chỉ được phát packet có media time trong khoảng này. Project cho phép NTP bằng end, nhưng bỏ packet có NTP `After(end)`.

### 7.3 Rate-Control

```text
Rate-Control: yes
```

Server điều tiết tốc độ phát theo timeline, thường gần thời gian thực.

```text
Rate-Control: no
```

Client chấp nhận server gửi nhanh nhất có thể và tự điều tiết. Project dùng upstream live đã paced theo thời gian thực; DTT Rate-Control cases vẫn pass. Đây chưa phải implementation đầy đủ cho archive reader production, nơi `no` nên đọc segment nhanh nhất theo khả năng I/O.

### 7.4 Immediate seek

Client đang PLAY rồi gửi PLAY mới:

```text
Range: clock=20260701T000005.000Z-
Immediate: yes
```

Server phải hủy location cũ và chuyển ngay sang location mới. Packet đầu ở location mới phải có D (discontinuity) flag trong ONVIF RTP extension.

Project reset base timestamp/NTP/CSeq và arm D theo payload type. Packet đầu video và metadata có D; packet sau clear.

### 7.5 Pause

RTSP PAUSE dừng phát nhưng giữ session; PLAY không Range tiếp tục từ pause point. gortsplib xử lý state/reader activation của PAUSE. Project không có archive cursor thật, nhưng native stream pause/resume đủ cho các case DTT hiện tại.

## 8. RTP Replay extension 0xABAC

Đây là phần khác biệt lớn nhất giữa real-time stream và ONVIF replay.

RTP bình thường có header:

```text
V/P/X/CC | M/PT | Sequence | RTP Timestamp | SSRC | Payload
```

Replay thêm extension profile-specific:

```text
X=1
Profile ID = 0xABAC
Length     = 3 words
Content    = 12 bytes
```

Byte layout:

```text
Offset  Size  Ý nghĩa
0       8     NTP timestamp 64-bit fixed-point
8       1     Flags C/E/D/T + reserved bits
9       1     low byte của PLAY CSeq
10      2     zero padding
```

Flags:

```text
C = 0x80  clean/sync point
E = 0x40  end của đoạn ghi liên tục
D = 0x20  discontinuity
T = 0x10  terminal frame
```

Ví dụ PLAY có `CSeq: 261`. Low byte là:

```text
261 decimal = 0x0105
CSeq trong extension = 0x05
```

Nếu seek Immediate, flags byte có thể là `0x20`:

```text
NTP[8] | 20 | 05 | 00 00
```

### 8.1 NTP khác RTP Timestamp thế nào

RTP Timestamp là clock tương đối của codec. H264 thường dùng 90 kHz. Nếu frame đầu có RTP timestamp `R0`, packet sau có `R`, media delta là:

```text
delta_seconds = (R - R0) / 90000
NTP(packet)   = RangeStart + delta_seconds
```

NTP là UTC tuyệt đối dạng fixed-point 64 bit: 32 bit giây từ epoch 1900 và 32 bit phần lẻ. Project cộng offset `2208988800` để đổi Unix epoch 1970 sang NTP epoch 1900.

Ví dụ RangeStart là 00:00:00 và RTP delta là 180000:

```text
180000 / 90000 = 2 giây
NTP packet = 00:00:02
```

DTT kiểm tra NTP tăng đơn điệu và không vượt bounded Range.

## 9. Vì sao phải packetize lại H264

Upstream `/main` có packet gần chạm giới hạn 1472 byte. Replay thêm 4 byte extension header và 12 byte extension content. Nếu gắn thẳng:

```text
1472 + 16 = 1488 bytes
```

gortsplib giới hạn outbound RTP/RTCP packet ở 1472 và trả `short buffer`. Tăng giới hạn không phải hướng đúng vì UDP MTU.

Project xử lý riêng `/replay`:

```text
upstream RTP H264
      │
      ▼
decode RTP thành H264 access unit
      │
      ▼
encode/packetize lại với PayloadMaxSize=1434
      │
      ▼
gắn extension 0xABAC
      │
      ▼
packet cuối ≤ 1472 bytes
```

`/main` không qua pipeline này, nên Media/RTSS real-time không bị regression.

## 10. Cách project map chuẩn sang code

| Yêu cầu | File / component | Implementation |
|---|---|---|
| Profile G discovery | Device/Discovery services | Scope Profile/G, advertise 3 service namespace |
| Recording model | `RecordingService.cpp` | Recording_0, VIDEO_0, META_0, Job_0 |
| Recording configuration | `RecordingService.cpp` | In-memory state có mutex, Get/Set + event |
| Job control | `RecordingService.cpp` | Create/Delete/Get/Set/Mode/State |
| Recording information | `SearchService.cpp` | Fixed timeline và track list |
| XPath recording filter | `SearchService.cpp` | Audio predicates trả empty, filter phù hợp trả Recording_0 |
| Event history | `SearchService.cpp` | Forward/backward, virtual start state, MaxMatches |
| Replay URI | `ReplayService.cpp` | Host request + port 8555 + `/replay` |
| Replay timeout/caps | `ReplayService.cpp` | PT60S, Reverse=false, TCP=true |
| RTSP auth/control | `main.go` | gortsplib handler, Digest, DESCRIBE/SETUP/PLAY |
| Replay timing | `main.go/replayState` | CSeq, Range start/end, RTP→NTP mapping |
| ONVIF RTP extension | `main.go/replayState.packet` | profile 0xABAC, 12-byte content |
| Immediate D flag | `main.go/replayState` | one-shot per payload type |
| MTU-safe H264 | `main.go/relayOnce` | decode AU, packetize payload max 1434 |
| Real-time isolation | `main.go` | `/main` và `/replay` là stream riêng |

## 11. End-to-end ví dụ

Giả sử VMS muốn xem bốn giây đầu của `Recording_0`.

### Bước 1: lấy thông tin

```xml
<tse:GetRecordingInformation>
  <tse:RecordingToken>Recording_0</tse:RecordingToken>
</tse:GetRecordingInformation>
```

Device trả timeline 01/07 đến 28/07 và hai track Video/Metadata.

### Bước 2: lấy URI

```xml
<trp:GetReplayUri>
  <trp:StreamSetup>
    <tt:Stream>RTP-Unicast</tt:Stream>
    <tt:Transport><tt:Protocol>UDP</tt:Protocol></tt:Transport>
  </trp:StreamSetup>
  <trp:RecordingToken>Recording_0</trp:RecordingToken>
</trp:GetReplayUri>
```

Device trả:

```text
rtsp://device:8555/replay
```

### Bước 3: mở RTSP

```text
DESCRIBE → SDP có H264 track và ONVIF metadata track
SETUP    → chọn UDP hoặc interleaved TCP
PLAY     → Range absolute + Require onvif-replay
```

### Bước 4: device tạo replay packet

Với packet video đầu:

```text
RangeStart = 2026-07-01 00:00:00 UTC
RTP delta  = 0
NTP        = RangeStart
Flags      = 0
CSeq       = low byte của PLAY
```

Các packet tiếp theo lấy NTP từ RTP delta. Khi NTP vượt 00:00:04, project không gửi nữa.

### Bước 5: seek

Client gửi PLAY Immediate tới 00:00:05. Project reset base và packet đầu mới có:

```text
NTP   = 2026-07-01 00:00:05 UTC
D     = 1
CSeq  = CSeq của PLAY mới
```

## 12. Phần nào là mock, phần nào gần production

### Đúng chuẩn và có thể tái sử dụng

Các phần discovery/service namespace, SOAP response/fault shape, token validation, XPath behavior no-audio, RTSP URI semantics, RTP extension layout, NTP conversion, bounded Range và D flag đều là kiến thức áp dụng cho production.

### Chỉ phù hợp mock/conformance

Project không đọc media từ storage theo Range. `/replay` lấy live `/main` rồi gắn timeline giả. Recording/track/job nằm trong RAM và mất khi restart. Search có một recording, token cố định và trả kết quả đồng bộ. Event history được dựng sẵn. Rate-Control=no chưa có fast archive reader. Session replay state dùng một state chung, phù hợp DTT tuần tự nhưng không đủ cho nhiều replay client đồng thời.

## 13. Nếu thay mock bằng DVR thật

Một implementation production cần bổ sung:

```text
Storage index
  recording_token → segments → start/end → codec config → file offsets

Recorder
  media source → segment writer → index → retention cleanup

Search engine
  query/filter/time range → paginated asynchronous results

Replay session riêng
  session → recording → cursor → range → speed → pause/seek state

Packetizer
  đọc access unit thật → RTP timestamp/NTP → 0xABAC → RTCP sender reports
```

Mỗi RTSP session phải có state riêng thay vì `handler.replay` global. Range phải map tới segment/file offset thật. Gap phải tạo discontinuity phù hợp. Rate-Control, reverse playback nếu quảng bá, C/E/T flags và clean-point detection cần dựa trên media index/I-frame thật.

## 14. Các lỗi tư duy nên tránh

Không coi RecordingToken là filename. Không coi GetReplayUri là nơi phát video. Không dùng RTP timestamp thay NTP UTC. Không thêm Audio giả chỉ để pass filter. Không gắn extension Replay vào `/main`. Không trả mọi recording như một “superset” khi có XPath filter. Không quảng bá reverse/dynamic/audio nếu backend không làm thật.

## 15. Kết luận

Recording của Profile G là data model và control plane cho timeline lưu trữ. Search là index/query plane. Replay SOAP chỉ giúp tìm endpoint; RTSP/RTP mới là data plane. RTP extension 0xABAC nối packet media với UTC recording timeline và lệnh PLAY đã tạo nó.

Project hiện mô phỏng đầy đủ hành vi quan sát được để đạt 313/313 DTT: một recording cố định Video+Metadata, search/event timeline nhất quán, replay URI reachable, RTSP transports hoạt động, RTP có NTP/CSeq/D flag đúng, bounded Range dừng đúng và stream real-time được cô lập khỏi replay. Tuy nhiên nó vẫn là conformance mock, chưa phải recorder/DVR lưu và đọc archive thật.