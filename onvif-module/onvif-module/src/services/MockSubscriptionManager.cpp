// MockSubscriptionManager.cpp — ONVIF Event Service (WS-BaseNotification), XML thủ công.
#include "services/MockSubscriptionManager.h"
#include <sstream>
#include <random>
#include <iostream>
#include <ctime>

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
    const std::string token = localName + ">";
    size_t p = xml.find(token);
    if (p == std::string::npos) return "";
    size_t start = p + token.size();
    size_t end = xml.find('<', start);
    if (end == std::string::npos) return "";
    std::string v = xml.substr(start, end - start);
    size_t a = v.find_first_not_of(" \t\r\n");
    size_t b = v.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    return v.substr(a, b - a + 1);
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

std::string MockSubscriptionManager::soapFault(const std::string& subcode,
                                               const std::string& reason) {
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
       << "<SOAP-ENV:Envelope"
       << " xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\""
       << " xmlns:ter=\"http://www.onvif.org/ver10/error\">"
       << "<SOAP-ENV:Body><SOAP-ENV:Fault>"
       << "<SOAP-ENV:Code><SOAP-ENV:Value>SOAP-ENV:Receiver</SOAP-ENV:Value>"
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
        "<wstop:TopicSet>"
          "<tns1:VideoSource>"
            "<MotionAlarm wstop:topic=\"true\">"
              "<tt:MessageDescription IsProperty=\"true\">"
                "<tt:Source><tt:SimpleItemDescription Name=\"Source\" Type=\"tt:ReferenceToken\"/></tt:Source>"
                "<tt:Data><tt:SimpleItemDescription Name=\"State\" Type=\"xs:boolean\"/></tt:Data>"
              "</tt:MessageDescription>"
            "</MotionAlarm>"
          "</tns1:VideoSource>"
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
    int timeout = 60;

    {
        std::lock_guard<std::mutex> lk(mtx_);
        SubscriptionState st;
        st.id = subId;
        st.timeoutSeconds = timeout;
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
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = subscriptions_.find(subId);
        if (it == subscriptions_.end())
            return soapFault("ter:InvalidArgVal",
                             "Subscription not found: " + subId);
        it->second.terminationTime =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(it->second.timeoutSeconds);
    }

    std::string now = getXmlUtcTime(0);
    std::ostringstream body;
    body << "<tev:PullMessagesResponse>"
         << "<tev:CurrentTime>" << now << "</tev:CurrentTime>"
         << "<tev:TerminationTime>" << getXmlUtcTime(60) << "</tev:TerminationTime>"
         // Property event: VideoSource/MotionAlarm (State=false, Initialized)
         << "<wsnt:NotificationMessage>"
         << "<wsnt:Topic Dialect=\"" << TOPIC_DIALECT << "\">"
         << "tns1:VideoSource/MotionAlarm</wsnt:Topic>"
         << "<wsnt:Message>"
         << "<tt:Message UtcTime=\"" << now << "\" PropertyOperation=\"Initialized\">"
         << "<tt:Source>"
         << "<tt:SimpleItem Name=\"Source\" Value=\"VideoSourceToken\"/>"
         << "</tt:Source>"
         << "<tt:Data>"
         << "<tt:SimpleItem Name=\"State\" Value=\"false\"/>"
         << "</tt:Data>"
         << "</tt:Message>"
         << "</wsnt:Message>"
         << "</wsnt:NotificationMessage>"
         << "</tev:PullMessagesResponse>";
    return wrapEnvelope(ACT_PULL, rel, body.str());
}

// ── Renew (WS-BaseNotification SubscriptionManager) ─────────────────────────
std::string MockSubscriptionManager::handleRenew(const std::string& subId,
                                                 const std::string& req) {
    std::string rel = extractTag(req, "MessageID");
    {
        std::lock_guard<std::mutex> lk(mtx_);
        auto it = subscriptions_.find(subId);
        if (it == subscriptions_.end())
            return soapFault("wsnt:UnableToDestroySubscriptionFault",
                             "Subscription not found: " + subId);
        it->second.terminationTime =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(it->second.timeoutSeconds);
    }
    std::ostringstream body;
    body << "<wsnt:RenewResponse>"
         << "<wsnt:TerminationTime>" << getXmlUtcTime(60) << "</wsnt:TerminationTime>"
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
        auto it = subscriptions_.find(subId);
        if (it == subscriptions_.end())
            return soapFault("wsnt:UnableToDestroySubscriptionFault",
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
