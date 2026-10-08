#pragma once
// MockSubscriptionManager — ONVIF Event Service (WS-BaseNotification) bằng XML thủ công.
// Xử lý toàn bộ event ops mà Profile T yêu cầu:
//   GetServiceCapabilities, GetEventProperties, CreatePullPointSubscription,
//   PullMessages, Renew, Unsubscribe, SetSynchronizationPoint.
//
// Vì các phần TopicSet / NotificationMessage của schema là xsd:any (gSOAP sinh
// class rỗng, không gắn được), toàn bộ được dựng bằng XML chuẩn tay để kiểm
// soát chính xác wire-format mà ONVIF Device Test Tool mong đợi.

#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <chrono>
#include <atomic>
#include <functional>
#include <thread>

struct SubscriptionState {
    std::string id;
    std::chrono::steady_clock::time_point terminationTime;
    int timeoutSeconds = 60;
    std::string topicFilter;   // nội dung TopicExpression (rỗng = không lọc)
    unsigned long pullCount = 0;
    // Event phát do thao tác (CreateProfile/Set*): mỗi phần tử là 1 chuỗi
    // <wsnt:NotificationMessage> dựng sẵn, PullMessages trả về rồi xóa.
    std::vector<std::string> pending;
    // Basic Notification: ConsumerReference URL để POST Notify tới.
    // Rỗng nếu là PullPoint subscription (server không tự push, tool pull).
    std::string consumerUrl;
};

// Thông tin 1 Recording Job dùng để dựng event tns1:RecordingConfig/JobState.
struct RecordingJobEvent {
    std::string jobToken;
    std::string recordingToken;
    std::string sourceToken;   // token Media profile
    std::string sourceType;    // thuộc tính Type của SourceToken
    std::string state;         // Idle | Active | PartiallyActive | Error
    std::string trackToken;    // track đích của job (rỗng = không khai báo track, giữ dạng cũ)
};

class MockSubscriptionManager {
public:
    static MockSubscriptionManager& getInstance();

    // Điểm vào duy nhất: nhận request thô + subId (rỗng nếu gửi tới /onvif/event),
    // trả về XML SOAP response. Trả "" nếu không nhận diện được operation.
    std::string dispatch(const std::string& deviceIp, int port,
                         const std::string& subId, const std::string& rawRequest);

    // Phát event khi Media2 thao tác — enqueue vào các subscription khớp filter
    // để PullMessages trả về. Gọi từ Media2Service/Media2MetadataService.
    //   tns1:Media/ProfileChanged: Source SimpleItem Token=<profile token>
    //   tns1:Media/ConfigurationChanged: Source Token=<cfg token> + Type=<type>
    void fireProfileChanged(const std::string& token);
    void fireConfigurationChanged(const std::string& token, const std::string& type);
    void fireRecordingConfigChanged(const std::string& topicName,
                                    const std::string& sourceXml,
                                    const std::string& configurationXml);
    void fireRecordingJobState(const std::string& jobToken, const std::string& state);
    // Bản dùng job thật (token Recording/Source lấy từ `job`, không cứng Recording_0).
    void fireRecordingJobState(const RecordingJobEvent& job);

    // Đăng ký nguồn job thật cho event JobState dạng Initialized (gửi ở lần PullMessages
    // đầu tiên của mỗi subscription). Chưa đăng ký (mock) → giữ nguyên hành vi cũ: 1 event
    // Initialized cho Job_0. Hàm này được gọi KHÔNG giữ khóa nội bộ của manager, nên được
    // phép gọi xuống DVR.
    using RecordingJobProvider = std::function<std::vector<RecordingJobEvent>()>;
    void setRecordingJobProvider(RecordingJobProvider provider);

private:
    // ── Handlers từng operation ───────────────────────────────────────────
    std::string handleGetServiceCapabilities(const std::string& req);
    std::string handleGetEventProperties(const std::string& req);
    std::string handleCreateSubscription(const std::string& deviceIp, int port,
                                         const std::string& req);
    std::string handleBaseSubscribe(const std::string& deviceIp, int port,
                                    const std::string& req);
    std::string handlePullMessages(const std::string& subId, const std::string& req);
    std::string handleRenew(const std::string& subId, const std::string& req);
    std::string handleUnsubscribe(const std::string& subId, const std::string& req);
    std::string handleSetSynchronizationPoint(const std::string& subId,
                                              const std::string& req);

    // Basic Notification: gửi HTTP POST Notify tới ConsumerReference URL.
    // Chạy trong background thread — mỗi 5s scan subscriptions với consumerUrl
    // và push MotionAlarm event.
    void startNotifyThread();
    void stopNotifyThread();
    void notifyPushLoop();
    static bool httpPostNotify(const std::string& url, const std::string& xml);

    // ── Helpers ───────────────────────────────────────────────────────────
    static std::string getXmlUtcTime(int offsetSeconds = 0);
    static std::string generateSubId();
    static std::string newMessageId();
    // Trích nội dung phần tử <...local>VALUE</...local> đầu tiên (bỏ qua prefix).
    static std::string extractTag(const std::string& xml, const std::string& localName);
    // Kiểm tra phần tử theo local-name, không phụ thuộc prefix namespace.
    static bool hasElement(const std::string& xml, const std::string& localName);
    static std::string extractElementBlock(const std::string& xml,
                                           const std::string& localName);
    // Khung envelope + header (Action, MessageID, RelatesTo) + body.
    std::string wrapEnvelope(const std::string& action,
                             const std::string& relatesTo,
                             const std::string& bodyXml) const;
    // code = "SOAP-ENV:Sender" hoặc "SOAP-ENV:Receiver"
    static std::string soapFault(const std::string& code,
                                 const std::string& subcode,
                                 const std::string& reason);
    static int parseDurationSeconds(const std::string& iso, int fallback);
    void purgeExpired();   // xóa các subscription đã hết hạn

    std::mutex mtx_;
    RecordingJobProvider jobProvider_;   // bảo vệ bởi mtx_
    std::map<std::string, SubscriptionState> subscriptions_;
    std::atomic<bool> notifyRunning_{false};
    std::thread notifyThread_;
};
