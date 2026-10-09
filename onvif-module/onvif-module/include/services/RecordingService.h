#pragma once
// RecordingService — ONVIF Recording Control (trc) Profile G chạy trên DVR thật.
//
// Chỉ đăng ký khi capability `recording = real`. Mô hình (D1–D3, xem
// 01-IMPLEMENTATION_PLAN.md Phase 7):
//   - 1 Recording / sensor: "rec_<VideoSourceId>"; mỗi luồng ghi được là 1 track video
//     "VIDEO_main" / "VIDEO_sub". Chỉ lấy sensor có cả recorder DVR lẫn Media profile ONVIF
//     (nguồn ảo "Overlay" của DVR vì thế tự bị loại).
//   - 1 Recording Job / Recording. Nguồn job = token Media profile của luồng.
//   - Luồng đang ghi mà recording đó chưa có job nào (bật từ web hoặc lịch) vẫn hiện trong danh
//     sách job dưới dạng job "quan sát được" (token "auto_<recording>_<luồng>", Mode/State Active),
//     để VMS thấy đúng thực tế. Sửa hoặc đặt Mode cho job đó sẽ "nhận" nó thành job thật (giữ token).
//   - Job (token, priority, nguồn, mode mong muốn) và cấu hình Recording/Track lưu bền ở
//     RecordingJobStore. Mode Active/Idle điều khiển ghi TAY của DVR (dùng chung với nút
//     "Record" trên web; ghi theo lịch không bị ảnh hưởng). JobState luôn đọc từ DVR.
//   - Không dynamic recording/track, không audio, không metadata.

#include "backend/IDvrClient.h"
#include "core/IOnvifService.h"
#include "services/MockSubscriptionManager.h"
#include "services/RecordingJobStore.h"

#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

class RecordingService : public IOnvifService {
public:
    // `serviceAddress` công bố ở Recording.Source.Address, ví dụ "http://ip:8001/onvif/recording".
    RecordingService(std::shared_ptr<IDvrClient> dvr, std::shared_ptr<RecordingJobStore> store,
                        std::string serviceAddress);
    ~RecordingService() override;

    std::string pathPrefix() const override { return "/onvif/recording"; }
    std::string name() const override { return "RecordingService"; }
    std::string handle(const std::string& rawRequest) override;

    // <trc:Capabilities/> dùng chung cho GetServices (DeviceService) và GetServiceCapabilities
    // (RECORDING-1-1-3 so hai bên). Chưa có service thật đang chạy → giá trị mock cũ. Không
    // gọi DVR, chỉ dùng số sensor lần đọc gần nhất.
    static std::string capabilitiesXml();

    // Dành cho Search: các Recording hiện có, mô tả đúng bằng thứ GetRecordings trả (tt:Source,
    // tt:Content lấy từ cùng cấu hình) để RecordingInformation khớp GetRecordings. Kích thước,
    // fps, bitrate là cấu hình HIỆN TẠI của luồng (DVR không lưu lịch sử cấu hình theo từng file).
    // Trả false khi DVR không đọc được.
    struct CatalogTrack {
        std::string token, streamType, description;
        int width = 0, height = 0, framerate = 0, bitrate = 0;
        bool isRecording = false;
    };
    struct CatalogRecording {
        std::string token, videoSourceId, sourceId;   // sourceId = tt:SourceId (client đặt được)
        std::string sourceXml, contentXml;            // <tt:Source>…</tt:Source>, <tt:Content>…</tt:Content>
        std::vector<CatalogTrack> tracks;
    };
    bool catalog(std::vector<CatalogRecording>& out) const;

    // ── Cắt chuỗi / envelope SOAP, dùng chung với SearchService ──
    static std::string opName(const std::string& request);                 // phần tử đầu trong <Body>
    static std::string textOf(const std::string& xml, const std::string& name);   // văn bản thuần
    static std::string blockOf(const std::string& xml, const std::string& name);   // cả phần tử
    static std::string attrOf(const std::string& element, const std::string& name);
    static std::string reply(const std::string& rel, const char* op, const std::string& body);
    // Giá trị client gửi được giải mã khi nhận và thoát lại khi phát ra: response luôn hợp lệ
    // XML dù client gửi '&' trần, và dữ liệu lưu là văn bản thuần.
    static std::string esc(const std::string& text);
    static std::string unesc(const std::string& text);

private:
    struct Stream {
        std::string profileToken;   // token Media profile ("0", "0_sub")
        std::string streamType;     // "main" | "sub"
        std::string trackToken;     // "VIDEO_main" | "VIDEO_sub"
        bool isRecording = false;
        int width = 0, height = 0, framerate = 0, bitrate = 0;   // cấu hình encoder hiện tại
    };
    struct Source {
        std::string recordingToken, videoSourceId, name;
        std::vector<Stream> streams;
        const Stream* byProfile(const std::string& profileToken) const;
        const Stream* byTrack(const std::string& trackToken) const;
    };
    // DVR nhận lệnh bật ghi nhưng không bắt đầu ghi (luồng tắt, camera không có hình).
    struct RefusedError : std::runtime_error { using std::runtime_error::runtime_error; };

    // ── Recording / Track / Options (RecordingService.cpp) ─────
    std::string getRecordings(const std::string& rel);
    std::string getRecordingConfiguration(const std::string& req, const std::string& rel);
    std::string setRecordingConfiguration(const std::string& req, const std::string& rel);
    std::string getTrackConfiguration(const std::string& req, const std::string& rel);
    std::string setTrackConfiguration(const std::string& req, const std::string& rel);
    std::string getRecordingOptions(const std::string& req, const std::string& rel);

    // ── Recording Job (RecordingJobs.cpp) ──────────────────────
    std::string createJob(const std::string& req, const std::string& rel);
    std::string deleteJob(const std::string& req, const std::string& rel);
    std::string getJobs(const std::string& rel);
    std::string getJobConfiguration(const std::string& req, const std::string& rel);
    std::string setJobConfiguration(const std::string& req, const std::string& rel);
    std::string setJobMode(const std::string& req, const std::string& rel);
    std::string getJobState(const std::string& req, const std::string& rel);
    struct JobFields;   // trường client gửi trong <JobConfiguration>
    static JobFields parseJob(const std::string& scope);
    // Job trong danh sách: đã lưu, hoặc "quan sát được" (không nằm trong kho).
    struct JobView { RecordingJobRecord job; bool observed = false; };
    std::vector<JobView> listJobs(const std::vector<Source>& sources) const;
    bool findJobView(const std::string& token, const std::vector<Source>& sources, JobView& out) const;

    // ── DVR ───────────────────────────────────────────────────────
    // Thất bại → `fault` là SOAP fault để trả thẳng cho client.
    bool loadSources(std::vector<Source>& sources, std::string& fault) const;
    static const Source* findSource(const std::vector<Source>& sources, const std::string& token);
    // Bật/tắt ghi tay. Khi bật, đọc lại để xác nhận DVR thật sự ghi (DVR trả thành công cả khi
    // từ chối âm thầm); không thì ném RefusedError. Khi tắt không kiểm tra vì ghi theo lịch
    // có thể vẫn chạy. Lỗi DVR khác ném std::runtime_error.
    void setManualRecord(const Source& source, const Stream& stream, bool enable) const;

    // ── Dựng XML ──────────────────────────────────────────────────
    RecordingConfigRecord effectiveConfig(const Source& source) const;
    std::string configXml(const Source& source) const;
    static std::string sourceXml(const RecordingConfigRecord& config);
    static std::string contentXml(const RecordingConfigRecord& config);
    std::string tracksXml(const Source& source) const;
    // `sources` (nếu có) dùng để điền Tracks: luồng nguồn → track đích của recording.
    std::string jobConfigXml(const RecordingJobRecord& job, const std::vector<Source>* sources) const;
    static std::string trackOf(const RecordingJobRecord& job, const std::vector<Source>& sources);
    // "Active" khi job mong muốn ghi VÀ luồng nguồn đang ghi thật; ngược lại "Idle".
    static std::string jobStateOf(const RecordingJobRecord& job, const std::vector<Source>& sources);
    static RecordingJobEvent jobEvent(const RecordingJobRecord& job, const std::string& state,
                                      const std::string& trackToken);
    std::vector<RecordingJobEvent> initialJobEvents() const;

    static std::string faultNoRecording();
    static std::string faultNoTrack();
    static std::string faultNoJob();
    static std::string faultBadConfig(const char* reason);
    static std::string faultBackend();

    std::shared_ptr<IDvrClient> dvr_;
    std::shared_ptr<RecordingJobStore> store_;
    std::string serviceAddress_;
    // Tuần tự hóa thao tác ĐỔI trạng thái (vừa gọi DVR vừa sửa kho) để hai thao tác chồng nhau
    // không làm kho và DVR lệch nhau.
    std::mutex mutateMutex_;
};
