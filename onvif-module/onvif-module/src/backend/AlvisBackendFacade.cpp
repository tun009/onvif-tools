#include "backend/AlvisBackendFacade.h"
#include <stdexcept>
#include <utility>

AlvisBackendFacade::AlvisBackendFacade(CameraBackendPtr mockBackend, std::shared_ptr<IMgmtClient> mgmtClient, std::shared_ptr<IDvrClient> dvrClient, BackendMode mode, std::map<std::string, CapabilityMode> capabilities)
    : mockBackend_(std::move(mockBackend)), mgmtClient_(std::move(mgmtClient)), dvrClient_(std::move(dvrClient)), mode_(mode), capabilities_(std::move(capabilities)) {}
bool AlvisBackendFacade::real(const std::string &capability) const
{
    const auto it = capabilities_.find(capability);
    return mode_ != BackendMode::Mock && it != capabilities_.end() && it->second == CapabilityMode::Real;
}
ICameraBackend &AlvisBackendFacade::mock(const char *operation) const
{
    if (mockBackend_)
        return *mockBackend_;
    throw std::runtime_error(std::string(operation) + " is not migrated and mock is disabled");
}
DeviceInfo AlvisBackendFacade::getDeviceInfo()
{
    if (real("device"))
    {
        if (!mgmtClient_)
            throw std::runtime_error("MGMT client unavailable");
        return mgmtClient_->getDeviceInformation();
    }
    return mock("GetDeviceInformation").getDeviceInfo();
}
NetworkConfig AlvisBackendFacade::getNetworkConfig() { return mock("GetNetworkConfig").getNetworkConfig(); }
bool AlvisBackendFacade::setNetworkConfig(const NetworkConfig &v) { return mock("SetNetworkConfig").setNetworkConfig(v); }
HostnameConfig AlvisBackendFacade::getHostname()
{
    if (real("network"))
    {
        if (!mgmtClient_)
            throw std::runtime_error("MGMT client unavailable");
        return mgmtClient_->getHostname();
    }
    return mock("GetHostname").getHostname();
}
void AlvisBackendFacade::setHostname(const HostnameConfig &v)
{
    if (real("network"))
    {
        if (!mgmtClient_)
            throw std::runtime_error("MGMT client unavailable");
        mgmtClient_->setHostname(v);
        return;
    }
    mock("SetHostname").setHostname(v);
}
DnsConfig AlvisBackendFacade::getDns()
{
    if (real("network"))
    {
        if (!mgmtClient_)
            throw std::runtime_error("MGMT client unavailable");
        return mgmtClient_->getDns();
    }
    return mock("GetDNS").getDns();
}
void AlvisBackendFacade::setDns(const DnsConfig &v)
{
    if (real("network"))
    {
        if (!mgmtClient_)
            throw std::runtime_error("MGMT client unavailable");
        mgmtClient_->setDns(v);
        return;
    }
    mock("SetDNS").setDns(v);
}
NetworkInterfaceConfig AlvisBackendFacade::getNetworkInterface()
{
    if (real("network"))
    {
        if (!mgmtClient_)
            throw std::runtime_error("MGMT client unavailable");
        return mgmtClient_->getNetworkInterface();
    }
    return mock("GetNetworkInterfaces").getNetworkInterface();
}
void AlvisBackendFacade::setNetworkInterface(const NetworkInterfaceConfig &v)
{
    if (real("network"))
    {
        if (!mgmtClient_)
            throw std::runtime_error("MGMT client unavailable");
        mgmtClient_->setNetworkInterface(v);
        return;
    }
    mock("SetNetworkInterfaces").setNetworkInterface(v);
}
NetworkGatewayConfig AlvisBackendFacade::getNetworkGateway()
{
    if (real("network"))
    {
        if (!mgmtClient_)
            throw std::runtime_error("MGMT client unavailable");
        return mgmtClient_->getNetworkGateway();
    }
    return mock("GetNetworkDefaultGateway").getNetworkGateway();
}
void AlvisBackendFacade::setNetworkGateway(const NetworkGatewayConfig &v)
{
    if (real("network"))
    {
        if (!mgmtClient_)
            throw std::runtime_error("MGMT client unavailable");
        mgmtClient_->setNetworkGateway(v);
        return;
    }
    mock("SetNetworkDefaultGateway").setNetworkGateway(v);
}
std::vector<NetworkProtocolEntry> AlvisBackendFacade::getNetworkProtocols()
{
    if (real("network"))
    {
        if (!mgmtClient_)
            throw std::runtime_error("MGMT client unavailable");
        return mgmtClient_->getNetworkProtocols();
    }
    return mock("GetNetworkProtocols").getNetworkProtocols();
}
void AlvisBackendFacade::setNetworkProtocols(const std::vector<NetworkProtocolEntry> &v)
{
    if (real("network"))
    {
        if (!mgmtClient_)
            throw std::runtime_error("MGMT client unavailable");
        mgmtClient_->setNetworkProtocols(v);
        return;
    }
    mock("SetNetworkProtocols").setNetworkProtocols(v);
}
SystemDateTime AlvisBackendFacade::getSystemDateAndTime()
{
    if (!mgmtClient_)
        throw std::runtime_error("MGMT client unavailable");
    return mgmtClient_->getSystemDateAndTime();
}
bool AlvisBackendFacade::setSystemDateAndTime(const SystemDateTime &v)
{
    if (!mgmtClient_)
        throw std::runtime_error("MGMT client unavailable");
    mgmtClient_->setSystemDateAndTime(v);
    return true;
}
bool AlvisBackendFacade::reboot() { return mock("SystemReboot").reboot(); }
bool AlvisBackendFacade::factoryReset(bool v) { return mock("SetSystemFactoryDefault").factoryReset(v); }
std::vector<StreamProfile> AlvisBackendFacade::getProfiles()
{
    if (real("media"))
    {
        if (!dvrClient_)
            throw std::runtime_error("DVR client unavailable");
        return dvrClient_->getProfiles();
    }
    return mock("GetProfiles").getProfiles();
}
StreamUri AlvisBackendFacade::getStreamUri(const std::string &t, StreamProtocol p)
{
    if (real("media"))
    {
        if (!dvrClient_)
            throw std::runtime_error("DVR client unavailable");
        return dvrClient_->getStreamUri(t, p);
    }
    return mock("GetStreamUri").getStreamUri(t, p);
}
bool AlvisBackendFacade::setVideoEncoderConfig(const std::string &t, const VideoEncoderConfig &v) { return mock("SetVideoEncoderConfiguration").setVideoEncoderConfig(t, v); }
SnapshotUri AlvisBackendFacade::getSnapshotUri(const std::string &t)
{
    if (real("media"))
    {
        if (!dvrClient_)
            throw std::runtime_error("DVR client unavailable");
        return dvrClient_->getSnapshotUri(t);
    }
    return mock("GetSnapshotUri").getSnapshotUri(t);
}
bool AlvisBackendFacade::ptzAbsoluteMove(const std::string &t, const PTZVector &p, const PTZVector &s) { return mock("AbsoluteMove").ptzAbsoluteMove(t, p, s); }
bool AlvisBackendFacade::ptzRelativeMove(const std::string &t, const PTZVector &p, const PTZVector &s) { return mock("RelativeMove").ptzRelativeMove(t, p, s); }
bool AlvisBackendFacade::ptzContinuousMove(const std::string &t, const PTZVector &v) { return mock("ContinuousMove").ptzContinuousMove(t, v); }
bool AlvisBackendFacade::ptzStop(const std::string &t, bool p, bool z) { return mock("Stop").ptzStop(t, p, z); }
PTZStatus AlvisBackendFacade::getPtzStatus(const std::string &t) { return mock("GetStatus").getPtzStatus(t); }
bool AlvisBackendFacade::gotoHomePosition(const std::string &t) { return mock("GotoHomePosition").gotoHomePosition(t); }
bool AlvisBackendFacade::setHomePosition(const std::string &t) { return mock("SetHomePosition").setHomePosition(t); }
ImagingSettings AlvisBackendFacade::getImagingSettings(const std::string &t) { return mock("GetImagingSettings").getImagingSettings(t); }
bool AlvisBackendFacade::setImagingSettings(const std::string &t, const ImagingSettings &v) { return mock("SetImagingSettings").setImagingSettings(t, v); }
ImagingStatus AlvisBackendFacade::getImagingStatus(const std::string &t) { return mock("GetImagingStatus").getImagingStatus(t); }
std::vector<AnalyticsModule> AlvisBackendFacade::getSupportedAnalyticsModules(const std::string &t) { return mock("GetSupportedAnalyticsModules").getSupportedAnalyticsModules(t); }
std::vector<AnalyticsRule> AlvisBackendFacade::getAnalyticsRules(const std::string &t) { return mock("GetRules").getAnalyticsRules(t); }
bool AlvisBackendFacade::addAnalyticsRule(const std::string &t, const AnalyticsRule &r) { return mock("CreateRules").addAnalyticsRule(t, r); }
bool AlvisBackendFacade::deleteAnalyticsRule(const std::string &t, const std::string &n) { return mock("DeleteRules").deleteAnalyticsRule(t, n); }
bool AlvisBackendFacade::subscribe(const std::string &i, const std::string &f, EventCallback c) { return mock("Subscribe").subscribe(i, f, std::move(c)); }
bool AlvisBackendFacade::unsubscribe(const std::string &i) { return mock("Unsubscribe").unsubscribe(i); }
bool AlvisBackendFacade::renewSubscription(const std::string &i, int s) { return mock("Renew").renewSubscription(i, s); }
