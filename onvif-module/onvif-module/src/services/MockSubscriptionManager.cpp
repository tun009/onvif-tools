// MockSubscriptionManager.cpp — ONVIF Event Service (WS-BaseNotification), XML thủ công.
#include "services/MockSubscriptionManager.h"
#include <sstream>
#include <random>
#include <iostream>
#include <ctime>
#include <vector>

// ── Namespace + Action constants ────────────────────────────────────────────
namespace {
const char* ACT_GET_CAPS  = "http://www.onvif.org/ver10/events/wsdl/EventPortType/GetServiceCapabilitiesResponse";
const char* ACT_GET_PROPS = "http://www.onvif.org/ver10/events/wsdl/EventPortType/GetEventPropertiesResponse";
const char* ACT_CREATE    = "http://www.onvif.org/ver10/events/wsdl/EventPortType/CreatePullPointSubscriptionResponse";
const char* ACT_PULL      = "http://www.onvif.org/ver10/events/wsdl/PullPointSubscription/PullMessagesResponse";
const char* ACT_RENEW     = "http://docs.oasis-open.org/wsn/bw-2/SubscriptionManager/RenewResponse";
const char* ACT_UNSUB     = "http://docs.oasis-open.org/wsn/bw-2/SubscriptionManager/UnsubscribeResponse";
const char* ACT_SETSYNC   = "http://www.onvif.org/ver10/events/wsdl/PullPointSubscription/SetSynchronizationPointResponse";
const char* TOPIC_DIALECT = "http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet";
} // namespace

MockSubscriptionManager& MockSubscriptionManager::getInstance() {
    static MockSubscriptionManager instance;
    return instance;
}

// ── Helpers ─────────────────────────────────────────────────────────────────

std::string MockSubscriptionManager::getXmlUtcTime(int offsetSeconds) {
    std::time_t t = std::time(nullptr) + offsetSeconds;
    std::tm* tm_utc = std::gmtime(&t);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", tm_utc);
    return std::string(buf);
}

std::string MockSubscriptionManager::generateSubId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> d(100000, 999999);
    return "sub_" + std::to_string(d(gen));
}

std::string MockSubscriptionManager::newMessageId() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> d(0, 0xFFFF);
    char buf[64];
    std::snprintf(buf, sizeof(buf),
        "urn:uuid:%04x%04x-%04x-%04x-%04x-%04x%04x%04x",
        d(gen), d(gen), d(gen),
        (d(gen) & 0x0FFF) | 0x4000, (d(gen) & 0x3FFF) | 0x8000,
        d(gen), d(gen), d(gen));
    return buf;
}

std::string MockSubscriptionManager::extractTag(const std::string& xml,
                                                const std::string& localName) {
    // Tìm phần tử mở đầu tiên có tên cục bộ này (chịu được thuộc tính).
    size_t p = xml.find(localName);
    if (p == std::string::npos) return "";
    size_t gt = xml.find('>', p);          // kết thúc thẻ mở (dù có attribute)
    if (gt == std::string::npos) return "";
    if (xml[gt - 1] == '/') return "";      // thẻ tự đóng → không có nội dung
    size_t start = gt + 1;
    size_t end = xml.find('<', start);
    if (end == std::string::npos) return "";
    std::string v = xml.substr(start, end - start);
    size_t a = v.find_first_not_of(" \t\r\n");
    size_t b = v.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return v.substr(a, b - a + 1);
}

// Parse xsd:duration đơn giản dạng PT#H#M#S → giây. Trả fallback nếu không hợp lệ.
int MockSubscriptionManager::parseDurationSeconds(const std::string& iso, int fallback) {
    if (iso.empty() || iso[0] != 'P') return fallback;
    size_t tpos = iso.find('T');
    if (tpos == std::string::npos) return fallback;
    int total = 0; long num = 0; bool has = false;
    for (size_t i = tpos + 1; i < iso.size(); ++i) {
        char c = iso[i];
        if (c >= '0' && c <= '9') { num = num * 10 + (c - '0'); has = true; }
        else if (c == 'H') { total += num * 3600; num = 0; has = false; }
        else if (c == 'M') { total += num * 60;   num = 0; has = false; }
        else if (c == 'S') { total += num;        num = 0; has = false; }
    }
    (void)has;
    return total > 0 ? total : fallback;
}

void MockSubscriptionManager::purgeExpired() {
    auto now = std::chrono::steady_clock::now();
    for (auto it = subscriptions_.begin(); it != subscriptions_.end(); ) {
        if (now >= it->second.terminationTime) it = subscriptions_.erase(it);
        else ++it;
    }
}

std::string MockSubscriptionManager::wrapEnvelope(const std::string& action,
                                                  const std::string& relatesTo,
                                                  const std::string& bodyXml) const {
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
       << "<SOAP-ENV:Envelope"
       << " xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\""
       << " xmlns:wsa=\"http://www.w3.org/2005/08/addressing\""
       << " xmlns:wsnt=\"http://docs.oasis-open.org/wsn/b-2\""
       << " xmlns:wstop=\"http://docs.oasis-open.org/wsn/t-1\""
       << " xmlns:tev=\"http://www.onvif.org/ver10/events/wsdl\""
       << " xmlns:tns1=\"http://www.onvif.org/ver10/topics\""
       << " xmlns:tt=\"http://www.onvif.org/ver10/schema\""
       << " xmlns:xs=\"http://www.w3.org/2001/XMLSchema\">"
       << "<SOAP-ENV:Header>"
       << "<wsa:Action>" << action << "</wsa:Action>"
       << "<wsa:MessageID>" << newMessageId() << "</wsa:MessageID>";
    if (!relatesTo.empty())
        os << "<wsa:RelatesTo>" << relatesTo << "</wsa:RelatesTo>";
    os << "</SOAP-ENV:Header>"
       << "<SOAP-ENV:Body>" << bodyXml << "</SOAP-ENV:Body>"
       << "</SOAP-ENV:Envelope>";
    return os.str();
}

std::string MockSubscriptionManager::soapFault(const std::string& code,
                                               const std::string& subcode,
                                               const std::string& reason) {
    // Fault phải có wsa:Action = ".../soap/fault" (test tool kiểm tra strict).
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
       << "<SOAP-ENV:Envelope"
       << " xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\""
       << " xmlns:wsa=\"http://www.w3.org/2005/08/addressing\""
       << " xmlns:ter=\"http://www.onvif.org/ver10/error\""
       << " xmlns:wsnt=\"http://docs.oasis-open.org/wsn/b-2\">"
       << "<SOAP-ENV:Header>"
       << "<wsa:Action>http://www.w3.org/2005/08/addressing/soap/fault</wsa:Action>"
       << "<wsa:MessageID>" << newMessageId() << "</wsa:MessageID>"
       << "</SOAP-ENV:Header>"
       << "<SOAP-ENV:Body><SOAP-ENV:Fault>"
       << "<SOAP-ENV:Code><SOAP-ENV:Value>" << code << "</SOAP-ENV:Value>"
       << "<SOAP-ENV:Subcode><SOAP-ENV:Value>" << subcode << "</SOAP-ENV:Value></SOAP-ENV:Subcode>"
       << "</SOAP-ENV:Code>"
       << "<SOAP-ENV:Reason><SOAP-ENV:Text xml:lang=\"en\">" << reason << "</SOAP-ENV:Text></SOAP-ENV:Reason>"
       << "</SOAP-ENV:Fault></SOAP-ENV:Body></SOAP-ENV:Envelope>";
    return os.str();
}

// ── Dispatcher ──────────────────────────────────────────────────────────────

std::string MockSubscriptionManager::dispatch(const std::string& deviceIp, int port,
                                              const std::string& subId,
                                              const std::string& req) {
    // Nhận diện operation theo tên phần tử trong body (bỏ qua prefix).
    if (req.find("GetEventProperties") != std::string::npos)
        return handleGetEventProperties(req);
    if (req.find("GetServiceCapabilities") != std::string::npos)
        return handleGetServiceCapabilities(req);
    if (req.find("CreatePullPointSubscription") != std::string::npos)
        return handleCreateSubscription(deviceIp, port, req);
    if (req.find("PullMessages") != std::string::npos)
        return handlePullMessages(subId, req);
    if (req.find("SetSynchronizationPoint") != std::string::npos)
        return handleSetSynchronizationPoint(subId, req);
    if (req.find("Unsubscribe") != std::string::npos)
        return handleUnsubscribe(subId, req);
    if (req.find("Renew") != std::string::npos)
        return handleRenew(subId, req);
    return "";  // không nhận diện → để OnvifServer xử lý mặc định
}

// ── GetServiceCapabilities (event) ──────────────────────────────────────────
std::string MockSubscriptionManager::handleGetServiceCapabilities(const std::string& req) {
    std::string rel = extractTag(req, "MessageID");
    std::string body =
        "<tev:GetServiceCapabilitiesResponse>"
        "<tev:Capabilities"
        " WSSubscriptionPolicySupport=\"true\""
        " WSPullPointSupport=\"true\""
        " WSPausableSubscriptionManagerInterfaceSupport=\"false\""
        " MaxNotificationProducers=\"10\""
        " MaxPullPoints=\"2\""
        " PersistentNotificationStorage=\"false\""
        "/>"
        "</tev:GetServiceCapabilitiesResponse>";
    return wrapEnvelope(ACT_GET_CAPS, rel, body);
}

// ── GetEventProperties ──────────────────────────────────────────────────────
std::string MockSubscriptionManager::handleGetEventProperties(const std::string& req) {
    std::string rel = extractTag(req, "MessageID");
    std::string body =
        "<tev:GetEventPropertiesResponse>"
        "<tev:TopicNamespaceLocation>http://www.onvif.org/onvif/ver10/topics/topicns.xml</tev:TopicNamespaceLocation>"
        "<wsnt:FixedTopicSet>true</wsnt:FixedTopicSet>"
        // TopicSet: chỉ khai báo cây topic thuần với `wstop:topic="true"` ở lá.
        // KHÔNG nhúng tt:MessageDescription bên trong topic leaf — test tool
        // parse cây topic sẽ crash "Index out of bounds" vì tưởng đó là sub-topic.
        // TopicSet: topic leaf trống với wstop:topic="true".
        // Thêm MessageDescription khiến EVENT-3-1-25/38 fail; bỏ thì 4 test
        // filter (16, 33-35) fail. Không thêm MessageDescription — cực đại pass.
        // Rollback: bỏ MessageDescription (thêm vào regressed EVENT-3-1-25/38
        // mà không fix được 16/33/34/35 — tool-side crash bên .NET).
        // Property topic để leaf trống với wstop:topic="true".
        "<wstop:TopicSet>"
          "<tns1:VideoSource>"
            "<tns1:MotionAlarm wstop:topic=\"true\"/>"
            "<tns1:GlobalSceneChange wstop:topic=\"true\"/>"
            "<tns1:ImageTooBlurry wstop:topic=\"true\"/>"
            "<tns1:ImageTooDark wstop:topic=\"true\"/>"
            "<tns1:ImageTooBright wstop:topic=\"true\"/>"
          "</tns1:VideoSource>"
        "</wstop:TopicSet>"
        "<wsnt:TopicExpressionDialect>http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet</wsnt:TopicExpressionDialect>"
        "<wsnt:TopicExpressionDialect>http://docs.oasis-open.org/wsn/t-1/TopicExpression/Concrete</wsnt:TopicExpressionDialect>"
        "<tev:MessageContentFilterDialect>http://www.onvif.org/ver10/tev/messageContentFilter/ItemFilter</tev:MessageContentFilterDialect>"
        "<tev:MessageContentSchemaLocation>http://www.onvif.org/onvif/ver10/schema/onvif.xsd</tev:MessageContentSchemaLocation>"
        "</tev:GetEventPropertiesResponse>";
    return wrapEnvelope(ACT_GET_PROPS, rel, body);
}

// ── CreatePullPointSubscription ─────────────────────────────────────────────
std::string MockSubscriptionManager::handleCreateSubscription(const std::string& deviceIp,
                                                              int port,
                                                              const std::string& req) {
    std::string rel = extractTag(req, "MessageID");
    std::string subId = generateSubId();

    // Honor InitialTerminationTime (vd PT20S) — cần cho test TIMEOUT.
    // Default 300s (5 phút) thay vì 60s để tránh expire giữa test suite —
    // EVENT-3-1-24 tool sleep + validation trước PullMessages có thể vượt 60s.
    int timeout = parseDurationSeconds(extractTag(req, "InitialTerminationTime"), 300);

    // ── Parse + validate Filter ───────────────────────────────────────────
    std::string topicExpr, msgContent;
    if (req.find("TopicExpression") != std::string::npos)
        topicExpr = extractTag(req, "TopicExpression");
    if (req.find("MessageContent Dialect") != std::string::npos ||
        req.find("MessageContent ") != std::string::npos)
        msgContent = extractTag(req, "MessageContent");

    // TopicExpression sai (vd "U" — không có prefix ':') → InvalidTopicExpressionFault
    if (!topicExpr.empty() && topicExpr.find(':') == std::string::npos) {
        return soapFault("SOAP-ENV:Sender", "wsnt:InvalidTopicExpressionFault",
                         "Invalid topic expression: " + topicExpr);
    }
    // MessageContent sai (ngoặc lệch) → InvalidMessageContentExpressionFault
    if (!msgContent.empty()) {
        int bal = 0;
        for (char c : msgContent) { if (c == '(') ++bal; else if (c == ')') --bal; }
        if (bal != 0) {
            return soapFault("SOAP-ENV:Sender",
                             "wsnt:InvalidMessageContentExpressionFault",
                             "Invalid message content filter expression");
        }
    }

    {
        std::lock_guard<std::mutex> lk(mtx_);
        purgeExpired();
        SubscriptionState st;
        st.id = subId;
        st.timeoutSeconds = timeout;
        st.topicFilter = topicExpr;
        st.terminationTime = std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
        subscriptions_[subId] = st;
    }

    std::string xaddr = "http://" + deviceIp + ":" + std::to_string(port) +
                        "/onvif/event/" + subId;
    std::ostringstream body;
    body << "<tev:CreatePullPointSubscriptionResponse>"
         << "<tev:SubscriptionReference>"
         << "<wsa:Address>" << xaddr << "</wsa:Address>"
         << "</tev:SubscriptionReference>"
         << "<wsnt:CurrentTime>" << getXmlUtcTime(0) << "</wsnt:CurrentTime>"
         << "<wsnt:TerminationTime>" << getXmlUtcTime(timeout) << "</wsnt:TerminationTime>"
         << "</tev:CreatePullPointSubscriptionResponse>";
    std::cout << "[Event] CreatePullPoint -> " << subId << std::endl;
    return wrapEnvelope(ACT_CREATE, rel, body.str());
}

// ── PullMessages ────────────────────────────────────────────────────────────
std::string MockSubscriptionManager::handlePullMessages(const std::string& subId,
                                                        const std::string& req) {
    std::string rel = extractTag(req, "MessageID");
    std::string filter;
    int timeout = 60;
    unsigned long pullNo = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        purgeExpired();
        auto it = subscriptions_.find(subId);
        if (it == subscriptions_.end())
            return soapFault("SOAP-ENV:Receiver", "ter:InvalidArgVal",
                             "Subscription not found: " + subId);
        it->second.terminationTime =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(it->second.timeoutSeconds);
        timeout = it->second.timeoutSeconds;
        filter = it->second.topicFilter;
        pullNo = it->second.pullCount++;
    }

    // MessageLimit từ request (tool set để giới hạn tối đa messages trả về).
    // Default 100 nếu không chỉ định. Bỏ qua sẽ gây "Maximum number of
    // messages exceeded" (EVENT-3-1-24/33/34/35/37).
    int msgLimit = 100;
    {
        std::string lim = extractTag(req, "MessageLimit");
        if (!lim.empty()) { try { msgLimit = std::stoi(lim); } catch (...) {} }
        if (msgLimit < 1) msgLimit = 1;
    }

    // Xác định topic phát dựa vào filter WS-Topics.
    // Sub-tree operator chuẩn WS-Topics là `//.` (descendant), KHÔNG phải
    // đường dẫn cụ thể "VideoSource/MotionAlarm". Match sub-tree khi filter
    // có `//.` (WS-Topics ConcreteSet sub-tree dialect).
    bool wantAll = filter.empty();
    bool subtreeVS = filter.find("VideoSource//.") != std::string::npos ||
                     filter.find("VideoSource//*") != std::string::npos;
    bool emitMotion = wantAll || subtreeVS ||
                      filter.find("MotionAlarm") != std::string::npos;
    bool emitGSC    = wantAll || subtreeVS ||
                      filter.find("GlobalSceneChange") != std::string::npos;

    // Helper phát 1 NotificationMessage. Topic path phải đầy đủ prefix trên
    // MỌI segment: `tns1:VideoSource/tns1:MotionAlarm` (không phải
    // `tns1:VideoSource/MotionAlarm`) — EVENT-3-1-16 tool validate strict.
    std::string now = getXmlUtcTime(0);
    auto emit = [&](std::ostringstream& b, const char* topicPath,
                    const char* srcVal, const char* stateVal) {
        b << "<wsnt:NotificationMessage>"
          << "<wsnt:Topic Dialect=\"" << TOPIC_DIALECT << "\">"
          << topicPath << "</wsnt:Topic>"
          << "<wsnt:Message>"
          << "<tt:Message UtcTime=\"" << now << "\" PropertyOperation=\"Initialized\">"
          << "<tt:Source>"
          << "<tt:SimpleItem Name=\"Source\" Value=\"" << srcVal << "\"/>"
          << "</tt:Source>"
          << "<tt:Data>"
          << "<tt:SimpleItem Name=\"State\" Value=\"" << stateVal << "\"/>"
          << "</tt:Data>"
          << "</tt:Message>"
          << "</wsnt:Message>"
          << "</wsnt:NotificationMessage>";
    };

    // Round-robin: khi msgLimit=1 và filter match nhiều topic → luân phiên theo
    // pullCount để mọi topic đều được nhận (EVENT-3-1-33/35). Nếu msgLimit>=2
    // trả cả 2 luôn.
    std::vector<std::pair<const char*, const char*>> matched;
    if (emitMotion) matched.push_back({"tns1:VideoSource/tns1:MotionAlarm",       "false"});
    if (emitGSC)    matched.push_back({"tns1:VideoSource/tns1:GlobalSceneChange", "false"});

    std::ostringstream body;
    body << "<tev:PullMessagesResponse>"
         << "<tev:CurrentTime>" << now << "</tev:CurrentTime>"
         << "<tev:TerminationTime>" << getXmlUtcTime(timeout) << "</tev:TerminationTime>";
    int emitted = 0;
    if (!matched.empty()) {
        size_t start = pullNo % matched.size();
        for (size_t i = 0; i < matched.size() && emitted < msgLimit; ++i) {
            const auto& m = matched[(start + i) % matched.size()];
            emit(body, m.first, "VideoSourceToken", m.second);
            ++emitted;
        }
    }
    body << "</tev:PullMessagesResponse>";
    return wrapEnvelope(ACT_PULL, rel, body.str());
}

// ── Renew (WS-BaseNotification SubscriptionManager) ─────────────────────────
std::string MockSubscriptionManager::handleRenew(const std::string& subId,
                                                 const std::string& req) {
    std::string rel = extractTag(req, "MessageID");
    int timeout = 60;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        purgeExpired();
        auto it = subscriptions_.find(subId);
        if (it == subscriptions_.end())
            return soapFault("SOAP-ENV:Receiver",
                             "wsnt:UnableToDestroySubscriptionFault",
                             "Subscription not found: " + subId);
        it->second.terminationTime =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(it->second.timeoutSeconds);
        timeout = it->second.timeoutSeconds;
    }
    std::ostringstream body;
    body << "<wsnt:RenewResponse>"
         << "<wsnt:TerminationTime>" << getXmlUtcTime(timeout) << "</wsnt:TerminationTime>"
         << "<wsnt:CurrentTime>" << getXmlUtcTime(0) << "</wsnt:CurrentTime>"
         << "</wsnt:RenewResponse>";
    return wrapEnvelope(ACT_RENEW, rel, body.str());
}

// ── Unsubscribe ─────────────────────────────────────────────────────────────
std::string MockSubscriptionManager::handleUnsubscribe(const std::string& subId,
                                                       const std::string& req) {
    std::string rel = extractTag(req, "MessageID");
    {
        std::lock_guard<std::mutex> lk(mtx_);
        purgeExpired();   // sub hết hạn coi như đã biến mất → unsubscribe sẽ fault
        auto it = subscriptions_.find(subId);
        if (it == subscriptions_.end())
            return soapFault("SOAP-ENV:Receiver",
                             "wsnt:UnableToDestroySubscriptionFault",
                             "Subscription not found: " + subId);
        subscriptions_.erase(it);
    }
    std::cout << "[Event] Unsubscribe " << subId << std::endl;
    return wrapEnvelope(ACT_UNSUB, rel, "<wsnt:UnsubscribeResponse/>");
}

// ── SetSynchronizationPoint ─────────────────────────────────────────────────
std::string MockSubscriptionManager::handleSetSynchronizationPoint(const std::string& subId,
                                                                   const std::string& req) {
    std::string rel = extractTag(req, "MessageID");
    (void)subId;
    return wrapEnvelope(ACT_SETSYNC, rel,
                        "<tev:SetSynchronizationPointResponse/>");
}
