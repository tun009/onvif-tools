# Profile M — Nghiên cứu, đánh giá feasibility & kế hoạch

> 📅 Tạo: 10/07/2026
> 📄 Nguồn: `docs/onvif-profile-m-specification-v1-0.pdf` (spec v1.0) + test tool feature tree
> 🎯 Mục tiêu: đánh giá mức độ khả thi fake Profile M trên mock camera + kế hoạch triển khai
> ⚠️ Tài liệu nghiên cứu — CHƯA code.

---

## 1. Profile M là gì (khác Profile T thế nào)

Profile M = **Metadata & Analytics**. Trọng tâm là **metadata streaming + AI analytics**, ngược với Profile T (trọng tâm video streaming/config).

| Khía cạnh | Profile T | Profile M |
|-----------|-----------|-----------|
| Metadata streaming | Conditional | **MANDATORY** |
| Metadata configuration | không có | **MANDATORY** |
| Analytics module | Optional | **MANDATORY** |
| Video streaming | Mandatory | Conditional (phụ) |
| Service mới cần | — | **Analytics service** (`/onvif/analytics`) |

→ Cái Profile T coi là phụ (metadata) thì Profile M coi là core.

---

## 2. Điểm mấu chốt: "cổng" Metadata Streaming (7.5)

**Toàn bộ độ khó Profile M nằm ở 1 chỗ**: feature 7.5 Metadata Streaming (MANDATORY) yêu cầu device **thực sự stream 1 RTP metadata track** chứa XML analytics, song song video, qua RTSP.

- Nếu **fake được** metadata RTP stream → 80% Profile M còn lại chỉ là SOAP config CRUD (fake dễ như Profile T) + thêm field XML.
- Nếu **KHÔNG fake được** metadata stream → toàn bộ mandatory sụp (không pass được conformance).

Vì các conditional metadata (8.7-8.12: object class, face, body, vehicle, license plate, geolocation) chỉ là **thêm element vào XML analytics frame** — một khi có stream, chúng gần như "free".

---

## 3. Feature MANDATORY (Section 7) — đánh giá feasibility fake

| # | Feature | Ops chính | Service | Fake được? | Độ khó |
|---|---------|-----------|---------|-----------|--------|
| 7.1 | User Authentication | HTTP Digest / WS-Security | Device | ✅ Đã có (Profile T) | — |
| 7.2 | Get Services | GetServices (thêm URL analytics) | Device | ✅ Sửa nhẹ | Rất dễ |
| 7.3 | Discovery | WS-Discovery + scope `Profile/M` | Discovery | ✅ Thêm scope | Rất dễ |
| 7.4 | System | datetime, reboot... | Device | ✅ Đã có | — |
| **7.5** | **Metadata Streaming** | GetStreamUri + **RTP metadata track** + SetSynchronizationPoint | Media2 + RTSP | 🔴 **KHÓ NHẤT** | **Cao** |
| 7.6 | Metadata Information | GetSupportedMetadata + capability | Analytics | ✅ Trả sample frame hardcode | Dễ |
| 7.7 | Config Metadata Profile | GetMetadataConfigurations, AddConfiguration, RemoveConfiguration | Media2 | ✅ CRUD như Profile T | Dễ |
| 7.8 | Metadata Configuration | GetMetadataConfigurations, GetMetadataConfigurationOptions, SetMetadataConfiguration | Media2 | ✅ State map + fake | Dễ-TB |
| 7.9 | Config Analytics Profile | GetAnalyticsConfigurations, AddConfiguration | Media2 | ✅ CRUD | Dễ |
| 7.10 | Analytics Module Config | GetSupportedAnalyticsModules, GetAnalyticsModules, Create/Delete/GetOptions/Modify | Analytics | ✅ Module description hardcode + state map | TB |

**Tổng kết mandatory**: 9/10 feature fake DỄ (SOAP config CRUD, giống hệt cách đã làm Profile T). **Chỉ 7.5 Metadata Streaming là rào cản thực sự.**

---

## 4. Feature CONDITIONAL (Section 8) — chọn để tăng điểm

> Conditional = chỉ cần nếu device claim support. Chọn cái dễ để tăng coverage, bỏ cái khó.

| # | Feature | Bản chất | Fake được? | Độ khó | Nên làm? |
|---|---------|----------|-----------|--------|----------|
| 8.1 | Media Profile Management | CreateProfile/DeleteProfile | ✅ Đã có (Profile T) | — | ✅ Free |
| 8.2 | Video Streaming | GetStreamUri video | ✅ Đã có | — | ✅ Free |
| 8.3 | Image Sending | base64 image HOẶC image URI trong metadata | 🟡 base64 1 ảnh JPEG stub | TB | 🟡 Optional |
| 8.4 | Event Pull Points | CreatePullPoint, PullMessages, Unsubscribe | ✅ Đã có (Profile T event) | — | ✅ Free |
| 8.5 | **Event via MQTT** | GetEventBrokers, AddEventBroker, DeleteEventBroker + JSON event qua MQTT broker | 🔴 Cần MQTT broker + JSON event | **Cao** | ❌ Bỏ |
| 8.6 | Rule Configuration | GetSupportedRules, GetRules, Create/Delete/GetOptions/Modify Rules | ✅ Giống Analytics Module (hardcode + state map) | TB | 🟡 Nên |
| 8.7 | Object Classification | thêm `<Class><Type>` vào metadata XML | ✅ Nếu có stream (7.5) → thêm field | Dễ* | ✅ Nếu có 7.5 |
| 8.8 | Human Face Metadata | thêm `<HumanFace>` vào XML | ✅ Thêm field | Dễ* | ✅ Nếu có 7.5 |
| 8.9 | Human Body Metadata | thêm `<HumanBody>` | ✅ Thêm field | Dễ* | ✅ Nếu có 7.5 |
| 8.10 | Vehicle Metadata | thêm `<VehicleInfo>` | ✅ Thêm field | Dễ* | ✅ Nếu có 7.5 |
| 8.11 | License Plate Metadata | thêm `<LicensePlateInfo>` | ✅ Thêm field | Dễ* | ✅ Nếu có 7.5 |
| 8.12 | GeoLocation Metadata | thêm `<GeoLocation>` | ✅ Thêm field | Dễ* | ✅ Nếu có 7.5 |
| 8.13 | Face Recognition Event | topic `tns1:RuleEngine/Recognition/Face` + GetSupportedRules | ✅ Event topic (như tampering Profile T) | TB | 🟡 Nên |
| 8.14 | License Plate Recognition Event | topic `tns1:RuleEngine/Recognition/LicensePlate` | ✅ Event topic | TB | 🟡 Nên |
| 8.15 | Line Crossing Counter | topic `tns1:RuleEngine/CountAggregation/Counter` | ✅ Event topic | TB | 🟡 Nên |

`*` = "Dễ" với điều kiện **đã fake được metadata stream (7.5)**. Không có 7.5 thì các cái này vô nghĩa.

---

## 5. Rào cản 7.5 — phân tích cách fake metadata RTP stream

Tool sẽ: GetStreamUri (metadata profile) → RTSP DESCRIBE → thấy track:
```
m=application 0 RTP/AVP 107
a=rtpmap:107 vnd.onvif.metadata/90000
```
→ PLAY → nhận RTP packet chứa XML `<tt:MetadataStream>...`→ parse verify.

### 3 phương án fake

| Option | Cách làm | Ưu | Nhược |
|--------|----------|-----|-------|
| **A. GStreamer appsrc** | `appsrc ! application/x-rtp,media=application,encoding-name=VND.ONVIF.METADATA ! rtspclientsink` + script bơm XML frame định kỳ | Flexible, chuẩn RTP | Fiddly, cần test kỹ payload type |
| **B. Custom mini RTSP metadata server** | Viết RTSP server nhỏ (C++/Python ~250 dòng) chỉ serve metadata track, generate XML frame | Kiểm soát hoàn toàn | Viết nhiều, maintain |
| **C. mediamtx + RTP feeder** | mediamtx relay metadata track từ ffmpeg/gstreamer | Tận dụng mediamtx | mediamtx focus video/audio, khả năng không support application media type — **rủi ro cao** |

**Khuyến nghị**: Option A (GStreamer appsrc) — khả thi nhất, đã có GStreamer trên Jetson.

### Nội dung XML metadata fake (đủ pass nhiều conditional cùng lúc)

```xml
<tt:MetadataStream xmlns:tt="http://www.onvif.org/ver10/schema">
  <tt:VideoAnalytics>
    <tt:Frame UtcTime="2026-07-10T...">
      <tt:Object ObjectId="1">
        <tt:Appearance>
          <tt:Shape><tt:BoundingBox left="0.1" top="0.2" right="0.5" bottom="0.6"/></tt:Shape>
          <tt:Class><tt:Type Likelihood="0.9">Human</tt:Type></tt:Class>       <!-- 8.7 -->
          <tt:HumanFace>...</tt:HumanFace>                                     <!-- 8.8 -->
          <tt:HumanBody>...</tt:HumanBody>                                     <!-- 8.9 -->
          <tt:VehicleInfo>...</tt:VehicleInfo>                                 <!-- 8.10 -->
          <tt:LicensePlateInfo>...</tt:LicensePlateInfo>                       <!-- 8.11 -->
          <tt:GeoLocation>...</tt:GeoLocation>                                 <!-- 8.12 -->
        </tt:Appearance>
      </tt:Object>
    </tt:Frame>
  </tt:VideoAnalytics>
</tt:MetadataStream>
```
→ 1 XML frame nhét đủ field → pass 8.7-8.12 cùng lúc.

---

## 6. Kiến trúc triển khai (trên nền refactor Registry đã có)

Nhờ Service Registry (Phase 1-4 refactor), thêm Profile M **KHÔNG đụng OnvifServer core**:

### Service mới
```
include/services/AnalyticsService.h + src/AnalyticsService.cpp   (string-based, IOnvifService)
  pathPrefix = "/onvif/analytics"
  - GetServiceCapabilities (SupportedMetadata=true)
  - GetSupportedMetadata          (7.6)
  - GetSupportedAnalyticsModules  (7.10)
  - GetAnalyticsModules
  - CreateAnalyticsModules / DeleteAnalyticsModules / GetAnalyticsModuleOptions / ModifyAnalyticsModules
  - GetSupportedRules / GetRules / CreateRules / DeleteRules  (8.6 nếu làm)
```
Register 1 dòng: `registry_.registerService(std::make_unique<AnalyticsService>(...))`

### Mở rộng Media2Service (đã có)
```
  + GetMetadataConfigurations / GetMetadataConfigurationOptions / SetMetadataConfiguration  (7.8)
  + GetAnalyticsConfigurations                                                             (7.9)
  + AddConfiguration/RemoveConfiguration hỗ trợ type Metadata + Analytics                  (7.7, 7.9)
```

### Backend interface (ICameraBackend) thêm
```
  + getMetadataConfigurations()
  + getSupportedAnalyticsModules() / getAnalyticsModules()
  + (metadata stream do dvr/GStreamer lo, không qua interface SOAP)
```

### mediamtx / GStreamer
```
  + metadata track (Option A) — GStreamer appsrc bơm XML frame
```

### Event topics thêm (MockSubscriptionManager)
```
  + tns1:RuleEngine/Recognition/Face             (8.13)
  + tns1:RuleEngine/Recognition/LicensePlate     (8.14)
  + tns1:RuleEngine/CountAggregation/Counter     (8.15)
```

---

## 7. Kế hoạch phân phase

| Phase | Nội dung | Effort | Rủi ro |
|-------|----------|--------|--------|
| **M0** | Chuẩn bị: thêm scope Profile/M, thêm analytics service URL vào GetServices | 0.5 ngày | Thấp |
| **M1** | `AnalyticsService` (SOAP config): GetSupportedMetadata, GetSupportedAnalyticsModules, GetAnalyticsModules, Create/Delete/Modify | 3-4 ngày | Thấp (fake như Profile T) |
| **M2** | Media2 metadata config: GetMetadataConfigurations/Options/Set, GetAnalyticsConfigurations, AddConfiguration metadata | 3-4 ngày | TB (đụng Media2Service) |
| **M3** | **Metadata RTP streaming** (GStreamer appsrc + XML feeder) + SetSynchronizationPoint | **5-7 ngày** | **Cao** |
| **M4** | Metadata content fields (object class, face, body, vehicle, LPR, geoloc) trong XML | 2-3 ngày | Thấp (thêm field) |
| **M5** | Recognition events (Face/LPR/LineCrossing) + GetSupportedRules | 3-4 ngày | TB |
| **M6** | Test conformance + fix | 4-5 ngày | — |

**Tổng: ~3-4 tuần** cho mock Profile M. Phần lớn effort ở M3 (metadata streaming).

**Bỏ qua** (không đáng effort): 8.5 MQTT (cần broker + JSON), 8.3 Image Sending (optional).

---

## 8. Đánh giá tổng — mock có fake được Profile M không?

| Nhóm | Feasibility |
|------|-------------|
| SOAP config CRUD (7.6-7.10, 8.6) | ✅ **Chắc chắn** — pattern y hệt Profile T |
| Metadata content XML (8.7-8.12) | ✅ **Dễ** — nếu có stream |
| Recognition events (8.13-8.15) | ✅ **Được** — event topic như tampering |
| **Metadata RTP streaming (7.5)** | 🟡 **Khả thi nhưng khó** — GStreamer appsrc, cần test kỹ |
| MQTT (8.5) | 🔴 **Bỏ** — conditional, không đáng |

**Kết luận**:
- Profile M **fake được ~85-90%** với effort ~3-4 tuần.
- Rào cản duy nhất đáng lo: **metadata RTP streaming (7.5)** — nếu GStreamer appsrc chạy được thì phần còn lại thuận lợi.
- **Khác biệt lớn với thực tế**: mock fake metadata (bounding box giả) — real DVR cần AI thật (vpu service). Nhưng để **pass conformance test**, fake là đủ (tool chỉ check format XML, không verify data đúng).

---

## 9. Khuyến nghị thứ tự làm

1. **Làm M0 + M1 + M2 trước** (SOAP config, ~1.5 tuần) → pass phần lớn mandatory không-streaming. Validate `AnalyticsService` trên nền Registry.
2. **POC M3 riêng** (thử GStreamer appsrc metadata track 1-2 ngày) → xác nhận khả thi TRƯỚC khi cam kết. Nếu POC fail → cân nhắc Option B (custom RTSP).
3. **M4-M5** sau khi M3 chạy.
4. **M6** test cuối.

> ⚠️ Rủi ro chính: M3 metadata streaming. Nên POC sớm để biết có làm được không, tránh cam kết cả kế hoạch rồi kẹt.

---

## 10. So sánh nhanh Profile T (đã xong) vs Profile M (dự kiến)

| | Profile T | Profile M |
|---|-----------|-----------|
| Mock hiện tại | 204/210 (97%) | ~0% (chưa có analytics/metadata) |
| Rào cản chính | HTTP tunnel (5 test bỏ) | Metadata RTP streaming (7.5) |
| Effort đạt | Đã xong | ~3-4 tuần |
| Service mới | 0 | AnalyticsService + Media2 mở rộng |
| Nền tảng sẵn sàng | — | ✅ Service Registry (refactor Phase 1-4) |

Kiến trúc Registry vừa refactor giúp thêm Profile M **sạch** — chỉ thêm file service + register, không đụng core.
