#pragma once
#include <string>
#include <map>
#include <vector>
#include <chrono>

struct SubscriptionState {
    std::string id;
    std::chrono::steady_clock::time_point terminationTime;
    int timeoutSeconds;
};

class MockSubscriptionManager {
public:
    MockSubscriptionManager() = default;
    ~MockSubscriptionManager() = default;

    // Singleton instance
    static MockSubscriptionManager& getInstance();

    // Xử lý tạo subscription mới
    std::string handleCreateSubscription(const std::string& clientIp, int port, const std::string& rawRequest);

    // Xử lý kéo tin nhắn sự kiện
    std::string handlePullMessages(const std::string& subId, const std::string& rawRequest);

    // Xử lý gia hạn subscription
    std::string handleRenew(const std::string& subId, const std::string& rawRequest);

    // Xử lý hủy subscription
    std::string handleUnsubscribe(const std::string& subId, const std::string& rawRequest);

private:
    // Helper lấy thời gian UTC hiện tại định dạng XML (ví dụ: 2026-06-29T04:40:00Z)
    static std::string getXmlUtcTime(int offsetSeconds = 0);

    // Helper tạo Subscription ID ngẫu nhiên
    static std::string generateSubId();

    std::map<std::string, SubscriptionState> subscriptions_;
};
