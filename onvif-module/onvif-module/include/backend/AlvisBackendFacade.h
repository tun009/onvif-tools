#pragma once

#include "backend/IMgmtClient.h"
#include "backend/IDvrClient.h"
#include "config/RuntimeConfig.h"
#include "interface/ICameraBackend.h"

// Compatibility facade during migration. Every operation is explicitly routed
// to its real owner or to mock; production never gets an implicit mock path.
class AlvisBackendFacade final : public ICameraBackend {
public:
    AlvisBackendFacade(CameraBackendPtr mockBackend, std::shared_ptr<IMgmtClient> mgmtClient,
                       std::shared_ptr<IDvrClient> dvrClient,
                       BackendMode mode, std::map<std::string, CapabilityMode> capabilities);
    DeviceInfo getDeviceInfo() override;
    NetworkConfig getNetworkConfig() override; bool setNetworkConfig(const NetworkConfig&) override;
    HostnameConfig getHostname() override; void setHostname(const HostnameConfig&) override;
    DnsConfig getDns() override; void setDns(const DnsConfig&) override;
    NetworkInterfaceConfig getNetworkInterface() override; void setNetworkInterface(const NetworkInterfaceConfig&) override;
    NetworkGatewayConfig getNetworkGateway() override; void setNetworkGateway(const NetworkGatewayConfig&) override;
    std::vector<NetworkProtocolEntry> getNetworkProtocols() override; void setNetworkProtocols(const std::vector<NetworkProtocolEntry>&) override;
    SystemDateTime getSystemDateAndTime() override; bool setSystemDateAndTime(const SystemDateTime&) override;
    bool reboot() override; bool factoryReset(bool) override;
    std::vector<StreamProfile> getProfiles() override;
    StreamUri getStreamUri(const std::string&, StreamProtocol) override;
    bool setVideoEncoderConfig(const std::string&, const VideoEncoderConfig&) override;
    SnapshotUri getSnapshotUri(const std::string&) override;
    bool ptzAbsoluteMove(const std::string&, const PTZVector&, const PTZVector&) override;
    bool ptzRelativeMove(const std::string&, const PTZVector&, const PTZVector&) override;
    bool ptzContinuousMove(const std::string&, const PTZVector&) override;
    bool ptzStop(const std::string&, bool, bool) override;
    PTZStatus getPtzStatus(const std::string&) override; bool gotoHomePosition(const std::string&) override; bool setHomePosition(const std::string&) override;
    ImagingSettings getImagingSettings(const std::string&) override; bool setImagingSettings(const std::string&, const ImagingSettings&) override; ImagingStatus getImagingStatus(const std::string&) override;
    std::vector<AnalyticsModule> getSupportedAnalyticsModules(const std::string&) override; std::vector<AnalyticsRule> getAnalyticsRules(const std::string&) override;
    bool addAnalyticsRule(const std::string&, const AnalyticsRule&) override; bool deleteAnalyticsRule(const std::string&, const std::string&) override;
    bool subscribe(const std::string&, const std::string&, EventCallback) override; bool unsubscribe(const std::string&) override; bool renewSubscription(const std::string&, int) override;
private:
    bool real(const std::string&) const; ICameraBackend& mock(const char*) const;
    CameraBackendPtr mockBackend_; std::shared_ptr<IMgmtClient> mgmtClient_; std::shared_ptr<IDvrClient> dvrClient_; BackendMode mode_; std::map<std::string, CapabilityMode> capabilities_;
};
