# Tổng kết triển khai ONVIF Profile G

## Kết quả cuối

ONVIF Mock Camera đã đạt **313/313 test case Profile G** trên ONVIF Device Test Tool 25.12. Mốc code cuối là commit `0bb932bf4eb497201d39b5fac5a430659a55beb4`.

Toàn bộ nhóm Recording, Recording Search, Replay và Event liên quan Profile G đều pass. Camera vẫn khai đúng phạm vi không hỗ trợ audio; không thêm audio source, audio encoder, audio track hay RTP audio giả.

## Profile G là gì

Profile G dành cho thiết bị và client có chức năng ghi hình, tìm kiếm dữ liệu đã ghi và phát lại dữ liệu đó. Với device, các khối chính gồm Recording Control để quản lý recording, track và recording job; Recording Search để lấy thông tin recording, lọc recording và tìm event lịch sử; Replay Control để cấp URI và phát lại video/metadata theo thời gian tuyệt đối; Event để báo thay đổi trạng thái và cấu hình recording.

Audio không bắt buộc với mọi Profile G device. Nó là chức năng có điều kiện: nếu device hỗ trợ audio thì phải cung cấp các hành vi audio tương ứng. Device không hỗ trợ audio vẫn phải thực thi đúng các XPath search filter liên quan Audio; kết quả đúng là danh sách rỗng khi không recording nào có audio track.

## Phạm vi thiết bị mock

Thiết bị dùng một recording cố định, không dynamic:

```text
Recording_0
├── VIDEO_0    Video
└── META_0     Metadata

Job_0          Recording job cố định
Audio          Unsupported
Reverse replay Unsupported
```

Recording có khoảng thời gian giả lập từ `2026-07-01T00:00:00Z` đến `2026-07-28T00:00:00Z` và trạng thái `Stopped`. Replay hỗ trợ RTP/UDP, RTP/RTSP/TCP và RTP/RTSP/HTTP/TCP. Capability Replay khai `ReversePlayback=false`.

## Kiến trúc

Dự án có hai repo trong cùng cây git. `onvif-module` xử lý SOAP/gSOAP, authentication, discovery và các service ONVIF trên cổng 8080. `mock-camera-backend` cung cấp backend giả và streaming. Hai phần giao tiếp qua Unix socket `/tmp/mock-camera.sock`, nhưng Replay URI không cần thêm IPC message: SOAP chỉ trả URI, client kết nối trực tiếp tới RTSP relay.

Luồng streaming:

```text
GStreamer/ffmpeg publisher
        │
        ▼
MediaMTX :8554
        │
        ▼
gortsplib relay :8555
        │
        ├── /main    real-time stream, giữ nguyên
        └── /replay  replay stream riêng, có RTP extension ONVIF
```

Ba service Profile G được làm theo kiểu manual XML, không regenerate gSOAP:

```text
/onvif/recording
/onvif/search
/onvif/replay
```

Cách này tránh sửa `external/gsoap/` và tránh làm lệch shared contract `include/interface/` giữa hai repo.

## Recording Control

Đã thêm service discovery, namespace và capability cho Recording Control. Device quảng bá scope `onvif://www.onvif.org/Profile/G`, đăng ký Recording/Search/Replay service và trả đúng cả hai nhánh `GetServices` có hoặc không có capability.

Recording Service triển khai bộ operation bắt buộc phù hợp scope fixed recording. Các operation lấy danh sách/cấu hình trả nhất quán `Recording_0`, `VIDEO_0`, `META_0`, `Job_0`. Invalid token trả SOAP Fault qua `FaultBuilder`. Job state, recording state và cấu hình được giữ nhất quán giữa response SOAP và event.

Dynamic recording và dynamic tracks không được quảng bá. Tuy nhiên các operation recording job thuộc phần bắt buộc của device vẫn được triển khai dù recording không dynamic.

## Recording Events

Đã thêm các topic và notification liên quan recording, track, job và thay đổi cấu hình. Các event dùng đúng source/data items, `PropertyOperation`, `ElementItem` và topic namespace mà DTT yêu cầu.

Các lỗi từng gặp gồm sai schema result, thiếu `Information` trong JobState, thiếu notification khi cấu hình đổi và connection reset bất định ở Subscribe. Sau nhiều full run, toàn bộ 31 Event case pass. Connection reset từng đổi qua lại giữa `EVENT-2-1-24`, `EVENT-2-1-27` và `EVENT-2-1-29`, cho thấy lỗi timing/framing chứ không phải filter semantic; trạng thái cuối pass toàn bộ.

## Recording Search

Search Service triển khai `GetRecordingSummary`, `GetRecordingInformation`, `GetMediaAttributes`, `FindRecordings`, `GetRecordingSearchResults`, `FindEvents`, `GetEventSearchResults` và `EndSearch`. Search token và active state được kiểm tra; invalid token trả Fault đúng.

Recording information trả đúng source/content, earliest/latest time, track list và status. `MaximumRetentionTime` không được đưa vào `RecordingInformation` vì không thuộc schema đó; đây từng là nguyên nhân regression lớn ở g3.

Event Search xử lý đầy đủ hướng tìm kiếm và start-state:

```text
Forward:  StartPoint < EndPoint, kết quả tăng dần
Backward: StartPoint > EndPoint, kết quả giảm dần
```

Virtual start-state event được tạo đúng tại StartPoint cho forward và tại các endpoint DTT yêu cầu cho backward. Non-virtual event set được giữ giống nhau giữa hai hướng. Track state có cả `VIDEO_0` và `META_0`; `MaxMatches` được tôn trọng.

Lỗi cuối của Search nằm ở XPath RecordingInformationFilter. Code ban đầu “parse-cho-qua” và luôn trả `Recording_0`, nên filter Audio nhận recording Video+Metadata và fail. Fix cuối lưu việc filter có match recording hay không. Các kết quả hiện tại:

```text
No filter       → Recording_0
Video           → Recording_0
Metadata        → Recording_0
Video+Metadata  → Recording_0
Audio           → empty result
Video+Audio     → empty result
```

`RecordingInformationFilter` có thuộc tính `xmlns`, nên helper cũ không trích được nội dung. Commit cuối kiểm tra predicate trong raw XML của riêng operation `FindRecordings`, sau đó `GetRecordingSearchResults` trả `SearchState=Completed` với danh sách rỗng cho filter yêu cầu Audio. Camera vẫn không quảng bá Audio.

## Replay SOAP và connectivity

`GetReplayUri` ban đầu trả `rtsp://127.0.0.1:8555/replay`, làm DTT trên máy khác kết nối loopback của chính nó. Relay cũng chưa có path `/replay`, nên toàn bộ Replay fail trước DESCRIBE.

Fix connectivity lấy hostname từ HTTP `Host` header sau khi validate ký tự và trả:

```text
rtsp://<DUT-host>:8555/replay
```

URI vẫn dùng scheme RTSP kể cả khi `Transport.Protocol=HTTP`, vì HTTP chỉ là tunnel cho RTSP control. Relay mở `/replay`, hỗ trợ DESCRIBE, SETUP và PLAY với `Require: onvif-replay`. Sau fix, DTT đi qua toàn bộ RTSP handshake nhưng vẫn báo không có frame; probe packet chứng minh relay có gửi H264, còn DTT loại packet vì thiếu Replay RTP extension.

## RTP Replay extension 0xABAC

ONVIF Replay yêu cầu RTP header extension profile-specific:

```text
Extension profile: 0xABAC
Length:            3 words = 12 bytes
Bytes 0..7:        NTP timestamp 64-bit fixed-point
Byte 8:            C/E/D/T flags
Byte 9:            low byte của PLAY CSeq
Bytes 10..11:      zero padding
```

Đã tách `/replay` thành `pathStream` riêng để không thay đổi `/main`. PLAY parser đọc CSeq và `Range: clock=`. Replay state ánh xạ RTP timestamp đầu tiên của từng payload type thành thời gian bắt đầu tuyệt đối; NTP sau đó tăng theo clock 90 kHz và không giảm.

Khi thêm extension 16 byte gồm header và payload, các RTP packet H264 lớn vượt giới hạn 1472 byte của gortsplib, gây `short buffer` và decode corruption. Fix đúng không tăng server packet limit; video Replay được decode thành H264 access unit rồi packetize lại với payload tối đa 1434 byte trước khi thêm extension. `/main` không qua bước này. Kết quả ffprobe của cả `/main` và `/replay` sạch, không còn short buffer hoặc decode error.

DTT sau đó pass 15/19 Replay case, bao gồm UDP, TCP, HTTP tunnel, I-Frames, Rate-Control và PAUSE.

## Bounded Range và Immediate seek

Bốn Replay case cuối lộ hai yêu cầu riêng.

Với bounded Range:

```text
Range: clock=20260701T000000.000Z-20260701T000004.000Z
```

Code cũ chỉ đọc start và phát vô hạn. Fix parse cả optional end, gửi packet có NTP bằng end nhưng chặn packet có NTP lớn hơn end. Cơ chế áp dụng cho video và metadata. Range mở `start-` reset end và tiếp tục phát bình thường.

Với Immediate seek:

```text
PLAY ...
Range: clock=20260701T000005.000Z-
Immediate: yes
```

Packet đầu của location mới phải có D flag. Fix arm discontinuity theo payload type; packet đầu mỗi track sau PLAY Immediate có `D=0x20`, packet kế tiếp tự clear. CSeq và NTP được reset theo PLAY mới.

Probe trên DUT xác nhận bounded playback dừng đúng 4.0 giây; open Range tiếp tục qua 5 giây; Immediate packet đầu có D/CSeq/NTP đúng; packet sau không còn D.

## Tiến trình DTT

| Report | Pass | Fail | Trọng tâm còn lỗi |
|---|---:|---:|---|
| g1 | 267 | 46 | Recording 16, Search 7, Replay 21, Event 2 |
| g2 | 278 | 35 | Recording/Search state và Replay |
| g3 | 269 | 53 | Regression schema Search |
| g4 | 281 | 32 | Recording events, Search, Replay |
| g5 | 281 | 32 | Recording/Search/Event |
| g6 | 289 | 24 | Search 5, Replay 19 |
| g7 | 288 | 25 | Search 5, Replay 19, Event 1 |
| g8 | 288 | 25 | Search 5, Replay 19, Event 1 |
| g9 | 291 | 22 | Search Audio 2, Replay 19, Event 1 |
| g10 | 291 | 22 | Replay qua handshake, thiếu RTP 0xABAC |
| g11 | 307 | 6 | Range 2, Immediate 2, Audio filter 2 |
| g12 | 311 | 2 | Audio filter semantic |
| Final | **313** | **0** | Hoàn thành |

g3 là bài học quan trọng: một thay đổi schema sai có thể làm số test tăng do DTT chọn thêm case nhưng số pass giảm mạnh. Sau đó quy trình được siết lại: fix từng nhóm nhỏ, chạy full conformance sau mỗi đợt và không coi isolated test là đủ.

## Các commit chính

Các commit tiêu biểu theo thứ tự phát triển:

```text
0013439  emit recording events and search states
9f3514f  correct event search result schema
36b5eae  add video track search events
4945d2a  order event search results
269d9b7  handle event search direction and state
1de5016  expose replay RTSP endpoint
7e1c041  add replay RTP timestamps
e6bff99  fit replay RTP extension
b89fb9a  repacketize replay within MTU
b0de2c5  repacketize replay RTP safely
7759002  honor replay range and seek
5de9d96  apply recording audio filters
0bb932b  parse attributed search filters
```

Một số commit trung gian là bước thử và được commit sau sửa tiếp để giữ lịch sử điều tra. Trạng thái production cuối nằm ở `0bb932b`.

## Quy trình build và deploy đã dùng

Mọi sửa đổi được thực hiện local-first. Build check C++ dùng bản copy `/tmp/build_check` trên DUT và chỉ upload file đã đổi vào bản copy; không ghi đè source trong repo server. Sau khi pass build, code được commit local, chuyển bằng git bundle khi local không có GitHub credential, fast-forward trên server rồi push bằng SSH key của server.

Relay Go được build bằng container `golang:1.25-bookworm` vì `go.mod` yêu cầu Go 1.25. Service chỉ restart khi bị ảnh hưởng, kill theo exact PID. Backend được start trước ONVIF khi cần restart cả hai. Sau deploy luôn verify process, owner port, git hash, log, SOAP bằng `requests` với `Connection: close`, RTSP bằng probe packet và ffprobe.

## Bài học kỹ thuật

Profile conformance phụ thuộc vào hành vi được quảng bá, không chỉ operation tồn tại. Capability, service discovery, response schema, event payload và stream byte layout phải nhất quán.

Không được trả superset tùy tiện cho XPath filter. Filter Audio trên device no-audio phải trả empty result, không phải recording không match.

Replay không thể chỉ alias real-time RTSP. DTT xác thực `Require: onvif-replay`, absolute Range, NTP, CSeq, D flag và packet size. Packet có payload video hợp lệ nhưng thiếu extension vẫn bị DTT loại trước render.

Tách `/replay` khỏi `/main` là quyết định chống regression quan trọng. Mọi biến đổi Replay chỉ nằm trên stream riêng; toàn bộ test Media/RTSS đã pass được giữ nguyên.

Full conformance là tiêu chuẩn xác nhận cuối. State pollution, reconnect timing và Subscribe framing từng khiến kết quả dao động; reset baseline và chạy full suite giúp phân biệt regression thật với lỗi bất định.

## Trạng thái bàn giao

```text
Profile G DTT 25.12: 313/313 pass
Final commit:         0bb932bf4eb497201d39b5fac5a430659a55beb4
Recording model:      Recording_0 = VIDEO_0 + META_0
Audio:                Unsupported, khai báo và hành vi nhất quán
Reverse playback:     Unsupported
Dynamic recording:    Unsupported
Replay endpoint:      rtsp://<DUT-host>:8555/replay
```

Profile G đã hoàn thành trong scope thiết bị mock, không còn case failed.