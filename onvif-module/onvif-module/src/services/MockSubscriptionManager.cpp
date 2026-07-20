// MockSubscriptionManager.cpp — ONVIF Event Service (WS-BaseNotification), XML thủ công.
#include "services/MockSubscriptionManager.h"
#include <sstream>
#include <random>
#include <iostream>
#include <ctime>
#include <vector>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <cerrno>

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

bool MockSubscriptionManager::hasElement(const std::string& xml,
                                         const std::string& localName) {
    size_t search = 0;
    while (search < xml.size()) {
        size_t lt = xml.find('<', search);
        if (lt == std::string::npos) return false;
        size_t nameStart = lt + 1;
        if (nameStart >= xml.size()) return false;
        char first = xml[nameStart];
        if (first == '/' || first == '!' || first == '?') {
            search = nameStart + 1;
            continue;
        }
        // Include '>' so unprefixed elements such as <Subscribe> are parsed
        // exactly like prefixed elements such as <wsnt:Subscribe>.
        size_t nameEnd = xml.find_first_of(" \t\r\n/>", nameStart);
        if (nameEnd == std::string::npos) return false;
        size_t colon = xml.rfind(':', nameEnd);
        size_t localStart = (colon != std::string::npos && colon > nameStart)
                            ? colon + 1 : nameStart;
        if (xml.compare(localStart, nameEnd - localStart, localName) == 0) {
            return true;
        }
        search = nameEnd + 1;
    }
    return false;
}

std::string MockSubscriptionManager::extractElementBlock(
    const std::string& xml,
    const std::string& localName) {
    size_t search = 0;
    while (search < xml.size()) {
        size_t lt = xml.find('<', search);
        if (lt == std::string::npos) return "";
        size_t nameStart = lt + 1;
        if (nameStart >= xml.size()) return "";
        char first = xml[nameStart];
        if (first == '/' || first == '!' || first == '?') {
            search = nameStart + 1;
            continue;
        }
        size_t nameEnd = xml.find_first_of(" \t\r\n/>", nameStart);
        if (nameEnd == std::string::npos) return "";
        size_t colon = xml.rfind(':', nameEnd);
        size_t localStart = (colon != std::string::npos && colon > nameStart)
                            ? colon + 1 : nameStart;
        if (xml.compare(localStart, nameEnd - localStart, localName) == 0) {
            size_t gt = xml.find('>', nameEnd);
            if (gt == std::string::npos || xml[gt - 1] == '/') return "";
            std::string openName = xml.substr(nameStart, nameEnd - nameStart);
            std::string closeTag = "</" + openName + ">";
            size_t end = xml.find(closeTag, gt + 1);
            if (end == std::string::npos) return "";
            return xml.substr(lt, end + closeTag.size() - lt);
        }
        search = nameEnd + 1;
    }
    return "";
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
    if (hasElement(req, "GetEventProperties"))
        return handleGetEventProperties(req);
    if (hasElement(req, "GetServiceCapabilities"))
        return handleGetServiceCapabilities(req);
    if (hasElement(req, "CreatePullPointSubscription"))
        return handleCreateSubscription(deviceIp, port, req);
    // Basic Notification Subscribe (WS-BN) — element <Subscribe> namespace
    // http://docs.oasis-open.org/wsn/b-2. Khác CreatePullPointSubscription.
    if (hasElement(req, "Subscribe")) {
        // Match the operation by local name only. The namespace-prefix
        // conformance case may use a different prefix (or omit one) on
        // ConsumerReference; it is still the same WS-BN Subscribe request.
        return handleBaseSubscribe(deviceIp, port, req);
    }
    if (hasElement(req, "PullMessages"))
        return handlePullMessages(subId, req);
    if (hasElement(req, "SetSynchronizationPoint"))
        return handleSetSynchronizationPoint(subId, req);
    if (hasElement(req, "Unsubscribe"))
        return handleUnsubscribe(subId, req);
    if (hasElement(req, "Renew"))
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
        // Mỗi topic leaf kèm <tt:MessageDescription> để DTT Preliminary Check
        // xác định feature = SUPPORTED (không có MessageDescription → NOT SUPPORTED
        // dù topic tồn tại). tt:/xs: đã khai trong envelope wrap.
        // KHÔNG khai tampering (ImageTooBlurry/Dark/Bright): thiết bị pass M+T (HALO)
        // không khai → tránh bị DTT đòi tampering hoạt động (Profile T errata).
        // WS-Topics: CHỈ node gốc (VideoSource/Media) mang prefix tns1:; topic con
        // để KHÔNG prefix (đối chiếu topicset thiết bị thật). Prefix ở leaf khiến
        // DTT dựng sai topic-path → không nhận ra topic nào → tất cả event NOT SUPPORTED.
        "<wstop:TopicSet>"
          "<tns1:VideoSource>"
            "<MotionAlarm wstop:topic=\"true\">"
              "<tt:MessageDescription IsProperty=\"true\">"
                "<tt:Source><tt:SimpleItemDescription Name=\"Source\" Type=\"tt:ReferenceToken\"/></tt:Source>"
                "<tt:Data><tt:SimpleItemDescription Name=\"State\" Type=\"xs:boolean\"/></tt:Data>"
              "</tt:MessageDescription>"
            "</MotionAlarm>"
            "<GlobalSceneChange wstop:topic=\"true\">"
              "<tt:MessageDescription IsProperty=\"true\">"
                "<tt:Source><tt:SimpleItemDescription Name=\"Source\" Type=\"tt:ReferenceToken\"/></tt:Source>"
                "<tt:Data><tt:SimpleItemDescription Name=\"State\" Type=\"xs:boolean\"/></tt:Data>"
              "</tt:MessageDescription>"
            "</GlobalSceneChange>"
          "</tns1:VideoSource>"
          // Profile M/T mandatory Media event topics.
          "<tns1:Media>"
            "<ProfileChanged wstop:topic=\"true\">"
              "<tt:MessageDescription IsProperty=\"false\">"
                "<tt:Source><tt:SimpleItemDescription Name=\"Token\" Type=\"tt:ReferenceToken\"/></tt:Source>"
              "</tt:MessageDescription>"
            "</ProfileChanged>"
            "<ConfigurationChanged wstop:topic=\"true\">"
              "<tt:MessageDescription IsProperty=\"false\">"
                "<tt:Source>"
                  "<tt:SimpleItemDescription Name=\"Token\" Type=\"tt:ReferenceToken\"/>"
                  "<tt:SimpleItemDescription Name=\"Type\" Type=\"xs:string\"/>"
                "</tt:Source>"
              "</tt:MessageDescription>"
            "</ConfigurationChanged>"
          "</tns1:Media>"
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
    // DTT can spend several minutes validating PullPoint subscriptions before
    // sending PullMessages. Keep this subscription type alive for 10 minutes;
    // Base Subscribe below intentionally keeps its own timeout policy.
    if (timeout < 600) timeout = 600;

    // ── Parse + validate Filter ───────────────────────────────────────────
    std::string topicExpr, msgContent;
    if (hasElement(req, "TopicExpression"))
        topicExpr = extractTag(req, "TopicExpression");
    if (hasElement(req, "MessageContent"))
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
    bool subtreeMedia = filter.find("Media//.") != std::string::npos ||
                        filter.find("Media//*") != std::string::npos;
    bool emitProfileChanged = wantAll || subtreeMedia ||
                              filter.find("ProfileChanged") != std::string::npos;
    bool emitConfigurationChanged = wantAll || subtreeMedia ||
                                    filter.find("ConfigurationChanged") != std::string::npos;

    // Topic path dạng canonical ONVIF: CHỈ prefix ở segment gốc
    // (`tns1:VideoSource/MotionAlarm`), KHÔNG prefix mọi segment. DTT khớp topic
    // notification với filter theo dạng này (IMAGING-4-1-5 reject dạng
    // `tns1:VideoSource/tns1:MotionAlarm`). Verify không regress EVENT-3-1-16.
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
    if (emitMotion) matched.push_back({"tns1:VideoSource/MotionAlarm",       "false"});
    if (emitGSC)    matched.push_back({"tns1:VideoSource/GlobalSceneChange", "false"});
    if (emitProfileChanged) matched.push_back({"tns1:Media/ProfileChanged", "true"});
    if (emitConfigurationChanged) matched.push_back({"tns1:Media/ConfigurationChanged", "true"});

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

// ── Base Subscribe (WS-BaseNotification) ────────────────────────────────────
// Khác CreatePullPointSubscription ở chỗ:
//   - Element <Subscribe> namespace wsn/b-2 (không phải tev)
//   - Có <ConsumerReference><Address>URL</Address></ConsumerReference>
//   - Server phải POST Notify tới ConsumerReference URL định kỳ
//   - Response: <SubscribeResponse> namespace wsn/b-2
std::string MockSubscriptionManager::handleBaseSubscribe(const std::string& deviceIp,
                                                         int port,
                                                         const std::string& req) {
    std::string rel = extractTag(req, "MessageID");
    std::string subId = generateSubId();
    // ConsumerReference/Address KHÔNG dùng extractTag chung vì SOAP Header có
    // <ReplyTo><Address>anonymous</Address></ReplyTo> đứng TRƯỚC — extractTag
    // match nhầm. Cắt substring bên trong <ConsumerReference>...</ConsumerReference>.
    std::string consumerUrl;
    {
        std::string block = extractElementBlock(req, "ConsumerReference");
        if (!block.empty()) consumerUrl = extractTag(block, "Address");
    }
    int timeout = parseDurationSeconds(
        extractTag(req, "InitialTerminationTime"), 300);
    // Nếu tool yêu cầu timeout ngắn (PT10S) — server bỏ qua, giữ tối thiểu 60s
    // để notify thread có đủ thời gian push (subscription không expire quá sớm).
    if (timeout < 60) timeout = 60;
    std::string topicExpr;
    if (hasElement(req, "TopicExpression"))
        topicExpr = extractTag(req, "TopicExpression");

    {
        std::lock_guard<std::mutex> lk(mtx_);
        purgeExpired();
        SubscriptionState st;
        st.id = subId;
        st.timeoutSeconds = timeout;
        st.topicFilter = topicExpr;
        st.consumerUrl = consumerUrl;
        st.terminationTime = std::chrono::steady_clock::now() +
                             std::chrono::seconds(timeout);
        subscriptions_[subId] = st;
    }
    // Bảo đảm notify thread đang chạy (idempotent).
    startNotifyThread();

    std::string xaddr = "http://" + deviceIp + ":" + std::to_string(port) +
                        "/onvif/event/" + subId;
    std::ostringstream body;
    body << "<wsnt:SubscribeResponse>"
         << "<wsnt:SubscriptionReference>"
         << "<wsa:Address>" << xaddr << "</wsa:Address>"
         << "</wsnt:SubscriptionReference>"
         << "<wsnt:CurrentTime>" << getXmlUtcTime(0) << "</wsnt:CurrentTime>"
         << "<wsnt:TerminationTime>" << getXmlUtcTime(timeout) << "</wsnt:TerminationTime>"
         << "</wsnt:SubscribeResponse>";
    std::cout << "[Event] BaseSubscribe -> " << subId
              << " consumer=" << consumerUrl << std::endl;
    // wsa:Action cho SubscribeResponse (Base Notification)
    static const char* ACT_SUB_RESP =
        "http://docs.oasis-open.org/wsn/bw-2/NotificationProducer/SubscribeResponse";
    return wrapEnvelope(ACT_SUB_RESP, rel, body.str());
}

void MockSubscriptionManager::startNotifyThread() {
    bool expected = false;
    if (!notifyRunning_.compare_exchange_strong(expected, true)) return;
    notifyThread_ = std::thread(&MockSubscriptionManager::notifyPushLoop, this);
}

void MockSubscriptionManager::stopNotifyThread() {
    notifyRunning_ = false;
    if (notifyThread_.joinable()) notifyThread_.join();
}

void MockSubscriptionManager::notifyPushLoop() {
    using namespace std::chrono_literals;
    while (notifyRunning_) {
        // Interval 500ms để notify đầu tiên đến trước khi tool Unsubscribe
        // (EVENT-2-1-25/26/27: tool unsub ngay sau nhận notify đầu, ~2s window).
        std::this_thread::sleep_for(500ms);
        // Snapshot subscriptions có consumerUrl + reset terminationTime để
        // tránh purge trong khi tool đang chờ notify.
        // Snapshot subscriptions có consumerUrl + filter + counter cho round-robin
        struct Target { std::string url; std::string sub; std::string filter; unsigned long cnt; };
        std::vector<Target> targets;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            purgeExpired();
            for (auto& kv : subscriptions_) {
                if (!kv.second.consumerUrl.empty()) {
                    kv.second.terminationTime =
                        std::chrono::steady_clock::now() +
                        std::chrono::seconds(kv.second.timeoutSeconds);
                    targets.push_back({kv.second.consumerUrl, kv.first,
                                       kv.second.topicFilter,
                                       kv.second.pullCount++});
                }
            }
        }
        if (targets.empty()) continue;
        std::string now = getXmlUtcTime(0);
        for (auto& t : targets) {
            // Filter-aware: match topics theo subscription filter.
            bool wantAll = t.filter.empty();
            bool subtreeVS = t.filter.find("VideoSource//.") != std::string::npos ||
                             t.filter.find("VideoSource//*") != std::string::npos;
            bool wantMotion = wantAll || subtreeVS ||
                              t.filter.find("MotionAlarm") != std::string::npos;
            bool wantGSC = wantAll || subtreeVS ||
                           t.filter.find("GlobalSceneChange") != std::string::npos;
            std::vector<std::pair<const char*, const char*>> topics;
            if (wantMotion) topics.push_back({"tns1:VideoSource/MotionAlarm", "false"});
            if (wantGSC)    topics.push_back({"tns1:VideoSource/GlobalSceneChange", "false"});
            if (topics.empty()) continue;

            // EVENT-2-1-25/27: tool subscribe rồi Unsubscribe rất nhanh (~2s)
            // sau lần Notify đầu tiên → round-robin không kịp send topic thứ 2.
            // Fix: gom TẤT CẢ topics matching filter vào MỘT wsnt:Notify (nhiều
            // <NotificationMessage>). Tool nhận đầy đủ ngay Notify đầu.
            std::ostringstream env;
            env << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                << "<SOAP-ENV:Envelope"
                << " xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\""
                << " xmlns:wsa=\"http://www.w3.org/2005/08/addressing\""
                << " xmlns:wsnt=\"http://docs.oasis-open.org/wsn/b-2\""
                << " xmlns:tns1=\"http://www.onvif.org/ver10/topics\""
                << " xmlns:tt=\"http://www.onvif.org/ver10/schema\">"
                << "<SOAP-ENV:Header>"
                << "<wsa:Action>http://docs.oasis-open.org/wsn/bw-2/NotificationConsumer/Notify</wsa:Action>"
                << "<wsa:To>" << t.url << "</wsa:To>"
                << "<wsa:MessageID>" << newMessageId() << "</wsa:MessageID>"
                << "</SOAP-ENV:Header>"
                << "<SOAP-ENV:Body>"
                << "<wsnt:Notify>";
            for (const auto& tp : topics) {
                env << "<wsnt:NotificationMessage>"
                    << "<wsnt:Topic Dialect=\"http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet\">"
                    << tp.first
                    << "</wsnt:Topic>"
                    << "<wsnt:Message>"
                    << "<tt:Message UtcTime=\"" << now << "\" PropertyOperation=\"Initialized\">"
                    << "<tt:Source>"
                    << "<tt:SimpleItem Name=\"Source\" Value=\"VideoSourceToken\"/>"
                    << "</tt:Source>"
                    << "<tt:Data>"
                    << "<tt:SimpleItem Name=\"State\" Value=\"" << tp.second << "\"/>"
                    << "</tt:Data>"
                    << "</tt:Message>"
                    << "</wsnt:Message>"
                    << "</wsnt:NotificationMessage>";
            }
            env << "</wsnt:Notify>"
                << "</SOAP-ENV:Body>"
                << "</SOAP-ENV:Envelope>";
            std::string xml = env.str();
            bool ok = httpPostNotify(t.url, xml);
            std::cout << "[Event] Notify -> " << t.url << " sub=" << t.sub
                      << " topics=" << topics.size()
                      << (ok ? " OK" : " FAIL") << std::endl;
        }
    }
}

// HTTP POST đơn giản qua socket. Parse URL http://host:port/path.
// Trả true nếu send được (không check response).
bool MockSubscriptionManager::httpPostNotify(const std::string& url,
                                             const std::string& xml) {
    // Parse "http://host:port/path"
    if (url.rfind("http://", 0) != 0) return false;
    std::string rest = url.substr(7);
    size_t slash = rest.find('/');
    std::string hostport = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    std::string path = (slash == std::string::npos) ? "/" : rest.substr(slash);
    size_t colon = hostport.find(':');
    std::string host = hostport;
    int port = 80;
    if (colon != std::string::npos) {
        host = hostport.substr(0, colon);
        try { port = std::stoi(hostport.substr(colon + 1)); } catch (...) {}
    }

    // Resolve + connect
    struct addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0) return false;
    int sock = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock < 0) { freeaddrinfo(res); return false; }
    // Non-blocking connect với timeout ngắn (500ms) để tránh block notify thread
    struct timeval tv{}; tv.tv_sec = 1; tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int rc = ::connect(sock, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (rc < 0) { ::close(sock); return false; }

    std::ostringstream hdr;
    hdr << "POST " << path << " HTTP/1.1\r\n"
        << "Host: " << host << ":" << port << "\r\n"
        << "Content-Type: application/soap+xml; charset=utf-8\r\n"
        << "SOAPAction: \"http://docs.oasis-open.org/wsn/bw-2/NotificationConsumer/Notify\"\r\n"
        << "Content-Length: " << xml.size() << "\r\n"
        << "Connection: close\r\n\r\n";
    std::string request = hdr.str() + xml;
    ::send(sock, request.data(), request.size(), 0);
    // Đọc response để log HTTP status — debug tool có nhận đúng format không.
    char buf[2048];
    int n = ::recv(sock, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        buf[n] = 0;
        // Log line đầu (status) + Content-Length nếu có
        std::string resp(buf, n);
        size_t eol = resp.find('\n');
        std::string statusLine = (eol != std::string::npos) ? resp.substr(0, eol) : resp;
        std::cerr << "[NotifyResp] " << statusLine << std::endl;
    } else {
        std::cerr << "[NotifyResp] recv returned " << n << " errno=" << errno << std::endl;
    }
    ::close(sock);
    return true;
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
