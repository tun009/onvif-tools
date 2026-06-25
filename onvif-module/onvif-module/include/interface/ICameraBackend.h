#pragma once
#include "types/DeviceTypes.h"
#include "types/MediaTypes.h"
#include "types/PtzTypes.h"
#include "types/ImagingTypes.h"
#include "types/AnalyticsTypes.h"
#include "types/EventTypes.h"
#include <memory>
#include <vector>
#include <string>

class ICameraBackend {
public:
    virtual ~ICameraBackend() = default;

    // ── Device ───────────────────────────────────────────────────
    virtual DeviceInfo     getDeviceInfo()                              = 0;
    virtual NetworkConfig  getNetworkConfig()                           = 0;
    virtual bool           setNetworkConfig(const NetworkConfig& cfg)   = 0;
    virtual SystemDateTime getSystemDateAndTime()                       = 0;
    virtual bool           setSystemDateAndTime(const SystemDateTime&)  = 0;
    virtual bool           reboot()                                     = 0;
    virtual bool           factoryReset(bool hard)                      = 0;

    // ── Media2 ───────────────────────────────────────────────────
    virtual std::vector<StreamProfile> getProfiles()                    = 0;
    virtual StreamUri   getStreamUri(const std::string& profileToken,
                                     StreamProtocol protocol)           = 0;
    virtual bool        setVideoEncoderConfig(
                            const std::string& profileToken,
                            const VideoEncoderConfig& cfg)              = 0;
    virtual SnapshotUri getSnapshotUri(
                            const std::string& profileToken)            = 0;

    // ── PTZ ──────────────────────────────────────────────────────
    virtual bool      ptzAbsoluteMove(const std::string& profileToken,
                                      const PTZVector& pos,
                                      const PTZVector& speed)           = 0;
    virtual bool      ptzRelativeMove(const std::string& profileToken,
                                      const PTZVector& trans,
                                      const PTZVector& speed)           = 0;
    virtual bool      ptzContinuousMove(const std::string& profileToken,
                                        const PTZVector& velocity)      = 0;
    virtual bool      ptzStop(const std::string& profileToken,
                               bool panTilt, bool zoom)                 = 0;
    virtual PTZStatus getPtzStatus(const std::string& profileToken)     = 0;
    virtual bool      gotoHomePosition(const std::string& profileToken) = 0;
    virtual bool      setHomePosition(const std::string& profileToken)  = 0;

    // ── Imaging ──────────────────────────────────────────────────
    virtual ImagingSettings getImagingSettings(
                                const std::string& sourceToken)         = 0;
    virtual bool            setImagingSettings(
                                const std::string& sourceToken,
                                const ImagingSettings& settings)        = 0;
    virtual ImagingStatus   getImagingStatus(
                                const std::string& sourceToken)         = 0;

    // ── Analytics ────────────────────────────────────────────────
    virtual std::vector<AnalyticsModule> getSupportedAnalyticsModules(
                                const std::string& configToken)         = 0;
    virtual std::vector<AnalyticsRule>   getAnalyticsRules(
                                const std::string& configToken)         = 0;
    virtual bool addAnalyticsRule(const std::string& configToken,
                                  const AnalyticsRule& rule)            = 0;
    virtual bool deleteAnalyticsRule(const std::string& configToken,
                                     const std::string& ruleName)       = 0;

    // ── Events ───────────────────────────────────────────────────
    virtual bool subscribe(const std::string& subscriptionId,
                           const std::string& filter,
                           EventCallback callback)                      = 0;
    virtual bool unsubscribe(const std::string& subscriptionId)         = 0;
    virtual bool renewSubscription(const std::string& subscriptionId,
                                   int timeoutSeconds)                  = 0;
};

using CameraBackendPtr = std::shared_ptr<ICameraBackend>;
