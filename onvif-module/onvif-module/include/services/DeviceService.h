#pragma once
#include "soapDeviceBindingService.h"
#include "interface/ICameraBackend.h"
#include <memory>
#include <string>
#include <vector>
#include <mutex>

struct ServiceConfig {
    std::string deviceIp;
    int         httpPort   = 8080;
    int         rtspPort   = 8554;
    std::string deviceUuid;
    std::string username;
    std::string password;
};

class DeviceService : public DeviceBindingService {
public:
    DeviceService(struct soap* soap, const ServiceConfig& cfg, std::shared_ptr<ICameraBackend> backend);
    ~DeviceService() = default;

    virtual DeviceBindingService* copy() override;

    // ONVIF Device service operations
    virtual int GetSystemDateAndTime(_tds__GetSystemDateAndTime *tds__GetSystemDateAndTime, _tds__GetSystemDateAndTimeResponse &tds__GetSystemDateAndTimeResponse) override;
    virtual int GetDeviceInformation(_tds__GetDeviceInformation *tds__GetDeviceInformation, _tds__GetDeviceInformationResponse &tds__GetDeviceInformationResponse) override;
    virtual int GetCapabilities(_tds__GetCapabilities *tds__GetCapabilities, _tds__GetCapabilitiesResponse &tds__GetCapabilitiesResponse) override;
    virtual int GetServices(_tds__GetServices *tds__GetServices, _tds__GetServicesResponse &tds__GetServicesResponse) override;
    virtual int GetScopes(_tds__GetScopes *tds__GetScopes, _tds__GetScopesResponse &tds__GetScopesResponse) override;
    virtual int GetUsers(_tds__GetUsers *tds__GetUsers, _tds__GetUsersResponse &tds__GetUsersResponse) override;
    virtual int GetServiceCapabilities(_tds__GetServiceCapabilities *tds__GetServiceCapabilities, _tds__GetServiceCapabilitiesResponse &tds__GetServiceCapabilitiesResponse) override;

    // ── Network config (Profile T mandatory 7.4) ─────────────────────────
    virtual int GetHostname(_tds__GetHostname *req, _tds__GetHostnameResponse &resp) override;
    virtual int SetHostname(_tds__SetHostname *req, _tds__SetHostnameResponse &resp) override;
    virtual int GetDNS(_tds__GetDNS *req, _tds__GetDNSResponse &resp) override;
    virtual int SetDNS(_tds__SetDNS *req, _tds__SetDNSResponse &resp) override;
    virtual int GetNetworkInterfaces(_tds__GetNetworkInterfaces *req, _tds__GetNetworkInterfacesResponse &resp) override;
    virtual int SetNetworkInterfaces(_tds__SetNetworkInterfaces *req, _tds__SetNetworkInterfacesResponse &resp) override;
    virtual int GetNetworkDefaultGateway(_tds__GetNetworkDefaultGateway *req, _tds__GetNetworkDefaultGatewayResponse &resp) override;
    virtual int SetNetworkDefaultGateway(_tds__SetNetworkDefaultGateway *req, _tds__SetNetworkDefaultGatewayResponse &resp) override;
    virtual int GetNetworkProtocols(_tds__GetNetworkProtocols *req, _tds__GetNetworkProtocolsResponse &resp) override;
    virtual int SetNetworkProtocols(_tds__SetNetworkProtocols *req, _tds__SetNetworkProtocolsResponse &resp) override;

    // ── System (Profile T mandatory 7.5) ─────────────────────────────────
    virtual int SetSystemDateAndTime(_tds__SetSystemDateAndTime *req, _tds__SetSystemDateAndTimeResponse &resp) override;
    virtual int SetSystemFactoryDefault(_tds__SetSystemFactoryDefault *req, _tds__SetSystemFactoryDefaultResponse &resp) override;
    virtual int SystemReboot(_tds__SystemReboot *req, _tds__SystemRebootResponse &resp) override;

    // ── User handling (Profile T mandatory 7.6) ──────────────────────────
    virtual int CreateUsers(_tds__CreateUsers *req, _tds__CreateUsersResponse &resp) override;
    virtual int DeleteUsers(_tds__DeleteUsers *req, _tds__DeleteUsersResponse &resp) override;
    virtual int SetUser(_tds__SetUser *req, _tds__SetUserResponse &resp) override;

    // ── Profile T mandatory misc ─────────────────────────────────────────
    virtual int GetWsdlUrl(_tds__GetWsdlUrl *req, _tds__GetWsdlUrlResponse &resp) override;

private:
    bool validateAuth();

    ServiceConfig cfg_;
    std::shared_ptr<ICameraBackend> backend_;

    // Network state (static, persist across requests trong 1 phiên process).
    struct NetworkState {
        std::string hostname = "MockCam-4K";
        bool hostnameFromDHCP = false;
        bool dnsFromDHCP = false;
        std::vector<std::string> searchDomain = {"local"};
        std::vector<std::string> dnsManual   = {"8.8.8.8", "8.8.4.4"};

        std::string ifaceToken = "eth0";
        bool        ifaceEnabled = true;
        std::string ifaceName    = "eth0";
        std::string hwAddress    = "00:11:22:33:44:55";
        int         mtu          = 1500;

        // IPv4 config
        bool        ipv4DhcpEnabled = false;
        std::string ipv4Manual      = "192.168.254.119";
        int         prefixLength    = 24;

        std::vector<std::string> gatewayIPv4 = {"192.168.254.1"};

        struct Protocol { int type; bool enabled; int port; };
        std::vector<Protocol> protocols = {
            {0, true, 80},     // HTTP
            {1, false, 443},   // HTTPS
            {2, true, 554},    // RTSP
        };
    };
    static std::mutex netMtx_;
    static NetworkState net_;

    // System + User state (Profile T 7.5 & 7.6)
    struct MockUser {
        std::string username;
        std::string password;
        int level = 0;   // 0=Admin, 1=Operator, 2=User, 3=Anonymous, 4=Extended
    };
    struct SystemState {
        // Time
        bool ntpEnabled = false;
        int  tzOffsetMin = 0;
        // Users
        std::vector<MockUser> users = {{"admin", "admin123", 0}};
    };
    static std::mutex sysMtx_;
    static SystemState sys_;
};
