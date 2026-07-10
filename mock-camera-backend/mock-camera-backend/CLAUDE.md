# CLAUDE.md — Mock Camera Backend

> Tài liệu tự động nạp mỗi session. AI agent / developer PHẢI đọc trước khi code.

## Dự án là gì

Giả lập **phần cứng camera** cho ONVIF module. Cung cấp data (profiles, stream URI, imaging, events...) qua IPC Unix socket. Chạy kèm mediamtx (RTSP server) + ffmpeg (fake video stream).

**Kiến trúc 2 repo**:
- `onvif-module` — SOAP layer (ONVIF protocol)
- `mock-camera-backend` (repo này) — implement `ICameraBackend`, trả fake data

Giao tiếp qua `include/interface/` (shared contract, đồng bộ với onvif-module).

## Vai trò trong hệ thống

```
DTT Tool → onvif-module (SOAP) → IPC Unix socket → mock-camera-backend → fake data
                                                          │
                                                    mediamtx + ffmpeg (RTSP streams)
```

Mock **KHÔNG biết SOAP/XML** — chỉ implement C++ interface trả struct. onvif-module lo phần SOAP.

## Cấu trúc thư mục

```
include/
  interface/          ← ⚠️ SHARED CONTRACT (đồng bộ với onvif-module, cấm sửa lệch)
    ICameraBackend.h  ←   30 virtual methods phải implement
    ipc/IpcProtocol.h ←   format message IPC
    types/*.h         ←   struct data (mapping ONVIF XSD)
  mock/               ← implementation headers
    MockCameraBackend.h    ← class chính implement ICameraBackend
    MockEventGenerator.h   ← sinh event fake định kỳ (motion/tamper)
    MockPtzController.h    ← state machine PTZ
    IpcServer.h            ← server nghe Unix socket từ onvif-module
src/
  main.cpp            ← entry: tạo backend + IpcServer, chạy loop
  MockCameraBackend.cpp   ← 30 method trả fake data
  MockEventGenerator.cpp
  MockPtzController.cpp
  ipc/IpcServer.cpp   ← serialize/deserialize IPC + dispatch tới backend
config/mock.conf      ← cấu hình
rtsp/mediamtx.yml     ← config mediamtx RTSP (4 stream: main/sub1/sub2/jpeg)
scripts/              ← start_streams.sh, stop_streams.sh, check_streams.sh
```

## Quy ước bắt buộc

1. **Implement đúng `ICameraBackend`** — MockCameraBackend override đủ 30 method. Thiếu → compile error abstract class.
2. **⚠️ KHÔNG sửa `interface/` lệch với onvif-module** — 2 repo phải giống HỆT file `interface/`. Sửa 1 bên → sync bên kia ngay.
3. **Getter** → trả struct fake data (hardcode hoặc từ config). **Setter** → lưu state map + return true (mock không có hardware thật).
4. **Event** → `MockEventGenerator` sinh fake định kỳ. Đây là mock-specific — real DVR thay bằng bridge AI box.
5. **Stream** → mediamtx + ffmpeg serve RTSP. `getStreamUri()` trả URL trỏ mediamtx (`rtsp://127.0.0.1:8554/main`).
6. **Naming** → `MockXxx` cho class mock.

## Build & test

```bash
make            # build → tạo binary mock-camera-server
make clean
```

### Chạy mock (thứ tự quan trọng)

```bash
# 1. Start RTSP streams (mediamtx + ffmpeg)
bash scripts/start_streams.sh
# hoặc: ./bin/mediamtx ./rtsp/mediamtx.yml

# 2. Start mock backend
./mock-camera-server config/mock.conf
```

### Kiểm tra streams

```bash
bash scripts/check_streams.sh
# hoặc: curl -s http://127.0.0.1:19997/v3/paths/list    # mediamtx API
```

## Server & SSH

| Thông tin | Giá trị |
|-----------|---------|
| Host / User / Pass | `192.168.8.36` / `tomotech` / `Tomotech@123` |
| Repo mock-backend | `/home/tomotech/tungdt/onvif-tools/mock-camera-backend/mock-camera-backend` |
| Repo onvif-module | `/home/tomotech/tungdt/onvif-tools/onvif-module/onvif-module` |
| Log mock-backend | `/tmp/mock-backend.log` |

> ⚠️ **RULE VÀNG**: verify build chỉ upload file vào `/tmp/build_check/`. KHÔNG `sftp.put` đè repo path (user tự pull). Windows dùng paramiko.

### Verify build sau khi sửa

```python
# python script (chạy từ Windows local)
import paramiko
HOST='192.168.8.36'; USER='tomotech'; PW='Tomotech@123'
BASE='D:/Elcom/Ovif-mock/projects/mock-camera-backend/mock-camera-backend'
REPO='/home/tomotech/tungdt/onvif-tools/mock-camera-backend/mock-camera-backend'
FILES = ['src/MockCameraBackend.cpp']   # ⬅️ file vừa sửa

c = paramiko.SSHClient(); c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
c.connect(HOST, username=USER, password=PW, timeout=15)
_,so,_ = c.exec_command(f'rm -rf /tmp/mock_build && cp -r {REPO} /tmp/mock_build && echo OK', timeout=60)
print(so.read().decode())
sftp = c.open_sftp()
for f in FILES:
    sftp.put(f'{BASE}/{f}', f'/tmp/mock_build/{f}'); print('UP', f)
sftp.close()
_,so,_ = c.exec_command('cd /tmp/mock_build && make 2>&1 | grep -E "error:|Error [0-9]+" | head -30; echo === END ===', timeout=300)
print(so.read().decode())
c.close()
```

Chỉ thấy `=== END ===` (không `error:`) → build OK.

## Migrate sang real DVR (tương lai)

Khi thay mock bằng camera thật:
- **Giữ nguyên** `include/interface/` (contract)
- Tạo `DvrBackend` implement `ICameraBackend` — body gọi hardware thật (GStreamer, EEPROM, ISP...)
- Bỏ `MockEventGenerator` → bridge AI box thật
- Đổi `main.cpp`: `make_shared<DvrBackend>` thay `MockCameraBackend`
- `IpcServer` giữ nguyên
- Chi tiết: xem `../../docs/10-refactor-plan-multi-profile.md`

## Quy tắc làm việc với user

- User tự commit code — KHÔNG chạy `git commit`.
- Sửa code ở local, user tự push. Không tự sửa file trên server.
- Trả lời tiếng Việt.
