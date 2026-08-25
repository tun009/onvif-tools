# Handoff ONVIF - current context

File nay tom tat lai ngu canh hien tai de mo mot task/chat moi van co the tiep tuc lam duoc.

## Muc tieu hien tai

Team dang tich hop ONVIF vao backend camera that, thay cho mock backend truoc day.

ONVIF module nen duoc giu nhu mot service rieng/protocol adapter. Backend that gom cac service nhu MGMT, DVR/media, CORE/VPU/AI. ONVIF module se map SOAP/ONVIF sang API/noi bo cua cac service nay.

Gan day user dang debug mot van de rieng: cung mot `onvif_server`/cong 8000, camera `192.168.20.101` dung duoc voi ONVIF Device Manager, nhung camera `192.168.8.54` bi loi authentication/authorization.

## Thu muc va source lien quan

- Workspace chinh: `D:\Elcom\Ovif-mock\projects`
- Docs chinh: `D:\Elcom\Ovif-mock\projects\docs`
- Tai lieu kien truc backend moi: `D:\Elcom\Ovif-mock\projects\docs\Camera-alvis`
- Mock ONVIF/module tung pass DTT: `D:\Elcom\Ovif-mock\projects\onvif-module`
- Mock camera backend cu: `D:\Elcom\Ovif-mock\projects\mock-camera-backend`
- ONVIF server cu/shared cho camera that: `D:\Elcom\Ovif-mock\onvif_server`
- Backend DVR cu, camera `.101` dang ONVIF duoc: `D:\Elcom\DVR\dvr`
- Backend PTZ/CV25, camera `.54` dang loi ONVIF: `D:\Elcom\DVR\ptz-cv25`
- Backend moi AlvisOS: `D:\Elcom\Ovif-mock\AlvisOS`

## Thiet bi va endpoint dang biet

### Mock/DTT box

- Device service: `http://192.168.8.36:8080/onvif/device_service`
- RTSP video/metadata services da tung dung: `8554`, `8555`
- DTT da tung chay rat nhieu lan tren ONVIF Device Test Tool.
- User noi hien tai project mock da pass duoc Profile S, G, M, T nho mot agent khac.

### Camera that dang OK voi ONVIF Device Manager

- IP: `192.168.20.101`
- Port ONVIF: `8000`
- Device service: `http://192.168.20.101:8000/onvif/device_service`
- Source backend: `D:\Elcom\DVR\dvr`

### Camera that dang loi ONVIF

- IP: `192.168.8.54`
- Port ONVIF: `8000`
- Device service: `http://192.168.8.54:8000/onvif/device_service`
- Source backend: `D:\Elcom\DVR\ptz-cv25`
- ONVIF Device Manager bao loi:
  - `The security token could not be authenticated or authorized`
  - Co luc hien them `Authorization request in future`

### Backend MGMT moi

- Swagger MGMT: `http://192.168.8.125:8086/swagger.html`
- RTSP stream that:
  - `rtsp://192.168.8.125:1992/live/ch0`
  - `rtsp://192.168.8.125:1992/live/ch1`

Khong ghi password vao file handoff/source. Neu can credential, user da tung cung cap trong chat va nen hoi lai khi can.

## Ket qua kiem tra camera `.54`

User da SSH duoc vao camera `.54` bang command dang:

```powershell
ssh alvis@192.168.8.54
```

Lan dau SSH co prompt host key, can go `yes`.

Anh chup tu camera `.54` cho thay cac process dang chay:

- `/opt/dvr_apps/sub_process/onvif_server 8000`
- `/opt/dvr_apps/sub_process/onvif_discovery 8000`
- `/opt/dvr_apps/sub_process/mediamtx /opt/dvr_apps/sub_process/mediamtx.yml`
- cac process khac: `api_upload_service`, `ws_server`, `factory_reset`, `laser`, `gpio`, postgres...

Tren camera `.54`, lenh `ss` khong co san. Can dung lenh khac nhu `netstat` neu co, hoac chi can `ps`.

User da chay curl GET voi digest vao:

```powershell
curl.exe --digest -u "..." -i "http://192.168.8.54:8000/onvif/device_service"
```

Ket qua:

- HTTP `405 Method Not Allowed`
- SOAP fault: `HTTP GET method not implemented`

Dieu nay binh thuong voi ONVIF SOAP vi device service can POST, khong phai GET. No chi chung minh endpoint co tra loi, chua chung minh auth OK.

## Huong nghi ngo hien tai cho loi `.54`

Vi process ONVIF tren `.54` dang chay va endpoint co tra loi, kha nang cao loi khong phai do service down.

Can tap trung vao cac kha nang:

1. Sai/cau hinh khac user database, password, realm, auth mode giua `.54` va `.101`.
2. Clock/time skew lam WS-UsernameToken bi xem la request tu tuong lai/qua cu.
3. Khac config launch/env cua `onvif_server` giua source `ptz-cv25` va `dvr`.
4. Khac binary/version/branch cua `onvif_server` duoc package vao tung camera.
5. Device service XAddr/path/port dung nhung service policy/auth khong dong nhat.

Can doc source, so sanh hai backend folder truoc khi dung toi camera.

## Cach debug tiep theo cho camera `.54`

Uu tien read-only, khong sua/khoi dong lai camera neu user chua yeu cau.

1. So sanh `D:\Elcom\DVR\ptz-cv25` voi `D:\Elcom\DVR\dvr`:
   - Noi nao copy/package/chay `onvif_server`.
   - Config port `8000`, path `/onvif/device_service`.
   - User/auth config.
   - Time/NTP/date handling.
   - Cac script launch va env.

2. So sanh voi `D:\Elcom\Ovif-mock\onvif_server`:
   - Binary/config nao duoc lay vao tung backend.
   - Co branch/version khac nhau khong.

3. Neu can kiem tra tren camera, chi dung lenh doc:
   - `date`
   - `ps -ef | grep -E '[o]nvif|[d]vr|[s]oap'`
   - `find /opt/dvr_apps -maxdepth 3 -type f | grep -Ei 'onvif|user|auth|time|config'`
   - doc log/config lien quan neu co.

4. Khi test SOAP, dung POST chuan ONVIF, khong dung GET:
   - `GetSystemDateAndTime` de xem time.
   - `GetDeviceInformation` voi Digest/WSSE de lay fault auth chinh xac.

## ONVIF Device Manager vs DTT

- ONVIF Device Manager (ODM) la tool diagnostic/quan ly, kha de ket noi, co auto discovery va UI xem live/config.
- ONVIF Device Test Tool (DTT) la tool conformance, strict hon nhieu, dung de pass profile/chung chi.
- `NVT` trong ODM = Network Video Transmitter, tuc thiet bi camera/encoder ONVIF.

Loi `The security token could not be authenticated or authorized` nghia la request SOAP da toi server nhung bi tu choi auth. Nguyen nhan thuong gap: sai user/pass, digest mismatch, WS-Username timestamp/time skew, realm/nonce mismatch, endpoint/policy khac.

## Kinh nghiem tu qua trinh DTT truoc day

User da mat nhieu thoi gian vi cac thay doi quang ba feature gay regression.

Luu y quan trong:

- Khong quang ba feature/chuc nang chua implement that.
- Quang ba them feature trong `GetCapabilities`, `GetServices`, `GetEventProperties`, encoder options... co the lam DTT tang so testcase va fail them.
- Da tung co moc baseline tot hon: commit `86e1bc7 Fix Media2 encoder options consistency`, gan voi giai doan m66/m75, so case fail it hon.
- Da tung thay DTT tang tu `49 features / 240 tests` len `50 features / 248 tests` sau khi quang ba them feature, tao them failed cases.
- User da yeu cau neu sua code thi sua local -> commit -> server pull/build/restart. Khong sua truc tiep tren server production.

## Kien truc tich hop ONVIF voi backend moi

Khuyen nghi giu ONVIF module tach rieng, khong nhet het vao MGMT:

- ONVIF module la protocol adapter SOAP/WS-Discovery/RTSP metadata/Event.
- MGMT la control/configuration plane va co REST API.
- DVR/media service giu stream/recording/playback.
- CORE/VPU/AI sinh analytics/event.

ONVIF module can co config de tro toi backend:

- `mgmt_base_url`
- `public_xaddr_host` hoac public IP/port cua ONVIF service
- auth/API token neu MGMT can
- mapping stream/profile/token

Neu ONVIF va MGMT nam tren hai server/IP khac nhau, khong duoc hardcode localhost. ONVIF module phai goi MGMT bang URL cau hinh duoc.

## MGMT hien co va co the map vao ONVIF

Theo swagger MGMT moi user chup, hien thay mot so endpoint:

- `GET /mgmt/v1/Config/ZoomFocus`
- `PUT /mgmt/v1/Config/ZoomFocus`
- `GET /mgmt/v1/Config/LensInfo`
- `GET /mgmt/v1/Config/ImagingSettingsInit`
- `GET /mgmt/v1/Config/ImagingSettings`
- `PUT /mgmt/v1/Config/ImagingSettings`
- `GET /mgmt/v1/Ping`

Nhung endpoint nay co the map vao ONVIF Imaging/PTZ-ish control tuy theo chuan can expose. Can doc schema response/request trong Swagger/source truoc khi noi.

## Record/playback va Profile G

ONVIF Profile G can cac lop chinh:

- Recording Control Service: tao/xoa/cau hinh recording, recording job.
- Recording Search Service: tim recording/event/time range.
- Replay Service: cap RTSP replay URI va dieu khien playback bang RTSP.
- Export Service chi can neu advertise/export, khong nen advertise neu chua lam.

Backend DVR that phai giu file/index/metadata. ONVIF chi la SOAP facade chuyen request tu VMS/DTT sang DVR/backend.

Voi UI web:

- Continuous: ghi lien tuc trong khung thoi gian duoc bat.
- Scheduled: chi ghi theo lich.
- Event-triggered: chi ghi khi event/AI rule kich hoat, co pre-record/post-record neu backend ho tro.
- Grid 7x24 khong mau thuan voi cac che do tren neu coi moi block thoi gian la mot policy rieng.

## Analytics va custom rule

Trong ONVIF Analytics co cac rule/description chuan nhu:

- `LineDetector`
- `FieldDetector`
- `Loitering`
- Counting/Object Classification tuy phien ban va module.

`LineDetector` co:

- Parameters:
  - `Direction`
  - `Segments`
- Event output:
  - topic `tns1:RuleEngine/LineDetector/Crossed`
  - source items nhu `VideoSourceConfigurationToken`, `VideoAnalyticsConfigurationToken`, `Rule`
  - data item nhu `ObjectId`

Custom use case nhu "do rac sai quy dinh" khong co rule ONVIF standard rieng. Neu can expose thi dung vendor extension namespace rieng, vi du `erabyte:IllegalDumpingDetector`, nhung he thong ben thu ba chi hieu neu ho tich hop extension do.

## Luu y van hanh

- Khi lam voi mock DTT, moi lan run nen xoa/rotate log cu de tranh lan:
  - `/tmp/onvif-server.log`
- Sau DTT nen doc ca XML report user gui va log server.
- Khong dung `git reset --hard` hoac revert lung tung neu user chua yeu cau.
- Khong dung lenh kill/restart tren camera that neu user chua dong y.
- Neu phai SSH vao camera that, chi read-only truoc.

## Yeu cau gan nhat cua user

User muon doc source `D:\Elcom\DVR\ptz-cv25` de xem co implement ONVIF chua va vi sao camera `.54` bi auth fail, so sanh voi `D:\Elcom\DVR\dvr` la source camera `.101` dang ONVIF duoc.

Can tiep tuc bang cach doc source binh thuong, khong sua code:

1. Liet ke file root cua `D:\Elcom\DVR\ptz-cv25`.
2. Tim `onvif`, `device_service`, `8000`, `auth`, `digest`, `username`, `password`, `time`, `ntp`.
3. Lam tuong tu voi `D:\Elcom\DVR\dvr`.
4. So sanh launch/config/package.
5. Bao cao nguyen nhan kha nang cao va de xuat buoc debug tiep theo.
