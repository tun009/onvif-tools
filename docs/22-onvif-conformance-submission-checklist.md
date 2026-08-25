# Checklist submit ONVIF Conformance cho Device Profile S, T, M, G

## 1. Kết luận nhanh

Để một camera được ONVIF liệt kê là conformant cho Profile S, T, M và G, bộ hồ sơ cốt lõi phải được tạo bằng **ONVIF Device Test Tool chính thức ở chế độ Conformance Test**, không phải chỉ export Debug Report XML.

Ba tài liệu cốt lõi mà chính test tool nêu rõ là:

```text
1. ONVIF Declaration of Conformance (DoC)
2. ONVIF Feature List
3. ONVIF Test Report
```

DoC phải đi kèm Feature List và Test Report do tool tạo. Nên cấu hình chữ ký điện tử của authorized representative ngay trong Conformance tab trước khi chạy. Không đổi tên file do tool sinh. Sau đó upload nguyên bộ trong ONVIF Member Tools.

Ngoài ba file cốt lõi, cần chuẩn bị thông tin sản phẩm, firmware release và ONVIF Interface Guide/public product documentation theo policy hiện hành của Member Portal. Portal có thể yêu cầu URL hoặc file tùy workflow đang áp dụng.

## 2. Điều kiện trước khi submit

Công ty phải là ONVIF member còn hiệu lực và tài khoản submit phải có quyền dùng Member Tools. Device Test Tool phải là bản official member release được phép dùng cho submission tại thời điểm nộp.

Sản phẩm được test phải là firmware/hardware thực tế sẽ bán hoặc phát hành. Model, hardware ID, firmware version và product name trong tool, DoC, report, Interface Guide và trang sản phẩm phải khớp nhau.

Một listing multi-profile phải được chứng minh trên cùng product identity. Không nên ghép report của hai model hoặc firmware khác nhau vào một submission.

## 3. Ba file bắt buộc do Device Test Tool tạo

### 3.1 Declaration of Conformance

DoC là tuyên bố pháp lý của ONVIF member rằng sản phẩm đáp ứng các mandatory feature và applicable conditional feature của profile được claim.

Trước khi chạy Conformance Test, vào Conformance tab và nhập:

```text
Member/company legal name
Product name
Product type
Model
Hardware notation / Hardware ID
Firmware version
Authorized representative name
Title/department
Signature image
```

FAQ ONVIF hướng dẫn bật tùy chọn apply electronic signature. Nếu DoC có chữ ký điện tử hợp lệ và các file còn lại đúng, quy trình approval có thể tự động và sản phẩm được liệt kê ngay. Tool/portal phải hiện trạng thái tương đương “DoC is signed”.

Không chỉnh PDF sau khi tool ký. Không rename file.

### 3.2 Feature List

Feature List ghi chính xác feature/capability mà DUT hỗ trợ và profile nào được claim. Đây là tài liệu đi cùng DoC.

Với project hiện tại, feature declaration phải phản ánh đúng giới hạn:

```text
Profile S: theo kết quả final conformance
Profile T: theo kết quả final conformance
Profile M: theo kết quả final conformance
Profile G: supported
Profile G Audio: unsupported
Profile G ReversePlayback: false
Profile G DynamicRecordings: false
Profile G DynamicTracks: false
```

Không chỉnh Feature List bằng tay để “thêm” feature. Nếu tool phát hiện feature chưa defined, profile chưa ở release status, hoặc claimed profile không supported, Conformance Test sẽ fail.

### 3.3 Test Report

Test Report là report chính thức của lần **Conformance Test**, chứa identity DUT, tool/version, test result, supported profile và chữ ký/validation của tool.

Các file `g1.xml`…`g12.xml`, `m1.xml`…`m15.xml` trong máy hiện tại là Debug Report phục vụ phát triển. Chúng không thay thế signed Conformance Test Report/DoC/Feature List.

Kết quả 244/244 và 313/313 là bằng chứng kỹ thuật tốt để sẵn sàng chạy official conformance, nhưng vẫn phải generate bộ submission documents từ Conformance tab.

## 4. Interface Guide và tài liệu công khai

Chuẩn bị ONVIF Interface Guide theo template chính thức. Mục tiêu là giúp integrator biết cách dùng phần ONVIF của sản phẩm mà không phải đoán.

Interface Guide nên có:

```text
Product family/model áp dụng
Firmware version tối thiểu
Network/discovery setup
ONVIF service endpoints hoặc cách discovery
Authentication/user setup
Profile S/T/M/G được hỗ trợ
Feature conditional nào có/không
Media profiles, codec và stream transport
Event topics/analytics metadata
Profile G recording/search/replay behavior
Giới hạn: audio, reverse playback, dynamic recording
Factory default / security onboarding
Link tới firmware và product support page
```

Nếu nhiều model dùng chung firmware và behavior, kiểm tra quy trình Family Submittal Under Single DoC. Không tự gom family chỉ vì tên gần giống; phải thỏa policy family/submittal hiện hành.

## 5. Product identity phải thống nhất

Các report đang có cho thấy identity không đồng nhất:

```text
Profile G reports:
Model ALG2-B803
Firmware 3.4.0

Một số Profile M/T reports:
Model ALG2-B808
Firmware 3.4.1
```

Nếu muốn một camera listing claim cả S/T/M/G, phải chọn identity release duy nhất, ví dụ:

```text
Product Name:    <tên thương mại>
Model:           ALG2-B803 hoặc model release thật
Hardware ID:     <revision thật>
Firmware:        <một version release duy nhất>
Profiles:        S, T, M, G
```

Sau đó chạy full Conformance Test trên chính identity đó. Không dùng report B808/3.4.1 để bổ sung profile cho listing B803/3.4.0, trừ khi ONVIF duyệt theo family process và hồ sơ family hợp lệ.

## 6. Cần chạy lại thế nào

### Bước 1: đóng băng release candidate

Tag code/firmware, build reproducible binary, ghi checksum, model và firmware version. Không sửa code giữa các profile run.

### Bước 2: reset DUT sạch

Khôi phục baseline trước full run, bảo đảm process/stream/state sạch. Dùng release configuration thực tế, không dùng temporary patch chỉ tồn tại trong RAM.

### Bước 3: cấu hình Conformance tab

Điền product identity và authorized representative. Upload signature image và bật apply signature trước khi test.

### Bước 4: chọn claim S/T/M/G

Chỉ claim profile hiện ở release status trong tool và được DUT feature discovery xác nhận supported. Kiểm tra tool không báo undefined feature hoặc unsupported claimed profile.

### Bước 5: chạy official Conformance Test

Chạy trọn suite trên cùng DUT/firmware. Không chỉ chạy từng category hoặc debug case. Chờ tool generate documents thành công và verify signature hợp lệ.

### Bước 6: giữ nguyên output

Copy nguyên result folder vào kho release. Không rename, không sửa PDF/XML, không “merge” report thủ công.

Nên lưu thêm nội bộ:

```text
Firmware binary + SHA-256
Git tag/commit
DTT installer/version
DTT settings export
DUT config/reset procedure
Raw Debug Report nếu cần điều tra
Packet captures của critical stream tests
Generated submission documents
```

### Bước 7: upload Member Tools

Tạo product submission, điền product identity, profile claims, product URL/support URL và upload nguyên bộ DoC + Feature List + Test Report. Cung cấp Interface Guide hoặc URL nếu portal yêu cầu. Xác nhận portal nhận chữ ký DoC.

### Bước 8: kiểm tra public listing

Sau approval, kiểm tra Conformant Products page: member name, product name, model, firmware, product type và S/T/M/G phải đúng. Lưu URL listing vào release records.

## 7. Audit nhanh hồ sơ hiện có

### Đã có

```text
Profile M/T/S technical result: 244/244, DTT 24.12
Profile G technical result:     313/313, DTT 25.12
Profile G final commit:         0bb932b
Tài liệu implementation:       docs/16, docs/20, docs/21
Member status:                  user xác nhận đã là ONVIF member
```

### Chưa thấy trong folder đã chọn

```text
Signed ONVIF DoC generated by tool
ONVIF Feature List generated by tool
Signed/validated ONVIF Test Report generated by Conformance Test
Unified final run trên cùng model + firmware cho S/T/M/G
ONVIF Interface Guide cho sản phẩm release
Product page/support URL
Firmware release binary/checksum/tag package
```

`g*.xml` và `m*.xml` đang thấy không phải ba submission documents kể trên.

## 8. Bộ hồ sơ release nên tổ chức

Không rename file do ONVIF tool sinh. Có thể đặt chúng trong folder nội bộ theo cấu trúc:

```text
onvif-submission/<product>/<firmware>/
├── tool-output-original/       # nguyên folder output, tên file không đổi
├── interface-guide/
├── firmware/
│   ├── firmware.bin
│   └── SHA256SUMS
├── release-evidence/
│   ├── git-commit.txt
│   ├── build-info.txt
│   └── reset-and-test-procedure.txt
└── portal-receipt/             # confirmation/screenshot/submission ID
```

## 9. Những thông tin còn cần chốt

Để tạo checklist điền sẵn và audit chính xác, cần các thông tin sau:

```text
1. Legal ONVIF member/company name
2. Product commercial name
3. Product type trong portal
4. Model muốn public listing
5. Hardware ID/revision
6. Firmware version final dùng submit
7. S/T/M/G có cùng một firmware binary không
8. ALG2-B803 và ALG2-B808 là hai model hay cùng product family
9. Tên/chức danh authorized representative ký DoC
10. Product page URL và support/firmware URL
11. Đã có Interface Guide chưa
12. Conformance tab hiện generate những filename nào
```

## 10. Rủi ro lớn nhất hiện tại

Rủi ro không nằm ở số test pass mà ở identity và loại report. Kỹ thuật đã xanh, nhưng report development đang tách giữa DTT 24.12/25.12 và B803/B808, trong khi submission cần bộ tài liệu chính thức, signed/validated, gắn với một product/firmware release nhất quán.

Trước khi upload, ưu tiên chạy một official Conformance Test cuối trên hardware/firmware sẽ bán, claim đủ S/T/M/G trong cùng workflow nếu tool và policy cho phép.