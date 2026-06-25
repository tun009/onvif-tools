#pragma once
#include "interface/ipc/IpcProtocol.h"
#include "interface/ICameraBackend.h"
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>
#include <vector>

class IpcServer {
public:
    IpcServer(const std::string& ctrlSockPath,
              const std::string& evtSockPath,
              std::shared_ptr<ICameraBackend> backend);
    ~IpcServer();

    bool start();
    void stop();
    void pushEvent(ipc::MsgType type, const std::string& json);

private:
    void acceptLoop();
    void handleClient(int clientFd);
    void eventAcceptLoop();

    ipc::Message dispatch(const ipc::Message& req);

    ipc::Message handleGetDeviceInfo(const std::string& json);
    ipc::Message handleGetNetworkConfig(const std::string& json);
    ipc::Message handleGetDatetime(const std::string& json);
    ipc::Message handleSetDatetime(const std::string& json);
    ipc::Message handleReboot(const std::string& json);
    ipc::Message handleGetProfiles(const std::string& json);
    ipc::Message handleGetStreamUri(const std::string& json);
    ipc::Message handleSetVideoConfig(const std::string& json);
    ipc::Message handleGetSnapshotUri(const std::string& json);
    ipc::Message handlePtzAbsoluteMove(const std::string& json);
    ipc::Message handlePtzRelativeMove(const std::string& json);
    ipc::Message handlePtzContinuousMove(const std::string& json);
    ipc::Message handlePtzStop(const std::string& json);
    ipc::Message handlePtzGetStatus(const std::string& json);
    ipc::Message handleGetImagingSettings(const std::string& json);
    ipc::Message handleSetImagingSettings(const std::string& json);
    ipc::Message handleGetAnalyticsModules(const std::string& json);
    ipc::Message handleGetAnalyticsRules(const std::string& json);
    ipc::Message handleSubscribeEvents(const std::string& json);
    ipc::Message handleUnsubscribeEvents(const std::string& json);

    bool         sendMessage(int fd, const ipc::Message& msg);
    bool         recvMessage(int fd, ipc::Message& msg);
    ipc::Message makeOk(uint32_t reqId, const std::string& json);
    ipc::Message makeErr(uint32_t reqId, const std::string& msg);

    std::string                     ctrlSockPath_;
    std::string                     evtSockPath_;
    int                             ctrlFd_    = -1;
    int                             evtFd_     = -1;
    int                             evtClient_ = -1;

    std::shared_ptr<ICameraBackend> backend_;
    std::atomic<bool>               running_{false};
    std::thread                     acceptThread_;
    std::thread                     evtAcceptThread_;
    std::mutex                      evtClientMutex_;
};
