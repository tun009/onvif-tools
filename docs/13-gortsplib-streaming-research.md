# gortsplib streaming research

Ngay 2026-07-13, muc tieu la nghien cuu kha nang dung `gortsplib` de tao luong RTSP that thay cho MediaMTX + ffmpeg, phuc vu Profile M metadata streaming va cac case RTSP-over-HTTP/TCP dang fail trong DTT.

## Ket luan ngan

`gortsplib` la huong kha thi nhat de thay MediaMTX trong bai toan nay.

Ly do:

- Day la RTSP client/server library dung trong chinh MediaMTX.
- Server co san handler cho `DESCRIBE`, `SETUP`, `PLAY`, `RECORD`.
- Server support play qua UDP, UDP multicast, TCP interleaved.
- Server support tunneling `RTSP-over-HTTP` va `RTSP-over-WebSocket`.
- Co API de tu tao SDP/session description va tu write RTP packet ra stream.
- Co format H264, M-JPEG, va KLV. Voi ONVIF metadata, neu format co san khong match 100%, co the dung format generic/custom RTP va tu tao payload XML.

Nguon chinh:

- GitHub: https://github.com/bluenviron/gortsplib
- API docs: https://pkg.go.dev/github.com/bluenviron/gortsplib/v5

## Trang thai hien tai cua project

Mock stream hien tai nam o:

- `mock-camera-backend/mock-camera-backend/rtsp/mediamtx.yml`
- `mock-camera-backend/mock-camera-backend/scripts/start_streams.sh`

MediaMTX chay port `8554`, cac path:

- `/main`: ffmpeg `testsrc2` -> H264 3840x2160@30
- `/jpeg`: ffmpeg `testsrc2` -> MJPEG 1920x1080@30
- `/sub1`: ffmpeg `testsrc2` -> H264 1280x720@15
- `/sub2`: ffmpeg `testsrc2` -> H264 640x480@10

Khong co RTP metadata track ONVIF.

DTT `m21.xml` con 5 fail RTSS do HTTP tunnel:

- DTT goi `GetStreamUri` voi protocol HTTP.
- ONVIF server tra `http://192.168.8.36:8080/main` hoac `/jpeg`.
- DTT thu RTSP-over-HTTP tunnel vao port 8080.
- ONVIF SOAP server khong phai RTSP tunnel server nen fail `Network error 10057`.

Profile M van do o `Metadata Streaming` vi thieu RTSP metadata stream that su.

## Muc tieu cua server moi

Thay vi MediaMTX + ffmpeg, tao binary moi bang Go, tam goi:

```text
mock-rtsp-server
```

Server nay can serve cac path:

```text
rtsp://<ip>:8554/main
rtsp://<ip>:8554/jpeg
rtsp://<ip>:8554/sub1
rtsp://<ip>:8554/sub2
rtsp://<ip>:8554/metadata
```

De pass Profile M tot hon, co the can them mode:

```text
rtsp://<ip>:8554/main
```

voi SDP gom 2 media lines:

```text
m=video ...
a=rtpmap:<pt> H264/90000

m=application ...
a=rtpmap:<pt> vnd.onvif.metadata/90000
```

Tuy theo DTT no chon profile nao, ta co 2 option:

1. Profile metadata rieng: `/metadata` chi co `m=application`.
2. Profile video+metadata: `/main` co ca video va metadata trong cung RTSP session.

Profile M spec nghieng ve "selected Media Profile" co MetadataConfiguration; neu profile co ca video source + metadata config thi DTT co the mong stream co metadata track song song video. Nen POC nen lam ca 2 path de test nhanh.

## SDP metadata can co

Muc tieu DTT thuong tim metadata media line dang:

```text
m=application 0 RTP/AVP 107
a=rtpmap:107 vnd.onvif.metadata/90000
a=control:trackID=metadata
```

RTP payload la XML ONVIF metadata:

```xml
<tt:MetadataStream xmlns:tt="http://www.onvif.org/ver10/schema">
  <tt:VideoAnalytics>
    <tt:Frame UtcTime="2026-07-13T08:00:00Z">
      <tt:Object ObjectId="1">
        <tt:Appearance>
          <tt:Shape>
            <tt:BoundingBox left="0.10" top="0.10" right="0.40" bottom="0.60"/>
          </tt:Shape>
          <tt:Class>
            <tt:Type Likelihood="0.90">Human</tt:Type>
          </tt:Class>
        </tt:Appearance>
      </tt:Object>
    </tt:Frame>
  </tt:VideoAnalytics>
</tt:MetadataStream>
```

Can gui lap lai 1-5 fps. Timestamp RTP dung clock 90000. Marker bit nen set true moi XML frame.

## Mapping voi gortsplib

Server co the dua tren example `server-play-format-h264-from-disk`:

- Tao `gortsplib.Server`.
- Tao `description.Session`.
- Tao `description.Media` cho video va metadata.
- Tao `gortsplib.ServerStream`.
- Handler:
  - `OnDescribe`: return stream theo path.
  - `OnSetup`: return stream.
  - `OnPlay`: OK.
  - `OnGetParameter`/`OnSetParameter`: OK de keepalive va `SetSynchronizationPoint`.
- Goroutine dinh ky goi `stream.WritePacketRTP(media, pkt)`.

Voi video:

- H264: co the dung `format.H264`.
- M-JPEG: co the dung `format.MJPEG`.
- De POC nhanh, co the bo video payload that va chi uu tien metadata; nhung de thay MediaMTX that su thi can phat video co frame hop le.

Voi metadata:

- Thu dau tien: dung format generic/custom de render SDP `vnd.onvif.metadata/90000`.
- Neu API generic khong render dung ten encoding, can patch/extend local format hoac viet SDP description thu cong.
- Khong nen dung KLV cho ONVIF metadata neu SDP thanh `SMPTE336M`/KLV, vi DTT co the khong coi la ONVIF metadata.

## RTSP-over-HTTP tunnel

Day la diem an tien lon cua `gortsplib`.

Theo README/API docs, server support tunneling `RTSP-over-HTTP`. Neu ONVIF `GetStreamUri(Protocol=HTTP)` tra ve:

```text
http://192.168.8.36:8554/main
```

thay vi:

```text
http://192.168.8.36:8080/main
```

DTT se tunnel vao dung RTSP server. Neu `gortsplib` handle dung GET/POST tunnel theo ONVIF tool, 5 fail RTSS co kha nang pass cung luc.

Do do, khi POC gortsplib chay o port 8554, can sua `GetStreamUri` HTTP mode o ONVIF module:

- Media1 legacy: `MediaLegacyHandler::handleGetStreamUri`
- Media2: `Media2Service::GetStreamUri`

Tra HTTP URL ve `rtspPort` thay vi `httpPort`.

## Lo trinh de-risk

Khong nen thay MediaMTX toan bo ngay lap tuc. Lam tung nac:

### POC 1: metadata-only RTSP

Tao Go server tam thoi, serve:

```text
rtsp://192.168.8.36:8555/metadata
```

SDP chi co `m=application` metadata. Test bang DTT hoac client RTSP xem DESCRIBE co dung SDP khong.

Pass criteria:

- DESCRIBE thay `vnd.onvif.metadata/90000`.
- SETUP/PLAY qua UDP va TCP interleaved OK.
- RTP packet co XML parse duoc.

### POC 2: HTTP tunnel

Bat HTTP tunnel tren cung server, test URL:

```text
http://192.168.8.36:8555/metadata
```

Pass criteria:

- DTT/client co the DESCRIBE qua RTSP-over-HTTP.
- Khong con `Network error 10057`.

### POC 3: video + metadata

Serve path:

```text
rtsp://192.168.8.36:8555/main
```

SDP co H264 + ONVIF metadata. Video co the dung:

- Loop file H264 Annex-B nho co san.
- Hoac generate H264 bang ffmpeg pipe/child process roi packetize bang Go.
- Hoac tam thoi van de ffmpeg publish video vao server custom neu gortsplib server nhan RECORD, nhung huong nay lai tang complexity.

Pass criteria:

- DTT Profile M metadata streaming xanh hon.
- RTSS video TCP/UDP van pass.

### Tich hop that

Khi POC pass:

- Them `rtsp-server/` trong `mock-camera-backend`.
- Them `go.mod`, `cmd/mock-rtsp-server/main.go`.
- Makefile build them Go binary neu co Go tren server.
- `scripts/start_streams.sh` chuyen tu MediaMTX sang `mock-rtsp-server`.
- `scripts/stop_streams.sh`, `check_streams.sh` update theo binary moi.
- ONVIF `GetStreamUri` update HTTP tunnel port.

## Rui ro can kiem tra som

1. Go version tren server:
   - Da kiem tra server `tomotech@192.168.8.36`: ban dau `go not found`.
   - Da cai Go user-local, khong dung sudo/apt:
     - `~/tools/go1.26.5/bin/go`
     - Version: `go1.26.5 linux/amd64`
     - SHA256 verified voi checksum tu `go.dev/dl`.
   - Da tao module tam `/tmp/gortsplib_check` va `go get github.com/bluenviron/gortsplib/v5@latest` thanh cong.
   - Version dependency da tai: `github.com/bluenviron/gortsplib/v5 v5.6.1`.

2. SDP encoding name:
   - DTT co the yeu cau chinh xac `vnd.onvif.metadata/90000`.
   - Neu gortsplib generic khong cho custom encoding name dung, can sua nho.

3. Payload fragmentation:
   - XML metadata dai co the vuot MTU.
   - POC nen giu XML ngan truoc; sau do moi them fragment neu can.

4. Video replacement:
   - Metadata-only de pass Profile M chua chac thay duoc MediaMTX.
   - De pass full RTSS, video RTP phai hop le H264/JPEG nhu hien tai.

5. `SetSynchronizationPoint`:
   - ONVIF Media `SetSynchronizationPoint` va Profile M yeu cau metadata stream mo XML document moi.
   - Can co IPC/control nho hoac signal file de ONVIF SOAP server bao RTSP server inject sync metadata frame.

6. RTSP-over-HTTP tunnel trong gortsplib:
   - Da kiem tra module cache tren server: `gortsplib/v5.6.1` co `server_tunnel_http.go`.
   - Co test server-side `TestServerTunnelHTTP`.
   - Header tunnel duoc detect:
     - `Accept: application/x-rtsp-tunnelled` cho HTTP GET
     - `Content-Type: application/x-rtsp-tunnelled` cho HTTP POST
     - `X-Sessioncookie` de ghep GET/POST tunnel
   - Khong co field `HTTPAddress` rieng trong `Server`; HTTP tunnel duoc xu ly tren cung TCP listener `RTSPAddress`.
   - Vay `GetStreamUri(Protocol=HTTP)` nen tra `http://<ip>:8554/<path>` neu dung gortsplib thay MediaMTX.

## De xuat quyet dinh

Dung `gortsplib` theo huong custom RTSP server la kha thi va dang lam nhat.

Khong nen tiep tuc co sua MediaMTX + ffmpeg cho Profile M, vi thieu control voi `m=application` metadata track va HTTP tunnel. Phuong an tot nhat la:

1. Lam POC `mock-rtsp-server` metadata-only tren port 8555.
2. Test DTT metadata streaming rieng.
3. Neu SDP/RTP OK, mo rong sang path `/main` co video + metadata.
4. Sau do moi thay MediaMTX tren port 8554.

Doc nay chi la nghien cuu/plan, chua sua runtime code.
