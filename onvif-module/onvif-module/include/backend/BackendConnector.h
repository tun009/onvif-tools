#pragma once
#include "interface/ICameraBackend.h"
#include "interface/ipc/IpcProtocol.h"
#include <string>
#include <atomic>
#include <thread>
#include <mutex>
#include <map>

class BackendConnector final : public ICameraBackend {
public:
    BackendConnector(const std::string& ctrlSockPath,
                     const std::string& evtSockPath);
    ~BackendConnector() override;

    bool connect();
    void disconnect();
    bool isConnected() const { return connected_; }

    // Device
    DeviceInfo     getDeviceInfo()                              override;
    NetworkConfig  getNetworkConfig()                           override;
    bool           setNetworkConfig(const NetworkConfig& cfg)   override;
    SystemDateTime getSystemDateAndTime()                       override;
    bool           setSystemDateAndTime(const SystemDateTime&)  override;
    bool           reboot()                                     override;
    bool           factoryReset(bool hard)                      override;

    // Media2
    std::vector<StreamProfile> getProfiles()                    override;
    StreamUri   getStreamUri(const std::string& token,
                             StreamProtocol proto)              override;
    bool        setVideoEncoderConfig(const std::string& token,
                                      const VideoEncoderConfig& cfg) override;
    SnapshotUri getSnapshotUri(const std::string& token)        override;

    // PTZ
    bool      ptzAbsoluteMove(const std::string& token,
                               const PTZVector& pos,
                               const PTZVector& speed)          override;
    bool      ptzRelativeMove(const std::string& token,
                               const PTZVector& trans,
                               const PTZVector& speed)          override;
    bool      ptzContinuousMove(const std::string& token,
                                 const PTZVector& vel)          override;
    bool      ptzStop(const std::string& token,
                       bool panTilt, bool zoom)                 override;
    PTZStatus getPtzStatus(const std::string& token)            override;
    bool      gotoHomePosition(const std::string& token)        override;
    bool      setHomePosition(const std::string& token)         override;

    // Imaging
    ImagingSettings getImagingSettings(const std::string& src)  override;
    bool            setImagingSettings(const std::string& src,
                                       const ImagingSettings& s)override;
    ImagingStatus   getImagingStatus(const std::string& src)    override;

    // Analytics
    std::vector<AnalyticsModule> getSupportedAnalyticsModules(
                                 const std::string& token)      override;
    std::vector<AnalyticsRule>   getAnalyticsRules(
                                 const std::string& token)      override;
    bool addAnalyticsRule(const std::string& token,
                          const AnalyticsRule& rule)            override;
    bool deleteAnalyticsRule(const std::string& token,
                             const std::string& name)           override;

    // Events
    bool subscribe(const std::string& id,
                   const std::string& filter,
                   EventCallback cb)                            override;
    bool unsubscribe(const std::string& id)                     override;
    bool renewSubscription(const std::string& id, int sec)      override;

private:
    ipc::Message sendRequest(ipc::MsgType type, const std::string& json);
    bool         sendMessage(int fd, const ipc::Message& msg);
    bool         recvMessage(int fd, ipc::Message& msg);
    void         eventListenerLoop();
    uint32_t     nextId() {
        return reqId_.fetch_add(1, std::memory_order_relaxed);
    }

    std::string           ctrlPath_;
    std::string           evtPath_;
    int                   ctrlFd_   = -1;
    int                   evtFd_    = -1;
    std::atomic<bool>     connected_{false};
    std::atomic<uint32_t> reqId_{1};

    std::thread           evtThread_;
    std::atomic<bool>     evtRunning_{false};

    std::mutex                               cbMutex_;
    std::map<std::string, EventCallback>     callbacks_;
};
