#pragma once
#include "interface/types/EventTypes.h"
#include "interface/ipc/IpcProtocol.h"
#include <string>
#include <map>
#include <thread>
#include <atomic>
#include <mutex>

class MockEventGenerator {
public:
    MockEventGenerator();
    ~MockEventGenerator();

    void start();
    void stop();

    void addSubscription(const std::string& id, EventCallback cb);
    void removeSubscription(const std::string& id);

private:
    void        generateLoop();
    void        pushMotionEvent(bool isMotion);
    void        pushObjectEvent();
    void        pushTamperEvent(bool isTampered);
    void        dispatch(ipc::MsgType type, const std::string& payload);
    std::string nowIso();

    std::atomic<bool>                          running_{false};
    std::thread                                thread_;
    std::mutex                                 mutex_;
    std::map<std::string, EventCallback>       subscriptions_;
};
