#pragma once
#include "interface/ICameraBackend.h"
#include "mock/MockPtzController.h"
#include "mock/MockEventGenerator.h"
#include <memory>
#include <string>
#include <map>

class MockCameraBackend final : public ICameraBackend {
public:
    explicit MockCameraBackend(const std::string& configPath);
    ~MockCameraBackend() override = default;

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
    StreamUri   getStreamUri(const std::string& profileToken,
                             StreamProtocol protocol)           override;
    bool        setVideoEncoderConfig(const std::string& token,
                                      const VideoEncoderConfig& cfg) override;
    SnapshotUri getSnapshotUri(const std::string& profileToken) override;

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
    std::vector<StreamProfile>                   buildProfiles() const;
    DeviceInfo                                   buildDeviceInfo() const;

    std::string                                  configPath_;
    std::unique_ptr<MockPtzController>           ptz_;
    std::unique_ptr<MockEventGenerator>          eventGen_;
    std::map<std::string, VideoEncoderConfig>    videoConfigs_;
    std::map<std::string, ImagingSettings>       imagingSettings_;
    std::map<std::string, AnalyticsRule>         analyticsRules_;
};
