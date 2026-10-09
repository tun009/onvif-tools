#pragma once
// RecordingIndex — danh mục khoảng thời gian có dữ liệu ghi của DVR, đọc thẳng từ thư mục file
// (mặc định /media/records/<context|alpr>/<profile>_YYYYMMDD_HHMMSS.mp4) cho ONVIF Recording Search.
//
// Vì sao không dùng REST của DVR: `RecordPlaybackfilter` trả StartTime/EndTime sai (lệch tới vài
// chục giây, xem 01-IMPLEMENTATION_PLAN.md Phase 7). Tên file là giờ MỞ file, nhưng khung hình
// đầu tới sau đó (phải chờ keyframe). Cách tính ở đây:
//   kết thúc = mtime của file (lúc DVR đóng file; file đã +faststart)
//   bắt đầu  = mtime − thời lượng thật (đọc từ hộp mvhd ở đầu file MP4)
// Sai số đã đo ≈ ±1,5 s. File đang ghi dở chưa có hộp moov → bỏ qua cho tới khi đóng.
//
// Chỉ đọc thư mục, không gọi DVR. Thread-safe.

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

struct TimeRange {
    int64_t fromMs = 0;   // epoch UTC, mili giây
    int64_t toMs = 0;
};

class RecordingIndex {
public:
    explicit RecordingIndex(std::string rootDir);

    // Các đoạn liên tục (đã nối những đoạn cách nhau ≤ kGapMs) của 1 luồng, tăng dần theo thời gian.
    // `videoSourceId` "0"/"1", `streamType` "main"/"sub". Làm tươi danh mục nếu cũ quá kRefreshMs.
    std::vector<TimeRange> ranges(const std::string& videoSourceId, const std::string& streamType);

    // Hai đoạn cách nhau tới ngần này vẫn coi là một (tránh tách đoạn vì sai số mtime/keyframe).
    static constexpr int64_t kGapMs = 5000;

private:
    struct Entry {
        std::string videoSourceId, streamType;
        int64_t mtimeMs = 0;
        int64_t size = 0;
        int64_t durationMs = 0;   // 0 = chưa đọc được (file đang ghi dở)
    };
    void refreshLocked();

    std::string root_;
    std::mutex mutex_;
    std::map<std::string, Entry> files_;   // khóa: đường dẫn đầy đủ
    int64_t lastRefreshMs_ = 0;            // đồng hồ steady
    static constexpr int64_t kRefreshMs = 5000;
};
