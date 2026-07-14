# Profile M/T va gortsplib - tai lieu ban giao

Cap nhat: 2026-07-14

## 1. Muc tieu

Du an gom `onvif-module` (SOAP/ONVIF server) va `mock-camera-backend` (IPC, fake data, stream). Muc tieu la pass conformance va dang ky Profile M.

## 2. Vi sao dung gortsplib

Runtime ban dau dung MediaMTX + ffmpeg cho video. Cach nay tao duoc RTSP video nhung khong tao duoc track metadata ONVIF `application` voi payload XML, va khong chu dong kiem soat RTSP-over-HTTP/metadata synchronization.

`gortsplib` phu hop vi co API cho RTSP server, SDP tuy chinh, RTP packet, TCP interleaved, UDP va RTSP-over-HTTP. Tuy nhien kha nang cua thu vien khong dong nghia runtime hien tai da bat va kiem chung tat ca transport.

## 3. Ket qua metadata POC

POC tam tren Ubuntu server:

```text
/tmp/gortsplib_metadata_poc/mock-rtsp-metadata-poc
log: /tmp/mock-rtsp-metadata-poc.log
port: 8555
```

URI:

```text
rtsp://192.168.8.36:8555/metadata
http://192.168.8.36:8555/metadata
```

POC da xac minh:

- RTSP OPTIONS, DESCRIBE, SETUP, PLAY.
- SDP co metadata track:

```text
m=application 0 RTP/AVP 107
a=control:trackID=0
a=rtpmap:107 vnd.onvif.metadata/90000
```

- TCP interleaved nhan duoc RTP payload XML `tt:MetadataStream`.
- XML co `VideoAnalytics`, `Frame`, `Object`, bounding box va class.
- RTSP-over-HTTP DESCRIBE tra ve SDP thanh cong.

POC chua duoc tich hop hoan toan vao source chinh, chua thay MediaMTX tren port 8554, va chua co packet-level verification day du cho moi transport trong flow DTT.

## 4. Cac thay doi ONVIF da lam

- Media2 metadata profile/configuration.
- `GetMetadataConfigurations`, `GetMetadataConfigurationOptions`, `SetMetadataConfiguration`.
- Negative `SessionTimeout=invalid` tra SOAP fault dung va khong luu state sai.
- Media1 `GetMetadataConfiguration` singular.
- Media1 Add/Remove metadata configuration va GetCompatibleMetadataConfigurations.
- Theo doi dynamic profile sau Add/Remove.
- Metadata `GetStreamUri` tam tro ve `rtsp://192.168.8.36:8555/metadata`.
- PullPoint subscription lifetime toi thieu 600 giay de phu hop testcase DTT cho lau.

## 5. Ket qua conformance

Lan chay `m35-pass-full.xml`:

```text
229 / 229 testcase pass
0 failed
```

Tuy nhien DTT van hien:

```text
Profile M FAILED
Profile T FAILED
Add-on TLS Configuration 1.0 NOT SUPPORTED
Conformance Testing FAILED
```

Day la hai lop khac nhau:

1. Test execution: cac testcase duoc chay va pass.
2. Profile Support Preliminary Check: DTT doi chieu scope, service capabilities va feature definition voi profile dang claim.

Vi vay `229/229 pass` khong tu dong lam Profile Support Preliminary Check pass.

## 6. Van de con ton dong

### 6.1 Capability streaming chua khop Profile M/T

Ket qua DTT cho thay Media2 capabilities dang tuong tu:

```xml
<tr2:StreamingCapabilities RTPMulticast="false"
    RTP_TCP="true" RTP_RTSP_TCP="true"/>
```

Profile M specification v1.0, muc 7.5, yeu cau mandatory:

- Metadata streaming using RTSP.
- RTP/UDP.
- RTP/RTSP/HTTP/TCP.
- `GetProfiles`, `GetStreamUri`, `SetSynchronizationPoint`.

Profile T co yeu cau video streaming tuong tu. DTT dang danh dau do cac feature metadata streaming trong Preliminary Check. Can kiem chung:

- RTP/UDP metadata unicast co packet that su hay chua.
- RTSP-over-HTTP co ca SETUP/PLAY/RTP data, khong chi DESCRIBE.
- Metadata track co nam trong selected Media Profile DTT dung hay chi ton tai o path `/metadata` rieng.
- `SetSynchronizationPoint` co tao XML document moi trong RTP stream hay moi chi pass SOAP response.

`RTPMulticast=false` chi noi multicast khong ho tro, khong tu dong ket luan RTP/UDP unicast khong ho tro. Can feature detail va packet capture de ket luan cuoi cung.

### 6.2 TLS add-on

Capability dang tra:

```xml
<tt:TLS1.1>false</tt:TLS1.1>
<tt:TLS1.2>false</tt:TLS1.2>
```

Neu DTT dang claim TLS add-on, overall se fail. Neu khong dang ky TLS thi khong claim add-on. Neu muon claim thi phai trien khai HTTPS/TLS that va cap nhat capability dung runtime.

### 6.3 Profile T dang duoc claim

Source dang co scope:

```text
onvif://www.onvif.org/Profile/T
onvif://www.onvif.org/Profile/M
```

Neu chi muc tieu la Profile M, can can nhac bo scope T cho den khi video streaming T duoc kiem chung day du.

## 7. Cach debug tiep theo

Vi DTT khong cho chay rieng feature trong flow conformance, can ket hop Diagnostics, SOAP log va packet capture.

Trong Diagnostics, tim nhom Profile M:

```text
Metadata Streaming
  Ready-to-use Media Profile
  GetProfiles
  GetStreamUri
  Metadata Streaming using RTSP
  Metadata Streaming over RTP/UDP-Unicast
  Metadata Streaming over RTP/RTSP/HTTP/TCP
  SetSynchronizationPoint
```

Tren Ubuntu:

```bash
ss -lntup | grep -E ':8554|:8555|:8050|:8051'
tail -n 100 /tmp/onvif-server.log
tail -n 100 /tmp/mock-rtsp-metadata-poc.log
sudo tcpdump -ni any 'port 8555 or udp'
ffprobe -v debug rtsp://192.168.8.36:8555/metadata
```

Can ghi lai response cua `GetServices`, `GetServiceCapabilities(Media2)`, `GetProfiles` va `GetStreamUri`, roi doi chieu voi transport va packet thuc te.

## 8. File lien quan

- Nghien cuu: `docs/13-gortsplib-streaming-research.md`.
- POC: `docs/14-gortsplib-metadata-poc-result.md`.
- Specification: `docs/onvif-profile-m-specification-v1-0.pdf`.
- Media2 metadata: `onvif-module/onvif-module/src/services/Media2MetadataService.cpp`.
- Media2 URI/capability: `onvif-module/onvif-module/src/services/Media2Service.cpp`.
- Media1 URI/capability: `onvif-module/onvif-module/src/services/MediaLegacyHandler.cpp`.
- PullPoint: `onvif-module/onvif-module/src/services/MockSubscriptionManager.cpp`.
- Scope: `onvif-module/onvif-module/src/services/DiscoveryService.cpp`.

## 9. Trang thai ban giao

Da dat:

- 229/229 testcase gan nhat pass.
- gortsplib POC tao duoc SDP metadata va XML RTP payload.
- Metadata configuration Media1/Media2 pass.
- PullPoint testcase timeout dai pass.

Chua ket luan dat certification Profile M/T:

- Chua xac minh day du RTP/UDP metadata packet-level.
- Chua xac minh RTSP-over-HTTP ca session va RTP data trong flow DTT.
- Chua xac minh metadata track dung selected Media2 profile.
- TLS add-on chua ho tro neu van claim.
- Preliminary Check van bao Profile M/T FAILED.

Agent tiep theo nen bat dau bang file nay, docs 13/14, sau do lay feature specification detail cua DTT cho cac dot do de truy ra capability/transport thieu truoc khi sua code.
