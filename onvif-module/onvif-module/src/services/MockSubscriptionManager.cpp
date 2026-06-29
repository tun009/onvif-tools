#include "services/MockSubscriptionManager.h"
#include <sstream>
#include <iomanip>
#include <random>
#include <iostream>
#include <ctime>

MockSubscriptionManager& MockSubscriptionManager::getInstance() {
    static MockSubscriptionManager instance;
    return instance;
}

std::string MockSubscriptionManager::getXmlUtcTime(int offsetSeconds) {
    std::time_t t = std::time(nullptr) + offsetSeconds;
    std::tm* tm_utc = std::gmtime(&t);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", tm_utc);
    return std::string(buf);
}

std::string MockSubscriptionManager::generateSubId() {
    static std::random_device rd;
    static std::mt19937 generator(rd());
    std::uniform_int_distribution<uint32_t> distribution(100000, 999999);
    return "sub_" + std::to_string(distribution(generator));
}

std::string MockSubscriptionManager::handleCreateSubscription(
    const std::string& clientIp, int port, const std::string& rawRequest) 
{
    std::string subId = generateSubId();
    int timeout = 60; // Mặc định 60 giây
    
    // Parse InitialTerminationTime từ request nếu có (đơn giản hóa bằng regex hoặc tìm kiếm chuỗi)
    size_t pos = rawRequest.find("InitialTerminationTime");
    if (pos != std::string::npos) {
        // Có thể parse giá trị nếu cần, ở đây ta đặt mặc định là 60s
    }

    SubscriptionState state;
    state.id = subId;
    state.timeoutSeconds = timeout;
    state.terminationTime = std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
    subscriptions_[subId] = state;

    std::string currentTime = getXmlUtcTime(0);
    std::string terminationTime = getXmlUtcTime(timeout);
    std::string xaddr = "http://" + clientIp + ":" + std::to_string(port) + "/onvif/event/" + subId;

    std::ostringstream oss;
    oss << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        << "<soap:Envelope xmlns:soap=\"http://www.w3.org/2003/05/soap-envelope\" \n"
        << "               xmlns:wsa5=\"http://www.w3.org/2005/08/addressing\" \n"
        << "               xmlns:tev=\"http://www.onvif.org/ver10/events/wsdl\">\n"
        << "  <soap:Header>\n"
        << "    <wsa5:Action>http://www.onvif.org/ver10/events/wsdl/EventPortType/CreatePullPointSubscriptionResponse</wsa5:Action>\n"
        << "  </soap:Header>\n"
        << "  <soap:Body>\n"
        << "    <tev:CreatePullPointSubscriptionResponse>\n"
        << "      <tev:SubscriptionReference>\n"
        << "        <wsa5:Address>" << xaddr << "</wsa5:Address>\n"
        << "      </tev:SubscriptionReference>\n"
        << "      <tev:CurrentTime>" << currentTime << "</tev:CurrentTime>\n"
        << "      <tev:TerminationTime>" << terminationTime << "</tev:TerminationTime>\n"
        << "    </tev:CreatePullPointSubscriptionResponse>\n"
        << "  </soap:Body>\n"
        << "</soap:Envelope>";

    std::cout << "[MockEvent] Created subscription: " << subId << " -> xaddr: " << xaddr << std::endl;
    return oss.str();
}

std::string MockSubscriptionManager::handlePullMessages(
    const std::string& subId, const std::string& rawRequest) 
{
    // Kiểm tra xem subscription còn tồn tại không
    auto it = subscriptions_.find(subId);
    if (it == subscriptions_.end()) {
        // Trả về SOAP Fault nếu không tìm thấy
        std::ostringstream fault;
        fault << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
              << "<soap:Envelope xmlns:soap=\"http://www.w3.org/2003/05/soap-envelope\" \n"
              << "               xmlns:ter=\"http://www.onvif.org/ver10/error\">\n"
              << "  <soap:Body>\n"
              << "    <soap:Fault>\n"
              << "      <soap:Code><soap:Value>soap:Receiver</soap:Value></soap:Code>\n"
              << "      <soap:Reason><soap:Text xml:lang=\"en\">Subscription not found</soap:Text></soap:Reason>\n"
              << "    </soap:Fault>\n"
              << "  </soap:Body>\n"
              << "</soap:Envelope>";
        return fault.str();
    }

    // Cập nhật lại thời gian hết hạn của subscription
    it->second.terminationTime = std::chrono::steady_clock::now() + std::chrono::seconds(it->second.timeoutSeconds);

    std::string currentTime = getXmlUtcTime(0);
    std::string terminationTime = getXmlUtcTime(it->second.timeoutSeconds);

    // Sinh ra các sự kiện giả lập để pass Test Tool:
    // Trả về 2 tin nhắn sự kiện: MotionAlarm = false, TamperAlarm = false
    std::ostringstream oss;
    oss << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        << "<soap:Envelope xmlns:soap=\"http://www.w3.org/2003/05/soap-envelope\" \n"
        << "               xmlns:wsa5=\"http://www.w3.org/2005/08/addressing\" \n"
        << "               xmlns:tev=\"http://www.onvif.org/ver10/events/wsdl\" \n"
        << "               xmlns:tt=\"http://www.onvif.org/ver10/schema\">\n"
        << "  <soap:Header>\n"
        << "    <wsa5:Action>http://www.onvif.org/ver10/events/wsdl/PullPointSubscription/PullMessagesResponse</wsa5:Action>\n"
        << "  </soap:Header>\n"
        << "  <soap:Body>\n"
        << "    <tev:PullMessagesResponse>\n"
        << "      <tev:CurrentTime>" << currentTime << "</tev:CurrentTime>\n"
        << "      <tev:TerminationTime>" << terminationTime << "</tev:TerminationTime>\n"
        
        // 1. MotionAlarm Event Message
        << "      <tev:NotificationMessage>\n"
        << "        <tev:Topic Dialect=\"http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet\">\n"
        << "          tns1:VideoSource/MotionAlarm\n"
        << "        </tev:Topic>\n"
        << "        <tev:ProducerReference>\n"
        << "          <wsa5:Address>http://127.0.0.1:8080/onvif/event</wsa5:Address>\n"
        << "        </tev:ProducerReference>\n"
        << "        <tev:Message>\n"
        << "          <tt:Message UtcTime=\"" << currentTime << "\" PropertyOperation=\"Initialized\">\n"
        << "            <tt:Source>\n"
        << "              <tt:SimpleItem Name=\"VideoSourceConfigurationToken\" Value=\"video_source_token\"/>\n"
        << "            </tt:Source>\n"
        << "            <tt:Data>\n"
        << "              <tt:SimpleItem Name=\"State\" Value=\"false\"/>\n"
        << "            </tt:Data>\n"
        << "          </tt:Message>\n"
        << "        </tev:Message>\n"
        << "      </tev:NotificationMessage>\n"

        // 2. TamperAlarm Event Message
        << "      <tev:NotificationMessage>\n"
        << "        <tev:Topic Dialect=\"http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet\">\n"
        << "          tns1:VideoSource/TamperAlarm\n"
        << "        </tev:Topic>\n"
        << "        <tev:ProducerReference>\n"
        << "          <wsa5:Address>http://127.0.0.1:8080/onvif/event</wsa5:Address>\n"
        << "        </tev:ProducerReference>\n"
        << "        <tev:Message>\n"
        << "          <tt:Message UtcTime=\"" << currentTime << "\" PropertyOperation=\"Initialized\">\n"
        << "            <tt:Source>\n"
        << "              <tt:SimpleItem Name=\"VideoSourceConfigurationToken\" Value=\"video_source_token\"/>\n"
        << "            </tt:Source>\n"
        << "            <tt:Data>\n"
        << "              <tt:SimpleItem Name=\"State\" Value=\"false\"/>\n"
        << "            </tt:Data>\n"
        << "          </tt:Message>\n"
        << "        </tev:Message>\n"
        << "      </tev:NotificationMessage>\n"

        << "    </tev:PullMessagesResponse>\n"
        << "  </soap:Body>\n"
        << "</soap:Envelope>";

    std::cout << "[MockEvent] Pulled messages for subscription: " << subId << std::endl;
    return oss.str();
}

std::string MockSubscriptionManager::handleRenew(
    const std::string& subId, const std::string& rawRequest) 
{
    auto it = subscriptions_.find(subId);
    if (it == subscriptions_.end()) {
        std::ostringstream fault;
        fault << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
              << "<soap:Envelope xmlns:soap=\"http://www.w3.org/2003/05/soap-envelope\">\n"
              << "  <soap:Body>\n"
              << "    <soap:Fault>\n"
              << "      <soap:Code><soap:Value>soap:Receiver</soap:Value></soap:Code>\n"
              << "      <soap:Reason><soap:Text>Subscription not found</soap:Text></soap:Reason>\n"
              << "    </soap:Fault>\n"
              << "  </soap:Body>\n"
              << "</soap:Envelope>";
        return fault.str();
    }

    int timeout = 60;
    it->second.terminationTime = std::chrono::steady_clock::now() + std::chrono::seconds(timeout);

    std::string currentTime = getXmlUtcTime(0);
    std::string terminationTime = getXmlUtcTime(timeout);

    std::ostringstream oss;
    oss << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        << "<soap:Envelope xmlns:soap=\"http://www.w3.org/2003/05/soap-envelope\" \n"
        << "               xmlns:tev=\"http://www.onvif.org/ver10/events/wsdl\">\n"
        << "  <soap:Body>\n"
        << "    <tev:RenewResponse>\n"
        << "      <tev:CurrentTime>" << currentTime << "</tev:CurrentTime>\n"
        << "      <tev:TerminationTime>" << terminationTime << "</tev:TerminationTime>\n"
        << "    </tev:RenewResponse>\n"
        << "  </soap:Body>\n"
        << "</soap:Envelope>";

    std::cout << "[MockEvent] Renewed subscription: " << subId << std::endl;
    return oss.str();
}

std::string MockSubscriptionManager::handleUnsubscribe(
    const std::string& subId, const std::string& rawRequest) 
{
    subscriptions_.erase(subId);
    
    std::ostringstream oss;
    oss << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
        << "<soap:Envelope xmlns:soap=\"http://www.w3.org/2003/05/soap-envelope\" \n"
        << "               xmlns:tev=\"http://www.onvif.org/ver10/events/wsdl\">\n"
        << "  <soap:Body>\n"
        << "    <tev:UnsubscribeResponse/>\n"
        << "  </soap:Body>\n"
        << "</soap:Envelope>";

    std::cout << "[MockEvent] Unsubscribed: " << subId << std::endl;
    return oss.str();
}
