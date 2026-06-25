# Hướng dẫn Deploy Step-by-Step

> 📅 Cập nhật: 25/06/2026  
> 🎯 Mục tiêu: Build và chạy được ONVIF server, test với ONVIF Device Manager

---

## Yêu cầu hệ thống

| Thành phần | Yêu cầu |
|-----------|---------|
| Hardware | Jetson Orin NX |
| JetPack | 5.x (Ubuntu 22.04) |
| RAM | Tối thiểu 8GB |
| Storage | Tối thiểu 20GB |
| Network | Card mạng kết nối LAN |

---

## PHASE 1 — mock-camera-backend

### Bước 1: Copy project lên Jetson

**Từ Windows sang Jetson qua SCP:**
```powershell
# Chạy trên Windows (PowerShell):
scp -r "d:\Elcom\Ovif-mock\projects\mock-camera-backend\mock-camera-backend" `
    chudat@192.168.8.114:~/projects/mock-camera-backend

scp -r "d:\Elcom\Ovif-mock\projects\onvif-module\onvif-module" `
    chudat@192.168.8.114:~/projects/onvif-module
```

**Hoặc dùng Git (khuyến nghị):**
```bash
# Trên Jetson:
mkdir -p ~/projects
cd ~/projects
git clone https://github.com/your-org/mock-camera-backend
git clone https://github.com/your-org/onvif-module
```

### Bước 2: Install dependencies

```bash
cd ~/projects/mock-camera-backend
bash scripts/install_deps.sh
```

Script này sẽ tự động:
1. Cài GStreamer packages
2. Cài nlohmann-json3-dev
3. Copy `json.hpp` vào `include/third_party/nlohmann/`
4. Download MediaMTX binary vào `bin/mediamtx`

**Nếu script lỗi, làm thủ công:**
```bash
# Bước 2a: GStreamer
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    libgstreamer1.0-dev \
    libgstreamer-plugins-base1.0-dev \
    gstreamer1.0-tools \
    nlohmann-json3-dev

# Bước 2b: json.hpp
mkdir -p include/third_party/nlohmann
wget -O include/third_party/nlohmann/json.hpp \
    https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp

# Bước 2c: MediaMTX
mkdir -p bin
wget -O /tmp/mediamtx.tar.gz \
    https://github.com/bluenviron/mediamtx/releases/download/v1.9.1/mediamtx_v1.9.1_linux_arm64v8.tar.gz
tar -xzf /tmp/mediamtx.tar.gz -C bin/ mediamtx
chmod +x bin/mediamtx
```

### Bước 3: Verify GStreamer Jetson plugins

```bash
gst-inspect-1.0 nvv4l2h264enc
gst-inspect-1.0 nvv4l2h265enc
```

Nếu **không thấy** (máy x86 để dev):
```bash
# Dùng software encode thay thế:
# Sửa file scripts/start_main.sh:
# Thay: nvv4l2h264enc
# Bằng: x264enc speed-preset=ultrafast

# Cài x264:
sudo apt-get install -y gstreamer1.0-plugins-ugly
```

### Bước 4: Build

```bash
cd ~/projects/mock-camera-backend
make

# Expected:
# [CC] src/main.cpp
# [CC] src/MockCameraBackend.cpp
# [CC] src/MockPtzController.cpp
# [CC] src/MockEventGenerator.cpp
# [CC] src/ipc/IpcServer.cpp
# [LINK] mock-camera-server
# [DONE] mock-camera-server
```

**Nếu build lỗi:**
```bash
# Xem lỗi chi tiết:
make 2>&1 | head -50

# Lỗi thường gặp:
# "json.hpp: No such file" → Chạy lại bước 2b
# "undefined reference to pthread_*" → LIBS có -lpthread chưa?
```

### Bước 5: Chạy IPC server

```bash
# Terminal 1: Chạy mock IPC server
cd ~/projects/mock-camera-backend
./mock-camera-server config/mock.conf
```

Expected output:
```
==============================================
  Mock Camera Backend
  Config: config/mock.conf
==============================================
[MockCameraBackend] initialized, config=config/mock.conf
[IpcServer] listening ctrl=/tmp/mock-camera.sock evt=/tmp/mock-camera-evt.sock
[main] Mock server ready:
  Control socket : /tmp/mock-camera.sock
  Event socket   : /tmp/mock-camera-evt.sock
  RTSP main      : rtsp://127.0.0.1:8554/main
Press Ctrl+C to stop.
```

### Bước 6: Chạy RTSP streams

```bash
# Terminal 2: Start MediaMTX (RTSP server)
cd ~/projects/mock-camera-backend
./bin/mediamtx rtsp/mediamtx.yml &

# Đợi 2 giây rồi start GStreamer pipelines
sleep 2
bash scripts/start_streams.sh
```

### Bước 7: Verify streams

```bash
# Terminal 3: Check
cd ~/projects/mock-camera-backend
bash scripts/check_streams.sh

# Hoặc test với VLC:
# vlc rtsp://127.0.0.1:8554/main
```

---

## PHASE 2 — onvif-module (IPC Smoke Test)

### Bước 8: Install deps cho onvif-module

```bash
cd ~/projects/onvif-module
bash scripts/install_deps.sh
```

### Bước 9: Build IPC test

```bash
cd ~/projects/onvif-module
make

# Expected:
# [CC] src/main.cpp
# [CC] src/backend/BackendConnector.cpp
# [LINK] onvif-ipc-test
# [DONE] onvif-ipc-test
```

### Bước 10: Chạy smoke test

> ⚠️ **mock-camera-server phải đang chạy (Bước 5)**

```bash
cd ~/projects/onvif-module
./onvif-ipc-test config/onvif.conf
```

Expected:
```
==============================================
  ONVIF Module
  Device : 192.168.8.114:8080
  Backend: /tmp/mock-camera.sock
==============================================
[BackendConnector] connected ctrl=/tmp/mock-camera.sock

[TEST] GetDeviceInfo:
  Manufacturer : MockCam Inc.
  Model        : MockCam-4K
  Firmware     : 1.0.0
  Serial       : MOCK-001-2024

[TEST] GetSystemDateAndTime:
  2024-01-15 10:30:00 UTC

[TEST] GetProfiles: 3 profiles
  [profile_main] Main 4K
  [profile_sub1] Sub1 1080p
  [profile_sub2] Sub2 480p

[TEST] StreamUri [profile_main]: rtsp://127.0.0.1:8554/main
[TEST] StreamUri [profile_sub1]: rtsp://127.0.0.1:8554/sub1
[TEST] StreamUri [profile_sub2]: rtsp://127.0.0.1:8554/sub2

[TEST] PTZ Status: pan=0.00 tilt=0.00 zoom=0.00
[TEST] ImagingSettings: brightness=50.0 contrast=50.0

[TEST] All smoke tests PASSED
```

---

## PHASE 3 — gSOAP Setup & Full ONVIF Server

### Bước 11: Install gSOAP

```bash
cd ~/projects/onvif-module

# Option A: apt (JetPack 5 / Ubuntu 22.04)
sudo apt-get install -y \
    gsoap \
    libgsoap-dev \
    libssl-dev \
    zlib1g-dev

# Verify:
wsdl2h --version
soapcpp2 --version
```

Nếu version apt cũ (< 2.8.100), build từ source:
```bash
GSOAP_VER="2.8.127"
wget "https://sourceforge.net/projects/gsoap2/files/gsoap_${GSOAP_VER}.zip"
unzip gsoap_${GSOAP_VER}.zip
cd gsoap-${GSOAP_VER}
./configure --prefix=/usr/local --with-openssl
make -j$(nproc)
sudo make install
sudo ldconfig
```

Sau khi install, copy runtime files:
```bash
cd ~/projects/onvif-module

# Từ apt:
GSOAP_SRC="/usr/share/gsoap"
# Hoặc từ source build:
# GSOAP_SRC="/path/to/gsoap-2.8.127/gsoap"

cp $GSOAP_SRC/stdsoap2.h    external/gsoap/
cp $GSOAP_SRC/stdsoap2.cpp  external/gsoap/
cp $GSOAP_SRC/plugin/wsseapi.h   external/gsoap/plugin/
cp $GSOAP_SRC/plugin/wsseapi.cpp external/gsoap/plugin/
cp $GSOAP_SRC/plugin/wsddapi.h   external/gsoap/plugin/
cp $GSOAP_SRC/plugin/wsddapi.cpp external/gsoap/plugin/
cp $GSOAP_SRC/plugin/mecevp.h    external/gsoap/plugin/
cp $GSOAP_SRC/plugin/mecevp.cpp  external/gsoap/plugin/
cp $GSOAP_SRC/plugin/smdevp.h    external/gsoap/plugin/
cp $GSOAP_SRC/plugin/smdevp.cpp  external/gsoap/plugin/
cp -r $GSOAP_SRC/import          external/gsoap/
```

### Bước 12: Generate ONVIF code từ WSDL

```bash
cd ~/projects/onvif-module

# Download WSDLs + generate code
make gen
```

Script `gen` sẽ:
1. Download 7 file WSDL từ onvif.org vào `wsdl/`
2. Chạy `wsdl2h` → `generated/onvif.h` (~5000 dòng)
3. Chạy `soapcpp2` → `generated/soapC.cpp` (~100k dòng, mất vài phút)
4. Tạo namespace map `generated/onvif.nsmap`

Verify:
```bash
ls generated/
# Expected files:
# onvif.h         ← wsdl2h output
# soapH.h         ← all type declarations
# soapC.cpp       ← serializers
# soapServer.cpp  ← server dispatch
# soapClient.cpp  ← client stubs
# soapDeviceBindingService.h
# soapMedia2BindingService.h
# soapPTZBindingService.h
# ...

wc -l generated/soapC.cpp
# Expected: 50000-150000 lines
```

### Bước 13: Implement services (xem doc 04)

Sau khi generate, cần implement các service files.  
Xem tài liệu **`04-onvif-services.md`** để biết chi tiết.

### Bước 14: Build full ONVIF server

```bash
cd ~/projects/onvif-module
make full
```

### Bước 15: Chạy ONVIF server

```bash
# mock-camera-server phải đang chạy!
./onvif-server config/onvif.conf
```

---

## Cấu hình quan trọng

### mock-camera-backend/config/mock.conf
```ini
[device]
manufacturer    = MockCam Inc.
model           = MockCam-4K
firmware        = 1.0.0
serial          = MOCK-001-2024
hardware        = JetsonOrinNX
ip              = 192.168.8.114     # ← Đổi thành IP Jetson thực
http_port       = 8080
rtsp_port       = 8554

[streams]
main_uri        = rtsp://127.0.0.1:8554/main
sub1_uri        = rtsp://127.0.0.1:8554/sub1
sub2_uri        = rtsp://127.0.0.1:8554/sub2
```

### onvif-module/config/onvif.conf
```ini
device_ip   = 192.168.8.114     # ← Đổi thành IP Jetson thực
http_port   = 8080
rtsp_port   = 8554
username    = admin
password    = admin123
ctrl_socket = /tmp/mock-camera.sock
evt_socket  = /tmp/mock-camera-evt.sock
device_uuid = 12345678-1234-1234-1234-123456789abc
```

---

## Troubleshooting

### Lỗi: `nlohmann/json.hpp: No such file`
```bash
cd mock-camera-backend
wget -O include/third_party/nlohmann/json.hpp \
    https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
```

### Lỗi: `Cannot connect to /tmp/mock-camera.sock`
```bash
# Kiểm tra mock server đang chạy:
ls -la /tmp/mock-camera.sock
pgrep -a mock-camera-server

# Nếu socket file tồn tại nhưng server không chạy:
rm /tmp/mock-camera.sock
```

### Lỗi: `nvv4l2h264enc not found`
```bash
# Kiểm tra JetPack:
dpkg -l | grep jetpack

# Fallback cho x86:
# Sửa scripts/start_main.sh: nvv4l2h264enc → x264enc speed-preset=ultrafast
```

### Lỗi: `wsdl2h: command not found`
```bash
sudo apt-get install -y gsoap
# Hoặc build từ source (xem Bước 11)
```

### ONVIF Device Manager không tìm thấy camera
```bash
# 1. Kiểm tra port mở:
sudo netstat -tlnp | grep 8080

# 2. Kiểm tra firewall:
sudo ufw status
sudo ufw allow 8080/tcp
sudo ufw allow 8554/tcp
sudo ufw allow 3702/udp   # WS-Discovery

# 3. Test thủ công bằng curl:
curl -s -X POST http://192.168.8.114:8080/onvif/device \
  -H "Content-Type: application/soap+xml" \
  -d '<?xml version="1.0"?>
<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope">
  <s:Body><GetSystemDateAndTime xmlns="http://www.onvif.org/ver10/device/wsdl"/></s:Body>
</s:Envelope>'
```
