# CLAUDE.md — ONVIF Module

> Tài liệu này tự động nạp mỗi session. AI agent / developer PHẢI đọc trước khi code.

## Dự án là gì

ONVIF server (SOAP/gSOAP) giả lập camera IP, chạy trên Jetson Orin NX. Giao tiếp với camera backend (mock hoặc real DVR) qua IPC Unix socket. Mục tiêu: pass ONVIF Profile T conformance (hiện 204/210), sau mở rộng Profile M / G / A.

**Kiến trúc 2 repo**:
- `onvif-module` (repo này) — SOAP layer, không biết hardware
- `mock-camera-backend` — giả lập hardware, implement `ICameraBackend`

Giao tiếp qua `include/interface/` (shared contract giữa 2 repo).

## Tài liệu bắt buộc đọc

| Việc | Đọc file |
|------|----------|
| Kiến trúc tổng quan | `../../docs/02-kien-truc-du-an.md` |
| **Chuẩn code + cấu trúc file (BẮT BUỘC)** | `../../docs/10-refactor-plan-multi-profile.md` **PHẦN II** |
| Profile T status | `../../docs/07-profile-t-status-report.md` |
| Test guide | `../../docs/05-test-guide.md` |

## 10 quy ước bắt buộc (tóm tắt từ doc 10 section 13)

1. **Thêm operation vào service có sẵn** → sửa file service, thêm nhánh trong `handle()`. KHÔNG đụng `OnvifServer.cpp`.
2. **Thêm service mới** → tạo file kế thừa `IOnvifService` + đăng ký 1 dòng trong `main.cpp`. KHÔNG đụng core.
3. **Trả fault** → LUÔN dùng `FaultBuilder`. CẤM string-concat `<SOAP-ENV:Fault>` thủ công.
4. **Auth** → KHÔNG tự check trong service. Set `requiresAuth()`, để `AuthMiddleware` lo.
5. **Gọi backend** → CHỈ qua `backend_->method()` (ICameraBackend). CẤM tự mở socket/gọi hardware.
6. **File > 600 dòng** → tách handler theo nhóm operation.
7. **Naming** → `XxxService` (class), `handleXxx()` (method khớp tên ONVIF op).
8. **Interface types** → struct mới đặt trong `interface/types/`, mapping đúng ONVIF XSD.
9. **Vùng cấm sửa tự do**: `interface/` (sync 2 repo), `external/gsoap/` (generated), `OnvifServer.cpp` serve loop (core team).
10. **Test sau mỗi thay đổi** → chạy DTT tool, không regression.

## Checklist tự review trước khi coi là "xong"

- [ ] Không sửa `OnvifServer.cpp` (trừ core change)
- [ ] Fault dùng `FaultBuilder`
- [ ] Không tự check auth trong service
- [ ] Không tự gọi hardware/socket (qua `backend_`)
- [ ] File ≤ 600 dòng
- [ ] Không sửa `interface/` mà chưa sync mock backend
- [ ] Naming đúng convention
- [ ] Đã test DTT không regression

## Server & SSH

> ⚠️ **`192.168.8.36` KHÔNG dùng để build-check cho nhánh `onvif-v4.0.0` nữa**
> (xác nhận 2026-09-22, xem `docs/onvif-alvis/01-IMPLEMENTATION_PLAN.md` mục
> Phase 4): nhánh git trên `.36` cũ, thuộc giai đoạn Profile-G, không liên
> quan tới công việc tích hợp MGMT/DVR hiện tại. Server thật đang dùng để
> build-check + chạy DTT thật là **`192.168.8.125`** (dưới đây).

| Thông tin | Giá trị |
|-----------|---------|
| Host | `192.168.8.125` (port 22) |
| User / Pass | `alvis` / `8yV9jBv4` |
| Repo onvif-module | `/home/alvis/tungdt/onvif/onvif-tools/onvif-module/onvif-module` |
| Log onvif-server (runtime) | `/tmp/onvif-server-8001.log` (đặt tên theo `http_port` tĩnh trong `onvif.conf` lúc `nohup` khởi động — port thật lúc chạy có thể khác nếu MGMT override, xem log dòng `[main] Port from MGMT: ...`) |
| Log archive cũ | `/media/log_archive_20260921/onvif-server-8001.log` (và `mgmt_private.log` cùng thư mục) — log ngày cũ được xoay ra đây, không phải log runtime hiện tại |
| Build temp | Không có thư mục cố định — script `ssh_check.py` (mục 1 bên dưới) tự tạo `/tmp/build_check` (hoặc `/tmp/build_check_<tên task>` khi cần giữ song song nhiều lần build-check) bằng cách copy sạch repo vào đó rồi build; **tuyệt đối không đè lên** `/home/alvis/tungdt/onvif/onvif-tools/onvif-module/onvif-module` (repo thật, user tự `git pull`) |
| Khởi động hiện tại | Không phải systemd — chạy tay bằng `nohup ./onvif-server config/onvif.conf < /dev/null > /tmp/onvif-server-8001.log 2>&1 &` trong `setsid bash -c` từ repo thật |
| DVR service thật | `dvr_new.service` (systemd), binary `/opt/dvr_apps/dvr`, config `/opt/dvr_apps/dvr_new.env` |
| MediaMTX (web UI/WHEP, không phải RTSP ONVIF) | `/opt/dvr_apps/mediamtx` + `/opt/dvr_apps/mediamtx.yml` |

**192.168.8.36 (cũ, chỉ tham khảo lịch sử — không dùng cho nhánh hiện tại):**

| Thông tin | Giá trị |
|-----------|---------|
| Host | `192.168.8.36` (port 22) |
| User / Pass | `tomotech` / `Tomotech@123` |
| Repo onvif-module | `/home/tomotech/tungdt/onvif-tools/onvif-module/onvif-module` |
| Repo mock-backend | `/home/tomotech/tungdt/onvif-tools/mock-camera-backend/mock-camera-backend` |
| Log onvif-server | `/tmp/onvif-server.log` |
| Log mock-backend | `/tmp/mock-backend.log` |

> ⚠️ Windows → dùng **paramiko** (pexpect không chạy). Local path repo: `D:/Elcom/Ovif-mock/projects/onvif-module/onvif-module`.

### 1. Verify build sau mỗi lần sửa code

> ⚠️ **RULE VÀNG**: chỉ upload file vào `/tmp/build_check/`. TUYỆT ĐỐI KHÔNG `sftp.put` đè lên repo path trên server (user tự pull code). Đã vi phạm 1 lần, user rất khó chịu.

Cách: copy repo sang `/tmp/build_check` → upload **file vừa sửa** vào đó → `make full` → grep lỗi.

```python
# scripts/ssh_check.py — chạy: python scripts/ssh_check.py
import paramiko
HOST='192.168.8.125'; USER='alvis'; PW='8yV9jBv4'
BASE='D:/Elcom/Ovif-mock/projects/onvif-module/onvif-module'
REPO='/home/alvis/tungdt/onvif/onvif-tools/onvif-module/onvif-module'

# ⬇️ Chỉ liệt kê file BẠN VỪA SỬA (không cần toàn bộ)
FILES = [
    'src/OnvifServer.cpp',
    'src/services/Media2Service.cpp',
    # ... thêm file vừa sửa
]

c = paramiko.SSHClient(); c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
c.connect(HOST, username=USER, password=PW, timeout=15)
# Copy repo sạch vào /tmp (KHÔNG đụng repo gốc)
_,so,_ = c.exec_command(f'rm -rf /tmp/build_check && cp -r {REPO} /tmp/build_check && echo OK', timeout=60)
print(so.read().decode())
# Upload file đã sửa VÀO /tmp/build_check
sftp = c.open_sftp()
for f in FILES:
    sftp.put(f'{BASE}/{f}', f'/tmp/build_check/{f}'); print('UP', f)
sftp.close()
# Build + lọc lỗi
_,so,_ = c.exec_command('cd /tmp/build_check && make full 2>&1 | grep -E "error:|Error [0-9]+" | head -30; echo === END ===', timeout=600)
print(so.read().decode())
c.close()
```

Kết quả: nếu chỉ thấy `=== END ===` (không có dòng `error:`) → **build OK**.

### 2. Verify repo path trên server KHÔNG bị đụng

```python
# scripts/ssh_status.py
import paramiko
c = paramiko.SSHClient(); c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
c.connect('192.168.8.125','22','alvis','8yV9jBv4',timeout=15)
_,so,_ = c.exec_command(
    'cd ~/tungdt/onvif/onvif-tools/onvif-module/onvif-module && git status --porcelain src include && '
    'echo === && rm -rf /tmp/build_check && echo cleaned', timeout=30)
print(so.read().decode())   # rỗng trước === = repo sạch
c.close()
```

### 3. Đọc log khi debug conformance test

```python
# scripts/ssh_log.py — đọc log sau khi user chạy DTT test
import paramiko
c = paramiko.SSHClient(); c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
c.connect('192.168.8.125', username='alvis', password='8yV9jBv4', timeout=15)
# Lọc theo pattern cần debug (VD SOAP action, token, event topic)
_,so,_ = c.exec_command(
    'grep -E "GetProfiles|SetVideoEncoder|\\[Tunnel\\]|error" /tmp/onvif-server-8001.log | tail -60',
    timeout=15)
print(so.read().decode())
c.close()
```

Grep pattern hữu ích: SOAP action (`GetProfiles`), token (`profile_main`), tunnel (`\[Tunnel\]`), lỗi (`error|fault|500`).

### 4. Kiểm tra trạng thái process (khi test fail bất thường)

```python
# .125 — không có mock-camera-server (DVR/MGMT thật chạy sẵn qua systemd,
# chỉ mình onvif-server là process chạy tay/nohup)
import paramiko
c = paramiko.SSHClient(); c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
c.connect('192.168.8.125', username='alvis', password='8yV9jBv4', timeout=15)
_,so,_ = c.exec_command(
    'echo "=== onvif-server processes ==="; ps -o pid,lstart,cmd -C onvif-server 2>/dev/null; '
    'echo "=== port 8000/8001/554/8554 ==="; ss -ltnp 2>/dev/null | grep -E ":(8000|8001|554|8554)\\b"; '
    'echo "=== dvr_new / MGMT ==="; systemctl is-active dvr_new mgmt 2>/dev/null', timeout=15)
print(so.read().decode())
c.close()
```

Nếu thấy **>1 onvif-server** hoặc process khởi động từ ngày cũ → stale, cần restart sạch (mục 5).

Trên `.36` (cũ, mock-camera-server) có bài học stale process giữ port 8080 (09/07) — script gốc:
```python
import paramiko
c = paramiko.SSHClient(); c.set_missing_host_key_policy(paramiko.AutoAddPolicy())
c.connect('192.168.8.36', username='tomotech', password='Tomotech@123', timeout=15)
_,so,_ = c.exec_command(
    'echo "=== port 8080 ==="; ss -ltnp 2>/dev/null | grep :8080; '
    'echo "=== processes ==="; ps -o pid,lstart,cmd -C onvif-server,mock-camera-server 2>/dev/null; '
    'echo "=== sockets ==="; ls -la /tmp/*.sock 2>&1', timeout=15)
print(so.read().decode())
c.close()
```

## Build & deploy

```bash
make full                                    # build local / trên server
```

**Deploy trên `.125`** (repo thật, không phải `/tmp/build_check`):
```bash
cd /home/alvis/tungdt/onvif/onvif-tools/onvif-module/onvif-module
git pull
make clean && make full   # camera từng gặp link nhầm object cũ nếu chỉ `make full` — luôn clean trước
pkill -f onvif-server; sleep 1
ps aux | grep onvif-server | grep -v grep   # verify rỗng
setsid nohup ./onvif-server config/onvif.conf < /dev/null > /tmp/onvif-server-8001.log 2>&1 &
```

**Deploy trên `.36`** (cũ, có thêm mock-camera-server — tránh stale process giữ port 8080, bài học 09/07):
```bash
pkill -f onvif-server; pkill -f mock-camera-server
sleep 1
ps aux | grep -E "onvif-server|mock-camera" | grep -v grep   # verify rỗng
ss -ltnp | grep :8080                                         # verify port free
# Start ĐÚNG THỨ TỰ: backend TRƯỚC, onvif SAU
cd ~/tungdt/onvif-tools/mock-camera-backend/mock-camera-backend
nohup ./mock-camera-server config/mock.conf > /tmp/mock-backend.log 2>&1 &
sleep 2
cd ~/tungdt/onvif-tools/onvif-module/onvif-module
nohup ./onvif-server config/onvif.conf > /tmp/onvif-server.log 2>&1 &
```

## Cấu trúc thư mục (xem đầy đủ: doc 10 section 11)

```
include/
  core/       ← IOnvifService, ServiceRegistry, ServiceContext (hạ tầng)
  auth/       ← AuthMiddleware, DigestAuthHandler, WsSecurityHandler
  utils/      ← FaultBuilder, SoapResponse, SimpleJson
  backend/    ← BackendConnector (IPC client)
  interface/  ← ⚠️ SHARED CONTRACT (sync 2 repo, cấm sửa tự do)
  services/   ← mỗi ONVIF service 1 file (nơi thêm 90% code mới)
src/          ← gương của include/
```

## Luồng 1 request (sau refactor)

```
Client SOAP → OnvifServer → ServiceRegistry.route(path) → AuthMiddleware
           → svc->handle() → backend_->method() → FaultBuilder/SoapResponse → Client
```

## Quy tắc làm việc với user

- User tự commit code — KHÔNG chạy `git commit`.
- Sửa code ở local, user tự push. Không tự sửa file trên server.
- Trả lời tiếng Việt.
