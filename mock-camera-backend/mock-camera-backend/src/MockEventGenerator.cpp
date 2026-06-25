#include "mock/MockEventGenerator.h"
#include <chrono>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstdio>

MockEventGenerator::MockEventGenerator() {}

MockEventGenerator::~MockEventGenerator() {
    stop();
}

void MockEventGenerator::start() {
    running_ = true;
    thread_  = std::thread(&MockEventGenerator::generateLoop, this);
    printf("[EventGen] started\n");
}

void MockEventGenerator::stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
}

void MockEventGenerator::addSubscription(const std::string& id,
                                          EventCallback cb) {
    std::lock_guard<std::mutex> lk(mutex_);
    subscriptions_[id] = cb;
    if (!running_) start();
}

void MockEventGenerator::removeSubscription(const std::string& id) {
    std::lock_guard<std::mutex> lk(mutex_);
    subscriptions_.erase(id);
}

void MockEventGenerator::generateLoop() {
    int tick = 0;
    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        ++tick;
        if (tick % 10 == 0) pushMotionEvent(tick % 20 == 0);
        if (tick % 15 == 0) pushObjectEvent();
        if (tick % 30 == 0) pushTamperEvent(false);
    }
}

std::string MockEventGenerator::nowIso() {
    auto now = std::chrono::system_clock::now();
    auto tt  = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&tt), "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

void MockEventGenerator::pushMotionEvent(bool isMotion) {
    std::string payload =
        std::string(R"({"topic":"tns1:VideoAnalytics/MotionAlarm",)") +
        R"("sourceToken":"src_main","timestamp":")" + nowIso() + R"(",)" +
        R"("data":{"isMotion":)" +
        (isMotion ? "true" : "false") +
        R"(,"region":{"x":0.1,"y":0.2,"w":0.5,"h":0.4}}})";
    dispatch(ipc::MsgType::EVT_MOTION_DETECTED, payload);
    printf("[EventGen] motion=%s\n", isMotion ? "true" : "false");
}

void MockEventGenerator::pushObjectEvent() {
    std::string payload =
        std::string(R"({"topic":"tns1:VideoAnalytics/ObjectInField",)") +
        R"("sourceToken":"src_main","timestamp":")" + nowIso() + R"(",)" +
        R"("data":{"objectType":"Person","confidence":0.87,)" +
        R"("bbox":{"x":0.3,"y":0.1,"w":0.15,"h":0.4}}})";
    dispatch(ipc::MsgType::EVT_OBJECT_DETECTED, payload);
    printf("[EventGen] object detected\n");
}

void MockEventGenerator::pushTamperEvent(bool isTampered) {
    std::string payload =
        std::string(R"({"topic":"tns1:VideoSource/Tampering",)") +
        R"("sourceToken":"src_main","timestamp":")" + nowIso() + R"(",)" +
        R"("data":{"isTampered":)" +
        (isTampered ? "true" : "false") + "}}";
    dispatch(ipc::MsgType::EVT_TAMPER_DETECTED, payload);
    printf("[EventGen] tamper=%s\n", isTampered ? "true" : "false");
}

void MockEventGenerator::dispatch(ipc::MsgType type,
                                   const std::string& payload) {
    std::lock_guard<std::mutex> lk(mutex_);
    for (auto& [id, cb] : subscriptions_)
        if (cb) cb(type, payload);
}
