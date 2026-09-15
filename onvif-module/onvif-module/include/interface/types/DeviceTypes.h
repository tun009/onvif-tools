#pragma once
#include <string>

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
