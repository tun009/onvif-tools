# gortsplib metadata POC result

Ngay 2026-07-13 da tao POC tren server Ubuntu `tomotech@192.168.8.36`.

## Vi tri tren server

```text
/tmp/gortsplib_metadata_poc
```

Binary:

```text
/tmp/gortsplib_metadata_poc/mock-rtsp-metadata-poc
```

Log:

```text
/tmp/mock-rtsp-metadata-poc.log
```

PID file:

```text
/tmp/mock-rtsp-metadata-poc.pid
```

Go user-local:

```text
/home/tomotech/tools/go1.26.5/bin/go
```

## Trang thai POC

POC dang chay rieng port `8555`, khong dung vao MediaMTX port `8554`.

Listener:

```text
tcp *:8555
udp *:8050
udp *:8051
```

URL:

```text
rtsp://192.168.8.36:8555/metadata
http://192.168.8.36:8555/metadata  # RTSP-over-HTTP tunnel
```

## Ket qua da verify

### 1. RTSP OPTIONS

Server tra:

```text
RTSP/1.0 200 OK
Public: DESCRIBE, SETUP, PLAY, GET_PARAMETER, SET_PARAMETER, TEARDOWN
Server: gortsplib
```

### 2. RTSP DESCRIBE

Server tra SDP dung metadata ONVIF:

```text
v=0
o=- 0 0 IN IP4 127.0.0.1
s=ONVIF metadata POC
c=IN IP4 0.0.0.0
t=0 0
m=application 0 RTP/AVP 107
a=control:trackID=0
a=rtpmap:107 vnd.onvif.metadata/90000
```

### 3. SETUP/PLAY TCP interleaved

Da SETUP/PLAY thanh cong:

```text
Transport: RTP/AVP/TCP;unicast;interleaved=0-1;ssrc=...
RTP-Info: url=rtsp://127.0.0.1:8555/metadata/trackID=0;seq=...;rtptime=...
```

Da nhan RTP interleaved packet channel 0, payload type 107, payload XML:

```xml
<tt:MetadataStream xmlns:tt="http://www.onvif.org/ver10/schema">
  <tt:VideoAnalytics>
    <tt:Frame UtcTime="...">
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

### 4. RTSP-over-HTTP tunnel DESCRIBE

Dung `gortsplib.Client` voi:

```go
Client{
    Scheme: u.Scheme,
    Host: u.Host,
    Tunnel: gortsplib.TunnelHTTP,
}
```

Ket qua:

```text
status 200
```

Va SDP tra ve van dung:

```text
m=application 0 RTP/AVP 107
a=rtpmap:107 vnd.onvif.metadata/90000
```

## Lenh van hanh nhanh

Start lai POC:

```bash
cd /tmp/gortsplib_metadata_poc
nohup ./mock-rtsp-metadata-poc > /tmp/mock-rtsp-metadata-poc.log 2>&1 &
echo $! > /tmp/mock-rtsp-metadata-poc.pid
```

Stop POC:

```bash
kill "$(cat /tmp/mock-rtsp-metadata-poc.pid)"
```

Check:

```bash
ss -lntup | grep -E ':8555|:8050|:8051'
tail -n 100 /tmp/mock-rtsp-metadata-poc.log
```

## Ket luan

POC da chung minh cac diem can nhat cho Profile M:

- Tao duoc RTSP server bang `gortsplib`.
- SDP co ONVIF metadata RTP track dung `vnd.onvif.metadata/90000`.
- Phat duoc RTP payload XML `tt:MetadataStream`.
- TCP interleaved PLAY OK.
- RTSP-over-HTTP tunnel DESCRIBE OK.

Buoc tiep theo de dua vao DTT:

1. Them metadata profile/path vao ONVIF `GetProfiles`/`GetStreamUri`, tra ve `rtsp://192.168.8.36:8555/metadata` hoac tam test bang port 8555.
2. Neu test Profile M yeu cau video + metadata trong cung session, mo rong POC path `/main` co ca H264 va metadata.
3. Sau khi pass POC voi DTT, moi tich hop vao repo `mock-camera-backend` va thay MediaMTX tren port 8554.
