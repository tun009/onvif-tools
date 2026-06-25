# Tài liệu dự án ONVIF Mock Server

> 📅 Cập nhật: 25/06/2026

Thư mục này chứa toàn bộ tài liệu kỹ thuật của dự án **ONVIF Profile T Mock Server** cho Jetson Orin NX.

---

## Danh sách tài liệu (đọc theo thứ tự)

| # | File | Mô tả | Đối tượng |
|---|------|-------|-----------|
| 1 | [01-ONVIF-tong-quan.md](./01-ONVIF-tong-quan.md) | ONVIF là gì? Khái niệm cơ bản, kiến trúc hệ thống | **Bắt đầu từ đây** |
| 2 | [02-kien-truc-du-an.md](./02-kien-truc-du-an.md) | Sơ đồ chi tiết 2 repos, IPC protocol, GStreamer | Sau khi hiểu doc 1 |
| 3 | [03-huong-dan-deploy.md](./03-huong-dan-deploy.md) | Build & deploy từng bước lên Jetson | Khi bắt đầu làm |
| 4 | [04-onvif-services.md](./04-onvif-services.md) | Chi tiết DeviceService (Các service khác tạm hoãn) | Khi implement services |
| 5 | [05-test-guide.md](./05-test-guide.md) | Hướng dẫn test với ONVIF Device Test Tool hãng | Khi test |

---

## Tóm tắt cực nhanh

```
GÌ: ONVIF server giả lập camera IP trên Jetson Orin NX
    → Kết nối được với bất kỳ VMS nào hỗ trợ ONVIF
    → Không cần hardware camera thật trong quá trình dev

TẠI SAO CÓ 2 REPO:
  mock-camera-backend  = Giả lập phần cứng camera
                         (IPC server, PTZ state, fake events, RTSP streams)
  
  onvif-module         = ONVIF server thật
                         (SOAP, WS-Security, WS-Discovery)

GIAO TIẾP:
  SOAP (HTTP:8080)  ← ONVIF Device Test Tool kết nối vào đây
  RTSP (8554)       ← Xem video qua đây
  IPC (Unix Socket) ← onvif-module ↔ mock-camera-backend nội bộ

PHẠM VI HIỆN TẠI:
  - Chỉ tập trung triển khai duy nhất DeviceService để test kết nối, lấy thông tin
    thiết bị, cấu hình mạng và đồng bộ ngày giờ.
  - Sử dụng ONVIF Device Test Tool chính thức của hãng để kiểm thử.
```

---

## Bắt đầu nhanh

```bash
# Trên Jetson Orin NX:

# 1. Build mock server
cd mock-camera-backend
bash scripts/install_deps.sh && make

# 2. Chạy mock server + RTSP
./mock-camera-server config/mock.conf &
bash scripts/start_streams.sh

# 3. Test IPC
cd ../onvif-module
make && ./onvif-ipc-test config/onvif.conf

# 4. Xem video test bằng VLC
vlc rtsp://192.168.1.100:8554/main
```
