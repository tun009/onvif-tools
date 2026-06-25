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
    int  year           = 2024;
    int  month          = 1;
    int  day            = 1;
    int  hour           = 0;
    int  minute         = 0;
    int  second         = 0;
    int  utcOffset      = 0;
    bool daylightSaving = false;
};
