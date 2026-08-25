# Camera AI mới hoạt động như thế nào — giải thích dễ hiểu

> Bản tóm tắt dễ đọc của `docs/25`. Không dùng thuật ngữ kỹ thuật nếu tránh được. Coi cả hệ thống như **một cửa hàng**, mỗi service là một bộ phận trong cửa hàng đó.

## Hình dung tổng quát: camera AI giống một cửa hàng nhỏ

Tưởng tượng camera là **một cửa hàng có 7 bộ phận**, ai làm việc nấy, không giẫm chân nhau:

```text
1. HAL      = kho + thợ máy        (giữ và vận hành đồ vật, dụng cụ)
2. BUS&IPC  = hệ thống loa + bưu tá (truyền tin trong cửa hàng)
3. DVR      = quay phim             (quay, ghi băng, chiếu trực tiếp)
4. VPU      = đội chuyên gia nhìn   (nhận diện xe/người/biển số)
5. Core     = quản lý cửa hàng      (quyết định khi nào báo động)
6. Gateway  = bảo vệ cổng           (không cho gì lọt ra ngoài mà chưa kiểm tra)
7. MGMT     = quầy tiếp tân         (khách hàng/hệ thống ngoài giao tiếp qua đây)
```

Nguyên tắc sống còn: **bộ phận nào chỉ làm đúng việc của mình**, muốn nhờ bộ phận khác thì phải "gửi yêu cầu" theo đúng mẫu (interface), không được tự ý chạy sang phòng người khác nghịch đồ.

---

## 1. HAL — kho + thợ máy

**Làm gì:** giữ tất cả phần cứng thật (camera nhìn thấy gì, chip nén hình, chip AI, mô-tơ xoay camera, đèn, sưởi). Ai muốn dùng phần cứng đều phải hỏi HAL, không ai được đụng trực tiếp.

**Vì sao cần:** camera này có thể chạy trên 3 loại chip khác nhau (Jetson, Ambarella, Rockchip) — như ba hãng xe khác nhau. HAL là **người lái xe chung**: các bộ phận khác chỉ cần nói "chở tôi đi", không cần biết xe hãng nào, số sàn hay số tự động. Đổi hãng chip → chỉ đổi "người lái xe" (HAL), mọi bộ phận khác không cần sửa.

**5 việc HAL lo:**

- Lấy hình từ mắt camera (cảm biến)
- Nén/giải nén hình và tiếng
- Điều khiển đồ trong thân camera: xoay ống kính, sưởi kính, gạt nước
- Điều khiển đồ ngoài thân: mô-tơ xoay camera (PTZ), chuông báo, cảm biến cửa
- Chạy nhận diện AI trên chip AI (NPU)

**Điểm hay:** mọi lệnh điều khiển đồ vật (sưởi, xoay, zoom...) đều gói vào **một loại phong bì chung** gọi là `PeriphCmd` — kiểu như "phiếu yêu cầu": ghi "muốn làm gì" + "mức bao nhiêu", còn dây nối là I2C hay SPI thì kho tự lo, người gửi phiếu không cần biết.

---

## 2. BUS & IPC — hệ thống loa + bưu tá nội bộ

**Làm gì:** truyền tin giữa các bộ phận. Có 3 "kênh" khác nhau, giống 3 cách liên lạc khác nhau trong công ty:

```text
Kênh 1 — Băng chuyền hình ảnh  : hình mới chụp, ai cần thì lấy, không photocopy
Kênh 2 — Bảng tin dán thông báo : "phát hiện gì đó" để ai quan tâm tự đọc
Kênh 3 — Điện thoại nội bộ     : gọi hỏi/ra lệnh, chờ trả lời
```

**Kênh 1 (băng chuyền hình ảnh):** camera chụp 1 khung hình → đặt lên băng chuyền → cả đội quay phim (DVR) và đội AI (VPU) đều **lấy chung một tấm ảnh gốc**, không ai copy ra bản riêng. Copy tốn thời gian, camera cần nhanh nên tuyệt đối không copy. Nếu đội nào lấy không kịp, ảnh cũ bị bỏ đi (không thể chờ, camera không được khựng lại).

**Kênh 2 (bảng tin):** đội AI dán thông báo "thấy 1 xe" lên bảng tin, ai quan tâm (quản lý cửa hàng — Core) tự ra đọc. Người đọc chậm không cản người dán tin mới.

**Kênh 3 (điện thoại):** dùng cho lệnh — "đổi độ nét", "bật OTA" — phải xưng danh (xác thực) trước khi được nghe.

**So với hệ thống mock hiện tại:** mock mới chỉ có Kênh 3 (điện thoại nội bộ dạng gói tin 16 byte + JSON). Kênh 1 và Kênh 2 (băng chuyền hình ảnh thật + bảng tin AI thật) là phần **mock chưa có**.

---

## 3. DVR — đội quay phim

**Làm gì:** đúng nghĩa đen là "đầu ghi hình": quay, nén, lưu trữ, và phát trực tiếp cho ai muốn xem.

**4 người trong đội:**

- **Người cầm máy quay** — lấy hình từ HAL, dán nhãn thời gian, đặt lên băng chuyền
- **Phòng dựng phim** — nén hình cho nhẹ, làm 2 bản (bản đẹp để xem kỹ, bản nhẹ để xem nhanh), ghép luôn tiếng
- **Kho băng** — cất file vào ổ cứng, đánh số để tìm lại dễ, ổ đầy thì ghi đè băng cũ nhất, khóa két (mã hóa) để rút ổ cứng ra không ai xem trộm được
- **Phòng chiếu trực tiếp** — ai đang xem live thì phục vụ qua đây (RTSP)

**Phần quan trọng nhất — Kho băng:** đây là phần mock **hoàn toàn chưa có**. Mock hiện tại không thật sự ghi băng, chỉ giả vờ có vài đoạn có sẵn. Camera thật phải ghi liên tục 24 giờ không rớt cảnh nào, và phải nhớ chính xác "đoạn này bắt đầu lúc mấy giờ, kết thúc lúc mấy giờ" để sau này tìm lại xem đúng đoạn cần xem.

---

## 4. VPU — đội chuyên gia nhìn (AI)

**Làm gì:** nhìn từng khung hình, nhận ra: đây là xe, đây là người, biển số là gì, và theo dõi một vật đi xuyên nhiều khung hình.

**Ý hay nhất của cả hệ thống — mỗi kỹ năng AI là một "chuyên gia" rời:**

> Hãy tưởng tượng phòng chuyên gia: một người chỉ giỏi nhận xe, một người chỉ giỏi đọc biển số, một người chỉ giỏi nhận khuôn mặt. Muốn thêm khả năng mới (ví dụ nhận diện lửa cháy)? Chỉ cần **tuyển thêm một chuyên gia mới**, bỏ vào phòng — không ai trong phòng cũ phải học lại, cả cửa hàng không cần đóng cửa để "training". Đây gọi là **plugin**: thêm/bớt AI ngay khi máy đang chạy.

**Việc trong đội:**

- **Người quản lý chuyên gia** — kiểm tra hồ sơ, nhận chuyên gia mới vào làm, sa thải người làm sai
- **Người chia việc** — cắt hình cho từng chuyên gia đúng tốc độ họ làm được (chuyên gia nhanh nhận nhiều hình hơn)
- **Đội máy tính (chip AI)** — thật sự chạy phép tính nhận diện
- **Người theo dõi (Tracker)** — nhớ "chiếc xe này là chiếc lúc nãy", gắn một số ID xuyên suốt
- **Người báo cáo** — gói kết quả gửi ra ngoài, cả cho hệ thống nội bộ (Core) và cho khách ngoài (ONVIF)

**Điểm mở để không phải sửa code khi thêm khả năng mới:** kết quả AI trả về được thiết kế có "ô trống linh hoạt" — chuyên gia nào muốn trả thêm thông tin lạ (biển số, màu áo...) thì ghi vào ô trống đó, hệ thống truyền đi mà không cần hiểu bên trong là gì.

---

## 5. Core — người quản lý cửa hàng ra quyết định

**Làm gì:** biến "chuyên gia AI thấy gì" thành "cửa hàng phải làm gì". VPU chỉ nói "có 1 xe ở đây". Core mới là người quyết "xe đó có vượt vạch cấm không, có cần báo động không".

**4 việc:**

- **Người đọc nội quy** — so vật AI thấy với luật đã đặt (khu vực cấm, vạch ảo, hướng đi, tốc độ) → ra quyết định có sự kiện hay không. Đổi nội quy không cần tắt máy khởi động lại.
- **Người lọc báo động** — chống báo động dồn dập: một người đứng lì trong khu cấm 10 phút không được kêu còi 10.000 lần, chỉ kêu một lần rồi im một lúc (cooldown)
- **Người ghép sổ** — dán thông tin AI vào đúng đoạn băng đã quay, và tự xóa dữ liệu quá hạn lưu
- **Người thống kê** — đếm xe qua lại, tính lưu lượng, vẽ bản đồ nhiệt khu vực đông người

**Ví dụ dễ hình dung:** VPU là camera bảo vệ chỉ biết "có người ở khu A". Core là trưởng ca đọc nội quy: "khu A sau 10 giờ tối có người thì báo động, còn giờ hành chính thì kệ, không báo".

---

## 6. Gateway — bảo vệ cổng ra

**Làm gì:** là **cổng duy nhất** để bất cứ thứ gì rời khỏi camera đi ra ngoài (lên mây/cloud, gửi tin nhắn cảnh báo, gửi file qua mạng). Không ai được lén mở cổng sau.

**Ví dụ dễ hình dung:** giống hải quan sân bay. Không kiện hàng nào rời khỏi nước mà không qua kiểm tra giấy tờ (xác thực), niêm phong (mã hóa), ghi sổ ai gửi gì lúc nào (kiểm toán/audit).

**3 việc:**

- **Đội an ninh** — mã hóa, xác thực, kiểm xem đích đến có được phép gửi không, ghi lại mọi lần gửi
- **Đội phân loại + đăng ký** — biết đang có bao nhiêu "hãng chuyển phát" (giao thức: gửi lên mây, gửi tin nhắn, gửi file...) và chọn đúng hãng cho từng loại tin
- **Các hãng chuyển phát thật** — mỗi hãng lo một kiểu gửi (lên mây, MQTT, FTP...)

**Điểm hay — "kho tạm chờ gửi":** nếu mất mạng, tin cảnh báo không bị mất — nó được cất tạm (ghi xuống đĩa, không chỉ nhớ tạm trong RAM) và gửi lại khi có mạng, không gửi trùng, không mất kể cả khi mất điện đột ngột. Việc quan trọng như cảnh báo được ưu tiên giữ; việc ít quan trọng (số liệu định kỳ) bị bỏ trước nếu kho đầy.

**Lưu ý dễ nhầm:** đây chỉ là cổng cho luồng **chủ động gửi đi**. Còn khi khách (VMS) chủ động **vào xem** camera (xem trực tiếp qua ONVIF/RTSP) thì không đi qua cổng bảo vệ này — đó là cửa trước do quầy tiếp tân (MGMT) và đội quay phim (DVR) phục vụ trực tiếp.

---

## 7. MGMT — quầy tiếp tân

**Làm gì:** là nơi duy nhất người ngoài (khách quản trị, phần mềm VMS) giao tiếp với camera.

**4 việc:**

- **Quầy web/app** — admin vào chỉnh cấu hình qua trình duyệt hoặc API
- **Phiên dịch viên ONVIF** — nói chuẩn "ngôn ngữ ONVIF" để mọi phần mềm VMS (Milestone, Genetec...) đều hiểu được, bất kể hãng nào
- **Thợ nâng cấp firmware** — cập nhật phần mềm kiểu "có 2 bản dự phòng": cài bản mới, hỏng thì tự quay về bản cũ, camera không bao giờ "chết cứng"
- **Phòng giám sát sức khỏe** — theo dõi log, đo hiệu năng, báo bộ phận nào đang gặp trục trặc

**Điểm quan trọng nhất với dự án của bạn:** "Phiên dịch viên ONVIF" (MGMT-02) chính là vai trò mà `onvif-module` hiện tại đang đóng. Nó **tách riêng** khỏi quầy web/app — quầy web hỏng không ảnh hưởng phiên dịch viên ONVIF và ngược lại.

---

## Một ví dụ xuyên suốt: xe vượt đèn đỏ

Theo một khung hình đi qua cả cửa hàng, cho dễ nhớ toàn bộ luồng:

```text
1. Mắt camera (Sensor) nhìn thấy → HAL đưa hình lên
2. Hình đặt lên "băng chuyền" (Kênh 1) — HAI đội cùng lấy một tấm hình:
   a) Đội quay phim (DVR): nén lại → vừa ghi vào kho vừa phát trực tiếp
   b) Đội chuyên gia AI (VPU): nhận ra "có 1 xe", theo dõi xe này qua các khung
3. VPU dán thông báo lên "bảng tin" (Kênh 2): "xe X ở vị trí Y"
4. Quản lý cửa hàng (Core) đọc bảng tin, so với nội quy:
   "xe X vượt vạch khi đèn đỏ" → quyết định: có báo động!
5. Core kiểm tra không báo động trùng gần đây → cho báo động đi tiếp, rẽ 2 hướng:
   a) Khách đang "xem" qua ONVIF (VMS) → nhận được thông báo + xem được thông tin xe
   b) Bảo vệ cổng (Gateway) → mã hóa → gửi cảnh báo lên mây/tin nhắn
6. Muốn xem lại sau: hỏi quầy tiếp tân (ONVIF) → quầy tiếp tân hỏi kho băng (DVR)
   → kho tìm đúng đoạn theo thời gian → trả lại cho khách xem
```

Một tấm hình, bốn số phận cùng lúc: **được lưu lại, được phát trực tiếp, được phân tích, và (nếu có chuyện) được báo động ra ngoài** — tất cả từ đúng một tấm hình gốc, không copy thêm bản nào.

## Vì sao việc này liên quan tới dự án ONVIF mock hiện tại

Dự án mock hiện tại đã xây xong "Phiên dịch viên ONVIF" (MGMT-02) và đạt điểm tuyệt đối (313/313 case Profile G). Khi ráp vào camera thật, **phần phiên dịch này gần như giữ nguyên** — chỉ cần nối nó vào dữ liệu thật thay vì dữ liệu giả:

```text
Còn thiếu nhiều nhất : Kho băng thật (DVR) — mock chưa ghi hình thật, chỉ giả vờ có
Còn thiếu             : Băng chuyền hình ảnh thật + bảng tin AI thật (Kênh 1, 2)
Đã có sẵn, tái dùng   : Toàn bộ "tiếng ONVIF" mà quầy tiếp tân nói (đã pass 313/313)
```

Nói ngắn gọn: phần khó nhất về "nói đúng chuẩn ONVIF" đã xong. Phần còn lại là nối dây "nói cái gì" — tức là làm cho Kho băng, băng chuyền hình ảnh, và bảng tin AI trở thành thật thay vì giả.
