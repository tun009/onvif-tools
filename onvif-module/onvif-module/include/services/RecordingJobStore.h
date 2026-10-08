#pragma once
// RecordingJobStore — kho dữ liệu Recording Control mà DVR không có và phải do onvif-module
// giữ: Recording Job, cấu hình Recording/Track do client đặt (Set*Configuration).
//
// Spec (Recording Control §5.2): mọi đối tượng phải bền qua mất điện → lưu ra file ở
// `path` mỗi khi có thay đổi: ghi file tạm, fsync, rename, fsync thư mục chứa (không có hai
// lần fsync thì mất điện đột ngột có thể để lại file rỗng hoặc bản cũ).
// `path` rỗng = chỉ giữ trong bộ nhớ (log cảnh báo khi khởi tạo).
//
// Chỉ lưu CẤU HÌNH. Trạng thái ghi thật luôn đọc từ DVR, không lưu bản sao ở đây.
// Giá trị là văn bản thuần (đã giải mã XML); lớp gọi tự thoát khi dựng response.

#include <map>
#include <mutex>
#include <string>
#include <vector>

struct RecordingJobRecord {
    std::string token;           // "job_1"
    std::string recordingToken;  // "rec_0"
    std::string mode;            // "Idle" | "Active"
    std::string priority;        // "0".."100" (chuỗi số nguyên)
    std::string sourceToken;     // token Media profile, ví dụ "0" / "0_sub"
    std::string sourceType;      // thuộc tính Type của SourceToken
};

struct RecordingConfigRecord {
    std::string sourceId, name, location, description, address;
    std::string content;
    std::string maxRetention;    // xs:duration, "PT0S" = không giới hạn theo thời gian
};

class RecordingJobStore {
public:
    explicit RecordingJobStore(std::string path);

    // ── Job ───────────────────────────────────────────────────────
    std::vector<RecordingJobRecord> jobs() const;
    bool findJob(const std::string& token, RecordingJobRecord& out) const;
    bool findJobByRecording(const std::string& recordingToken, RecordingJobRecord& out) const;
    // Gán token mới (không bao giờ dùng lại token đã cấp) và lưu.
    RecordingJobRecord addJob(RecordingJobRecord job);
    // Lưu job với token do caller chọn (dùng khi "nhận" job quan sát được, giữ nguyên token
    // client đã thấy). Trả false nếu token đã tồn tại.
    bool addJobWithToken(const RecordingJobRecord& job);
    bool updateJob(const RecordingJobRecord& job);
    bool removeJob(const std::string& token);

    // ── Cấu hình Recording / Track do client đặt ──────────────────
    bool getConfig(const std::string& recordingToken, RecordingConfigRecord& out) const;
    void setConfig(const std::string& recordingToken, const RecordingConfigRecord& config);
    bool getTrackDescription(const std::string& recordingToken, const std::string& trackToken,
                             std::string& out) const;
    void setTrackDescription(const std::string& recordingToken, const std::string& trackToken,
                             const std::string& description);

private:
    void load();
    void saveLocked() const;   // gọi khi đang giữ mutex_

    std::string path_;
    mutable std::mutex mutex_;
    std::vector<RecordingJobRecord> jobs_;
    std::map<std::string, RecordingConfigRecord> configs_;
    std::map<std::string, std::string> trackDescriptions_;  // khóa "rec|track"
    unsigned long nextJobNumber_ = 1;
};
