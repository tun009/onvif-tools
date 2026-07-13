# Hướng dẫn Triển khai Hệ thống Mock Camera (Deploy Guide)

> 📅 Cập nhật: 29/06/2026  
> 🎯 Mục tiêu: Triển khai nhanh hệ thống ONVIF Mock Camera trên server Linux mới

---

## 1. Cài đặt Thư viện Hệ thống (Dependencies)

Chạy lệnh duy nhất sau để cài đặt toàn bộ công cụ biên dịch, thư viện GStreamer (để stream video), OpenSSL, gSOAP phát triển và ffmpeg:

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config wget \
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libgstreamer-plugins-bad1.0-dev \
    libgstrtspserver-1.0-dev gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-tools \
    nlohmann-json3-dev gsoap libgsoap-dev libssl-dev ffmpeg
```

---

## 2. Triển khai Mock Camera Backend

Di chuyển vào thư mục dự án backend (ví dụ: `~/onvif-tools/mock-camera-backend`):

1. **Cài đặt thư viện JSON cục bộ và MediaMTX RTSP Server:**
   ```bash
   chmod +x scripts/install_deps.sh
   ./scripts/install_deps.sh
   ```
2. **Biên dịch backend:**
   ```bash
   make clean && make
   ```
   *(Tạo ra file thực thi `mock-camera-server`)*

---

## 3. Triển khai ONVIF Module

Di chuyển vào thư mục ONVIF module (ví dụ: `~/onvif-tools/onvif-module`):

1. **Tải WSDLs, tự động cấu hình runtime gSOAP và sinh code:**
   ```bash
   # Lệnh này sẽ tự động tìm kiếm và copy stdsoap2, wsddapi, và struct_timeval từ hệ thống
   make gen
   ```
2. **Biên dịch server ONVIF:**
   ```bash
   make clean && make full
   ```
   *(Tạo ra file thực thi `onvif-server`)*

---

## 4. Chạy Hệ thống

Để chạy đầy đủ camera mock, bạn cần khởi chạy **3 tiến trình** sau (khuyến nghị chạy trên các cửa sổ `tmux`/`screen` hoặc chạy nền bằng `&`):

### Tiến trình 1: RTSP Server (MediaMTX)
```bash
cd ~/onvif-tools/mock-camera-backend
./bin/mediamtx ./rtsp/mediamtx.yml
```

### Tiến trình 2: Backend Mock Camera Server
```bash
cd ~/onvif-tools/mock-camera-backend
./mock-camera-server config/mock.conf
```

### Tiến trình 3: ONVIF Server
```bash
cd ~/onvif-tools/onvif-module
./onvif-server config/onvif.conf
```


---

## 5. Cấu hình Tường lửa (Firewall) & Khắc phục lỗi

### Tường lửa (UFW)
Nếu test trong mạng nội bộ, khuyên dùng tắt hẳn tường lửa:
```bash
sudo ufw disable
```
Nếu bắt buộc phải bật tường lửa, mở các cổng sau để Client ONVIF kết nối và nhận luồng video (UDP):
```bash
sudo ufw allow 8080/tcp  # ONVIF HTTP Service Port
sudo ufw allow 8554/tcp  # RTSP Streaming Port
sudo ufw allow 3702/udp  # WS-Discovery (Tìm kiếm thiết bị)
sudo ufw allow 8000/udp  # RTP Video data
sudo ufw allow 8001/udp  # RTCP Video control
sudo ufw reload
```

### Kiểm tra port trống trước khi chạy (khi triển khai lên server chia sẻ)

Trước khi chạy mediamtx, kiểm tra các port sẽ dùng có bị chiếm không:
```bash
sudo ss -tlnp | grep -E ':8554|:9997|:8080|:3702'
```
Các port mặc định mediamtx bind mà **rtsp/mediamtx.yml đã disable** để tránh xung đột: `1935` (RTMP), `8888` (HLS), `8889` (WebRTC), `8890` (SRT). Nếu 8554 hoặc 9997 vẫn bận, sửa `rtspAddress`/`apiAddress` trong `rtsp/mediamtx.yml` và cập nhật `rtsp_port` trong `mock-camera-backend/config/mock.conf` cho khớp.

### Khắc phục lỗi thường gặp

* **Lỗi: `chrono_duration.cpp: error: 'std::chrono' has not been declared`**
  * **Nguyên nhân:** gSOAP của hệ thống copy nhầm các file custom serializer không tương thích vào dự án.
  * **Khắc phục:** Đồng bộ file `scripts/gen_gsoap.sh` mới nhất (chỉ copy `struct_timeval.c`/`struct_timeval.cpp`), sau đó chạy lại:
    ```bash
    make gen && make clean && make full
    ```
* **Lỗi: `listen tcp :8889: bind: address already in use` (hoặc port 1935/8888/8890)**
  * **Nguyên nhân:** mediamtx mặc định bind cả RTMP/HLS/WebRTC/SRT dù không dùng.
  * **Khắc phục:** đảm bảo `rtsp/mediamtx.yml` có 4 dòng disable: `rtmp: no`, `hls: no`, `webrtc: no`, `srt: no`.

* **Lỗi: `Cannot connect to /tmp/mock-camera.sock`**
  * **Nguyên nhân:** File socket cũ chưa được dọn dẹp hoặc Mock Camera Backend chưa chạy.
  * **Khắc phục:** Xóa socket cũ và đảm bảo `mock-camera-server` đang chạy:
    ```bash
    rm -f /tmp/mock-camera.sock /tmp/mock-camera-evt.sock
    ```
