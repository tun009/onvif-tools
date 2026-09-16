#pragma once
#include <string>
#include <vector>

struct DeviceInfo {
    std::string manufacturer;
    std::string model;
    std::string firmwareVersion;
    std::string serialNumber;
    std::string hardwareId;
};

struct NetworkConfig {
    std::string ipAddress;
    std::string subnetMask;
    std::string gateway;
    std::string macAddress;
    bool        dhcp       = false;
    int         httpPort   = 8080;
    int         rtspPort   = 8554;
};

struct SystemDateTime {
    std::string dateTimeType = "MANUAL";
    std::string timezone     = "UTC";
    int  year           = 2024;
    int  month          = 1;
    int  day            = 1;
    int  hour           = 0;
    int  minute         = 0;
    int  second         = 0;
    int  utcOffset      = 0;
    bool daylightSaving = false;
    int  localYear      = 2024;
    int  localMonth     = 1;
    int  localDay       = 1;
    int  localHour      = 0;
    int  localMinute    = 0;
    int  localSecond    = 0;
    // NTP server hiện đang cấu hình (MGMT trả kèm trong GetSystemDateAndTime).
    // Chỉ có ý nghĩa khi dateTimeType == "NTP". ntpMode: "MANUAL" | "DHCP".
    std::string ntpMode;
    std::string ntpHost;
};

// Network configuration (Profile T mục 7.4). Chỉ IPv4 — ONVIF Profile T
// không bắt buộc IPv6 (không nhắc tới trong spec), MGMT có hỗ trợ IPv6 thật
// nhưng cố tình bỏ qua ở vertical slice này để đúng phạm vi mandatory; xem
// 01-IMPLEMENTATION_PLAN.md mục 2.1.

struct HostnameConfig {
    bool fromDhcp = false;
    std::string name;
};

struct DnsConfig {
    bool fromDhcp = false;
    std::string primaryDns;
    std::string secondaryDns;
    std::vector<std::string> searchDomain;
};

struct NetworkInterfaceConfig {
    std::string token;
    std::string name;
    std::string hwAddress;
    bool enabled = true;
    int  mtu = 1500;
    bool ipv4Enabled = true;
    bool dhcp = false;
    std::string address;
    int  prefixLength = 24;
};

struct NetworkGatewayConfig {
    std::string ipv4Address;
};

struct NetworkProtocolEntry {
    std::string name;   // "HTTP" | "HTTPS" | "RTSP" — đúng 3 giá trị ONVIF schema cho phép
    bool enabled = false;
    int  port = 0;
};
