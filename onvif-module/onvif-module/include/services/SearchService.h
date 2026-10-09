#pragma once
// SearchService — ONVIF Recording Search (tse) Profile G chạy trên dữ liệu ghi thật của DVR.
//
// Chỉ đăng ký khi capability `search = real`. Dùng cùng mô hình Recording với
// RecordingService (token recording/track phải khớp để GetRecordings == RecordingInformation):
//   - 1 Recording / sensor ("rec_0", "rec_1"), track "VIDEO_main" / "VIDEO_sub".
//   - Khoảng có dữ liệu của mỗi track lấy từ RecordingIndex (quét file mp4: bắt đầu = mtime −
//     thời lượng, kết thúc = mtime). Không có audio/metadata → bộ lọc loại track đó trả rỗng.
//   - FindEvents dựng sự kiện lịch sử từ ranh giới các đoạn: RecordingHistory/Recording/State và
//     RecordingHistory/Track/State. Chỉ dựng được từ khoảng hở/đầu/cuối đoạn, không có sự kiện
//     phân tích nào khác (DVR không lưu). Bộ lọc MessageContent (XPath) bị bỏ qua.
//   - Phiên tìm kiếm thật: token không dùng lại, KeepAliveTime tính từ lần gọi cuối, EndSearch.
//     Kết quả chụp tại lúc Find (snapshot) nên không bao giờ phải chờ MinResults/WaitTime.
//   - Chỉ Find/GetXxxResults cho Recording và Event; PTZ/Metadata không hỗ trợ (capability false).

#include "core/IOnvifService.h"
#include "services/RecordingService.h"
#include "services/RecordingIndex.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class SearchService : public IOnvifService {
public:
    // `recording` cho danh sách Recording/Track (phải sống lâu hơn service này).
    SearchService(RecordingService& recording, std::shared_ptr<RecordingIndex> index);

    std::string pathPrefix() const override { return "/onvif/search"; }
    std::string name() const override { return "SearchService"; }
    std::string handle(const std::string& rawRequest) override;

private:
    using Rec = RecordingService::CatalogRecording;

    // Dữ liệu 1 track tại lúc tra cứu.
    struct TrackData {
        const RecordingService::CatalogTrack* track = nullptr;
        std::vector<TimeRange> ranges;
    };
    struct RecData {
        const Rec* rec = nullptr;
        std::vector<TrackData> tracks;   // mọi track của recording (kể cả chưa có dữ liệu)
        bool hasData() const;
        int64_t earliest() const;        // chỉ gọi khi hasData()
        int64_t latest() const;
    };

    struct Session {
        bool events = false;             // false = FindRecordings
        int64_t keepAliveMs = 10000;
        int64_t expiresMs = 0;           // đồng hồ steady
        std::vector<std::string> items;  // kết quả XML đã dựng
        std::vector<int64_t> times;      // thời gian từng kết quả (events), để EndSearch báo điểm đã tới
        std::size_t next = 0;            // kết quả đầu tiên chưa trả
        int64_t startPointMs = 0, endPointMs = 0;   // events
        bool hasEndPoint = false;
    };

    // Phạm vi tìm (SearchScope) đã giải thích; `fault` khác rỗng khi token sai.
    struct Scope {
        std::vector<RecData> recordings;
        std::string filter;              // XPath RecordingInformationFilter (có thể rỗng)
    };
    bool resolveScope(const std::string& request, const std::vector<Rec>& catalog, Scope& out,
                      std::string& fault);
    std::vector<RecData> collect(const std::vector<Rec>& catalog);

    std::string recordingInformation(const RecData& data) const;
    static std::string trackInformation(const TrackData& data);
    static bool matchesFilter(const RecData& data, const std::string& xpath, bool& understood);
    std::vector<std::pair<int64_t, std::string>> historyEvents(const RecData& data, int64_t lo, int64_t hi,
                                                              const std::string& topicFilter) const;
    static std::string eventXml(const std::string& recording, const std::string& track, int64_t timeMs,
                                const char* topic, const char* operation, const char* dataName,
                                bool value, bool startState);

    std::string getRecordingSummary(const std::string& rel);
    std::string getRecordingInformation(const std::string& req, const std::string& rel);
    std::string getMediaAttributes(const std::string& req, const std::string& rel);
    std::string findRecordings(const std::string& req, const std::string& rel);
    std::string findEvents(const std::string& req, const std::string& rel);
    std::string getResults(const std::string& req, const std::string& rel, bool events);
    std::string endSearch(const std::string& req, const std::string& rel);

    // Quản lý phiên (gọi khi đang giữ mutex_).
    void purgeExpiredLocked(int64_t nowMs);
    std::string newSessionLocked(Session session);

    static std::string reply(const std::string& rel, const char* op, const std::string& body);
    static std::string faultToken();
    static std::string faultBackend();

    RecordingService& recording_;
    std::shared_ptr<RecordingIndex> index_;
    std::mutex mutex_;
    std::map<std::string, Session> sessions_;
    uint64_t counter_ = 0;
    std::string tokenPrefix_;   // khác nhau giữa các lần chạy: không dùng lại token sau restart
};
