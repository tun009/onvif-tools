# DTT m21 — Status Note

Ngay sau khi fix Metadata Configuration, test lai bang ONVIF Device Test Tool tao file:

- `C:\Program Files (x86)\ONVIF\ONVIF Device Test Tool\24.12\m21.xml`
- Thoi diem file: 2026-07-13 15:25:07 local
- Ket qua van con 6 failed, nhung nhom `MEDIA2-8-1-1 MODIFY ALL SUPPORTED METADATA CONFIGURATIONS` da pass.

## Ket luan nhanh

Dot do Profile M da giam dung nhu quan sat trong UI: phan `Metadata Configuration` da xanh. Cac dot do con lai tap trung o `Metadata Streaming`.

Trong `m21.xml`, 6 failed hien tai gom:

| Testcase | Nhom | Nguyen nhan chinh |
| --- | --- | --- |
| `RTSS-1-1-28` | Video streaming HTTP tunnel | `GetStreamUri` tra `http://192.168.8.36:8080/main`, DTT DESCRIBE qua RTSP-over-HTTP bi `Network error 10057` |
| `RTSS-1-1-30` | Mix transport types | `rtsp://.../main` pass, nhung `http://192.168.8.36:8080/jpeg` fail HTTP tunnel |
| `RTSS-1-1-35` | JPEG over RTSP/HTTP/TCP | `http://192.168.8.36:8080/jpeg` fail HTTP tunnel |
| `RTSS-1-1-42` | H.264 over RTSP/HTTP/TCP | `http://192.168.8.36:8080/main` fail HTTP tunnel |
| `RTSS-1-1-45` | RTSP/HTTP/TCP, base64 line breaks | `http://192.168.8.36:8080/jpeg` fail HTTP tunnel |
| `EVENT-4-1-9` | Event namespaces | Hai request namespace-equivalent khong cho cung phan ung; request bien the tra `ActionNotSupported` |

## Metadata Configuration da pass

So voi `m20.xml`, testcase sau khong con failed:

- `MEDIA2-8-1-1-v20.12 MODIFY ALL SUPPORTED METADATA CONFIGURATIONS`

Dieu nay xac nhan fix luu lai `SetMetadataConfiguration` va replay lai trong `GetMetadataConfigurations` da dung voi DTT.

## Metadata Streaming con do

Profile M spec yeu cau `Metadata Streaming` la mandatory:

- Co ready-to-use media profile cho metadata.
- `GetStreamUri` cho profile metadata.
- RTSP session that su co RTP metadata track.
- Ho tro transport `RTP/UDP` va `RTP/RTSP/HTTP/TCP`.
- `SetSynchronizationPoint` phai lam metadata stream mo mot XML document moi theo yeu cau.

Hien tai MediaMTX + ffmpeg chi fake video stream:

```bash
cd ~/onvif-tools/mock-camera-backend
./bin/mediamtx ./rtsp/mediamtx.yml
```

Cac path `main`, `jpeg`, `sub1`, `sub2` khong co ONVIF metadata RTP track (`vnd.onvif.metadata/90000`). Vi vay ve Profile M, huong chuan nhat van la them mot RTSP server/publisher co metadata track that su, khong chi sua SOAP.

## Huong xu ly kha thi

1. Xu ly nhanh nhom 5 RTSS fail:
   - Goc loi la ONVIF server tra HTTP tunnel URI ve cong `8080`, nhung endpoint nay khong thuc hien RTSP-over-HTTP tunnel nen DTT bi `Network error 10057`.
   - Can test phuong an tra HTTP tunnel URI ve dung cong RTSP server neu MediaMTX/gortsplib ho tro, hoac bo claimed support neu profile cho phep. Voi Profile T/M mandatory HTTP tunnel thi kha nang cao phai implement support.

2. Xu ly Profile M Metadata Streaming:
   - Phuong an uu tien: viet RTSP server bang `gortsplib`, thay MediaMTX+ffmpeg hoac lam sidecar rieng.
   - Server can serve video + metadata track trong cung RTSP session, SDP co `m=application` va `a=rtpmap:<pt> vnd.onvif.metadata/90000`.
   - Can ho tro UDP, RTSP interleaved TCP, va RTSP-over-HTTP tunnel.

3. Xu ly `EVENT-4-1-9`:
   - Can doc request chi tiet trong `m21.xml` va sua Event service router/parser de coi cac prefix khac nhau nhung cung namespace la tuong duong.
   - Loi hien tai: request bien the namespace tra `ActionNotSupported`, trong khi request goc subscribe pass.

## Luu y thao tac server

Khong upload de len repo that tren server. Neu can verify build sau khi sua code:

```bash
ssh tomotech@192.168.8.36
cp -a /home/tomotech/tungdt/onvif-tools/onvif-module/onvif-module /tmp/build_check
cd /tmp/build_check
make full
```

Chi upload file da sua vao `/tmp/build_check` de verify. Repo that tren server de user tu pull/commit/deploy.
