#include "backend/HttpMgmtClient.h"
#include "utils/SimpleJson.h"
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <utility>

namespace {
struct Endpoint { std::string host; std::string port; std::string basePath; };
Endpoint parseHttpUrl(const std::string& url) {
    const std::string prefix = "http://";
    if (url.rfind(prefix, 0) != 0) throw std::runtime_error("MGMT URL must use http://");
    const std::string rest = url.substr(prefix.size()); const auto slash = rest.find('/');
    const std::string authority = rest.substr(0, slash); const auto colon = authority.rfind(':');
    Endpoint result{colon == std::string::npos ? authority : authority.substr(0, colon),
                    colon == std::string::npos ? "80" : authority.substr(colon + 1),
                    slash == std::string::npos ? "" : rest.substr(slash)};
    if (result.host.empty() || result.port.empty()) throw std::runtime_error("Invalid MGMT URL");
    return result;
}

std::string escapeJson(const std::string& value) {
    std::string output;
    output.reserve(value.size());
    for (const unsigned char ch : value) {
        switch (ch) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (ch < 0x20) throw std::runtime_error("Invalid control character in JSON value");
            output.push_back(static_cast<char>(ch));
        }
    }
    return output;
}

void sendAll(int fd, const std::string& data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t sent = send(fd, data.data() + offset, data.size() - offset,
                                  MSG_NOSIGNAL);
        if (sent <= 0) throw std::runtime_error("Failed to send MGMT request");
        offset += static_cast<std::size_t>(sent);
    }
}

std::string jsonObject(const std::string& json, const std::string& key) {
    const std::string marker = "\"" + key + "\":";
    auto start = json.find(marker);
    if (start == std::string::npos) return {};
    start = json.find('{', start + marker.size());
    if (start == std::string::npos) return {};
    int depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (std::size_t pos = start; pos < json.size(); ++pos) {
        const char ch = json[pos];
        if (quoted) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') quoted = false;
            continue;
        }
        if (ch == '"') quoted = true;
        else if (ch == '{') ++depth;
        else if (ch == '}' && --depth == 0) return json.substr(start, pos - start + 1);
    }
    return {};
}

bool validDateTime(int year, int month, int day, int hour, int minute, int second) {
    return year >= 1970 && year <= 9999 && month >= 1 && month <= 12 &&
           day >= 1 && day <= 31 && hour >= 0 && hour <= 23 &&
           minute >= 0 && minute <= 59 && second >= 0 && second <= 59;
}

// Trích chuỗi con "[...]" ứng với 1 key, cùng cách bracket-matching như
// jsonObject() nhưng cho mảng thay vì object.
std::string jsonArray(const std::string& json, const std::string& key) {
    const std::string marker = "\"" + key + "\":";
    auto start = json.find(marker);
    if (start == std::string::npos) return {};
    start = json.find('[', start + marker.size());
    if (start == std::string::npos) return {};
    int depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (std::size_t pos = start; pos < json.size(); ++pos) {
        const char ch = json[pos];
        if (quoted) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') quoted = false;
            continue;
        }
        if (ch == '"') quoted = true;
        else if (ch == '[') ++depth;
        else if (ch == ']' && --depth == 0) return json.substr(start, pos - start + 1);
    }
    return {};
}

// Tách 1 chuỗi mảng JSON thành các object con ở top-level (chỉ đếm độ sâu
// {}, bỏ qua dấu phẩy/khoảng trắng — đủ dùng cho mảng phẳng như
// NetworkProtocols/NetworkInterfaces, không cần parser JSON đầy đủ).
std::vector<std::string> jsonArrayObjects(const std::string& arrayJson) {
    std::vector<std::string> result;
    int depth = 0;
    bool quoted = false;
    bool escaped = false;
    std::size_t objStart = std::string::npos;
    for (std::size_t pos = 0; pos < arrayJson.size(); ++pos) {
        const char ch = arrayJson[pos];
        if (quoted) {
            if (escaped) escaped = false;
            else if (ch == '\\') escaped = true;
            else if (ch == '"') quoted = false;
            continue;
        }
        if (ch == '"') { quoted = true; continue; }
        if (ch == '{') { if (depth == 0) objStart = pos; ++depth; continue; }
        if (ch == '}') {
            if (--depth == 0 && objStart != std::string::npos) {
                result.push_back(arrayJson.substr(objStart, pos - objStart + 1));
                objStart = std::string::npos;
            }
        }
    }
    return result;
}

// Mảng chuỗi phẳng kiểu ["1.2.3.4","8.8.8.8"] — trả các giá trị theo thứ tự.
std::vector<std::string> jsonArrayStrings(const std::string& arrayJson) {
    std::vector<std::string> result;
    bool quoted = false;
    bool escaped = false;
    std::string current;
    for (std::size_t pos = 0; pos < arrayJson.size(); ++pos) {
        const char ch = arrayJson[pos];
        if (quoted) {
            if (escaped) { current.push_back(ch); escaped = false; }
            else if (ch == '\\') escaped = true;
            else if (ch == '"') { quoted = false; result.push_back(current); current.clear(); }
            else current.push_back(ch);
            continue;
        }
        if (ch == '"') quoted = true;
    }
    return result;
}

// MGMT lưu subnet dạng dotted mask ("255.255.255.0"); ONVIF dùng CIDR prefix
// length (int). Cần đổi 2 chiều ở biên onvif-module <-> MGMT.
std::string prefixLengthToSubnetMask(int prefix) {
    if (prefix < 0) prefix = 0;
    if (prefix > 32) prefix = 32;
    const std::uint32_t mask = prefix == 0 ? 0u : (0xFFFFFFFFu << (32 - prefix));
    std::ostringstream oss;
    oss << ((mask >> 24) & 0xFF) << '.' << ((mask >> 16) & 0xFF) << '.'
        << ((mask >> 8) & 0xFF) << '.' << (mask & 0xFF);
    return oss.str();
}

int subnetMaskToPrefixLength(const std::string& mask) {
    unsigned int a = 0, b = 0, c = 0, d = 0;
    if (std::sscanf(mask.c_str(), "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return 24;
    const std::uint32_t value = (a << 24) | (b << 16) | (c << 8) | d;
    int prefix = 0;
    for (int i = 31; i >= 0; --i) {
        if (value & (1u << i)) ++prefix;
        else break;
    }
    return prefix;
}
}

HttpMgmtClient::HttpMgmtClient(MgmtClientConfig config) : config_(std::move(config)) {}

HttpMgmtClient::HttpResponse HttpMgmtClient::request(
    const std::string& method, const std::string& path, const std::string& body) const {
    const auto endpoint = parseHttpUrl(config_.baseUrl); addrinfo hints{}; hints.ai_socktype = SOCK_STREAM;
    addrinfo* addresses = nullptr;
    if (getaddrinfo(endpoint.host.c_str(), endpoint.port.c_str(), &hints, &addresses) != 0) throw std::runtime_error("Cannot resolve MGMT host");
    int fd = -1;
    for (auto* address = addresses; address; address = address->ai_next) {
        fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol); if (fd < 0) continue;
        timeval connectTimeout{config_.connectTimeoutMs / 1000, (config_.connectTimeoutMs % 1000) * 1000};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &connectTimeout, sizeof(connectTimeout)); setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &connectTimeout, sizeof(connectTimeout));
        if (connect(fd, address->ai_addr, address->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(addresses); if (fd < 0) throw std::runtime_error("Cannot connect to MGMT");
    timeval requestTimeout{config_.requestTimeoutMs / 1000, (config_.requestTimeoutMs % 1000) * 1000};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &requestTimeout, sizeof(requestTimeout)); setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &requestTimeout, sizeof(requestTimeout));
    std::string payload = method + " " + endpoint.basePath + path +
        " HTTP/1.1\r\nHost: " + endpoint.host + "\r\nConnection: close\r\nAccept: application/json\r\n";
    if (!body.empty()) {
        payload += "Content-Type: application/json\r\nContent-Length: " +
                   std::to_string(body.size()) + "\r\n";
    }
    payload += "\r\n" + body;
    try { sendAll(fd, payload); }
    catch (...) { close(fd); throw; }
    std::string response; char buffer[4096];
    for (;;) { const auto count = recv(fd, buffer, sizeof(buffer), 0); if (count == 0) break; if (count < 0) { close(fd); throw std::runtime_error("Failed to read MGMT response"); } response.append(buffer, static_cast<size_t>(count)); }
    close(fd); const auto separator = response.find("\r\n\r\n");
    if (separator == std::string::npos) throw std::runtime_error("MGMT returned invalid HTTP response");
    const auto statusStart = response.find(' ');
    if (statusStart == std::string::npos || statusStart + 4 > response.size())
        throw std::runtime_error("MGMT returned invalid HTTP status");
    const int status = std::atoi(response.c_str() + statusStart + 1);
    if (status < 100 || status > 599) throw std::runtime_error("MGMT returned invalid HTTP status");
    return {status, response.substr(separator + 4)};
}

DeviceInfo HttpMgmtClient::getDeviceInformation() {
    const HttpResponse response = request("GET", "/mgmt/v1/Config/DeviceInformation");
    if (response.status != 200) throw std::runtime_error("MGMT rejected DeviceInformation request");
    const std::string& json = response.body;
    const auto start = json.find("\"GetDeviceInformationResponse\"");
    if (start == std::string::npos || SimpleJson::getInt(json, "result", 0) != 1) throw std::runtime_error("MGMT returned invalid DeviceInformation payload");
    const auto body = json.substr(start); DeviceInfo info;
    info.manufacturer = SimpleJson::getString(body, "Manufacturer"); info.model = SimpleJson::getString(body, "Model");
    info.firmwareVersion = SimpleJson::getString(body, "FirmwareVersion"); info.serialNumber = SimpleJson::getString(body, "SerialNumber"); info.hardwareId = SimpleJson::getString(body, "HardwareId");
    return info;
}

SystemDateTime HttpMgmtClient::getSystemDateAndTime() {
    const HttpResponse response = request("GET", "/mgmt/v1/Config/GetSystemDateAndTime");
    if (response.status != 200)
        throw std::runtime_error("MGMT rejected GetSystemDateAndTime request");
    if (SimpleJson::getInt(response.body, "result", 0) != 1)
        throw std::runtime_error("MGMT returned unsuccessful GetSystemDateAndTime result");

    const std::string root = jsonObject(response.body, "GetSystemDateAndTimeResponse");
    const std::string state = jsonObject(root, "SystemDateAndTime");
    const std::string zone = jsonObject(state, "TimeZone");
    const std::string utc = jsonObject(state, "UTCDateTime");
    const std::string utcDate = jsonObject(utc, "Date");
    const std::string utcTime = jsonObject(utc, "Time");
    const std::string local = jsonObject(state, "LocalDateTime");
    const std::string localDate = jsonObject(local, "Date");
    const std::string localTime = jsonObject(local, "Time");
    const std::string ntp = jsonObject(state, "NTPServer");
    if (state.empty() || zone.empty() || utcDate.empty() || utcTime.empty() ||
        localDate.empty() || localTime.empty())
        throw std::runtime_error("MGMT returned incomplete GetSystemDateAndTime payload");

    SystemDateTime result;
    result.dateTimeType = SimpleJson::getString(state, "DateTimeType");
    result.daylightSaving = SimpleJson::getBool(state, "DaylightSavings", false);
    result.timezone = SimpleJson::getString(zone, "TZ");
    result.year = SimpleJson::getInt(utcDate, "Year");
    result.month = SimpleJson::getInt(utcDate, "Month");
    result.day = SimpleJson::getInt(utcDate, "Day");
    result.hour = SimpleJson::getInt(utcTime, "Hour");
    result.minute = SimpleJson::getInt(utcTime, "Minute");
    result.second = SimpleJson::getInt(utcTime, "Second");
    result.localYear = SimpleJson::getInt(localDate, "Year");
    result.localMonth = SimpleJson::getInt(localDate, "Month");
    result.localDay = SimpleJson::getInt(localDate, "Day");
    result.localHour = SimpleJson::getInt(localTime, "Hour");
    result.localMinute = SimpleJson::getInt(localTime, "Minute");
    result.localSecond = SimpleJson::getInt(localTime, "Second");
    // NTPServer có thể rỗng (chưa từng cấu hình qua MGMT/Web) — không coi đây
    // là payload không hợp lệ, chỉ đơn thuần là "NTP chưa sẵn sàng dùng".
    result.ntpMode = SimpleJson::getString(ntp, "Mode");
    result.ntpHost = SimpleJson::getString(ntp, "Host");

    if ((result.dateTimeType != "MANUAL" && result.dateTimeType != "NTP") ||
        result.timezone.empty() ||
        !validDateTime(result.year, result.month, result.day, result.hour,
                       result.minute, result.second) ||
        !validDateTime(result.localYear, result.localMonth, result.localDay,
                       result.localHour, result.localMinute, result.localSecond))
        throw std::runtime_error("MGMT returned invalid GetSystemDateAndTime payload");
    return result;
}

void HttpMgmtClient::setSystemDateAndTime(const SystemDateTime& req) {
    if (req.dateTimeType != "MANUAL" && req.dateTimeType != "NTP")
        throw std::invalid_argument("setSystemDateAndTime: dateTimeType must be MANUAL or NTP");
    if (req.timezone.empty())
        throw std::invalid_argument("setSystemDateAndTime: timezone must not be empty");

    std::ostringstream body;
    body << "{\"DateTimeType\":\"" << escapeJson(req.dateTimeType) << "\""
         << ",\"TimeZone\":{\"TZ\":\"" << escapeJson(req.timezone) << "\"}";
    if (req.dateTimeType == "NTP") {
        body << ",\"NTPServer\":{\"Mode\":\"" << escapeJson(req.ntpMode)
             << "\",\"Host\":\"" << escapeJson(req.ntpHost) << "\"}";
    } else {
        body << ",\"UTCDateTime\":{"
             << "\"Date\":{\"Year\":" << req.year << ",\"Month\":" << req.month
             << ",\"Day\":" << req.day << "},"
             << "\"Time\":{\"Hour\":" << req.hour << ",\"Minute\":" << req.minute
             << ",\"Second\":" << req.second << "}}";
    }
    body << "}";

    const HttpResponse response = request(
        "POST", "/mgmt/v1/Config/SetSystemDateAndTime", body.str());
    const int resultCode = SimpleJson::getInt(response.body, "result", 0);
    if (response.status == 200 && resultCode == 1) return;
    if (resultCode == -1) {
        throw MgmtValidationError(
            "MGMT rejected SetSystemDateAndTime, field=" +
            SimpleJson::getString(response.body, "field_error", "unknown"));
    }
    throw std::runtime_error(
        "MGMT SetSystemDateAndTime failed (status=" +
        std::to_string(response.status) + ", result=" + std::to_string(resultCode) + ")");
}

HostnameConfig HttpMgmtClient::getHostname() {
    const HttpResponse response = request("GET", "/mgmt/v1/GetHostname");
    if (response.status != 200) throw std::runtime_error("MGMT rejected GetHostname request");
    const std::string info = jsonObject(response.body, "HostnameInformation");
    if (info.empty()) throw std::runtime_error("MGMT returned invalid GetHostname payload");
    HostnameConfig result;
    result.fromDhcp = SimpleJson::getBool(info, "FromDHCP", false);
    result.name = SimpleJson::getString(info, "Name");
    return result;
}

void HttpMgmtClient::setHostname(const HostnameConfig& req) {
    const std::string body = "{\"Name\":\"" + escapeJson(req.name) +
        "\",\"FromDHCP\":" + (req.fromDhcp ? "true" : "false") + "}";
    const HttpResponse response = request("POST", "/mgmt/v1/SetHostname", body);
    const int resultCode = SimpleJson::getInt(response.body, "result", 0);
    if (response.status == 200 && resultCode == 1) return;
    if (response.status == 400) {
        throw MgmtValidationError("MGMT rejected SetHostname: " +
                                   SimpleJson::getString(response.body, "error", "unknown"));
    }
    throw std::runtime_error("MGMT SetHostname failed (status=" +
                              std::to_string(response.status) + ")");
}

DnsConfig HttpMgmtClient::getDns() {
    const HttpResponse response = request("GET", "/mgmt/v1/GetDNS");
    if (response.status != 200) throw std::runtime_error("MGMT rejected GetDNS request");
    const std::string info = jsonObject(response.body, "DNSInformation");
    if (info.empty()) throw std::runtime_error("MGMT returned invalid GetDNS payload");
    DnsConfig result;
    result.fromDhcp = SimpleJson::getBool(info, "FromDHCP", false);
    // Chỉ lấy entry Type=IPv4 — bỏ qua IPv6 theo phạm vi đã chốt (mục 2.1
    // 01-IMPLEMENTATION_PLAN.md).
    std::vector<std::string> ipv4;
    for (const auto& item : jsonArrayObjects(jsonArray(info, "DNSManual"))) {
        if (SimpleJson::getString(item, "Type") == "IPv4") {
            ipv4.push_back(SimpleJson::getString(item, "IPv4Address"));
        }
    }
    if (!ipv4.empty()) result.primaryDns = ipv4[0];
    if (ipv4.size() > 1) result.secondaryDns = ipv4[1];
    result.searchDomain = jsonArrayStrings(jsonArray(info, "SearchDomain"));
    return result;
}

void HttpMgmtClient::setDns(const DnsConfig& req) {
    std::ostringstream body;
    body << "{\"FromDHCP\":" << (req.fromDhcp ? "true" : "false");
    if (!req.fromDhcp) {
        body << ",\"DNSManual\":[";
        bool first = true;
        if (!req.primaryDns.empty()) {
            body << "{\"IPv4Address\":\"" << escapeJson(req.primaryDns) << "\"}";
            first = false;
        }
        if (!req.secondaryDns.empty()) {
            if (!first) body << ",";
            body << "{\"IPv4Address\":\"" << escapeJson(req.secondaryDns) << "\"}";
        }
        body << "]";
    }
    body << "}";
    const HttpResponse response = request("POST", "/mgmt/v1/SetDNS", body.str());
    const int resultCode = SimpleJson::getInt(response.body, "result", 0);
    if (response.status == 200 && resultCode == 1) return;
    if (response.status == 400) {
        throw MgmtValidationError("MGMT rejected SetDNS: " +
                                   SimpleJson::getString(response.body, "error", "unknown"));
    }
    throw std::runtime_error("MGMT SetDNS failed (status=" +
                              std::to_string(response.status) + ")");
}

NetworkInterfaceConfig HttpMgmtClient::getNetworkInterface() {
    const HttpResponse response = request("GET", "/mgmt/v1/GetNetworkInterfaces");
    if (response.status != 200) throw std::runtime_error("MGMT rejected GetNetworkInterfaces request");
    const std::string root = jsonObject(response.body, "GetNetworkInterfacesResponse");
    const auto objects = jsonArrayObjects(jsonArray(root, "NetworkInterfaces"));
    if (objects.empty()) throw std::runtime_error("MGMT returned no network interface");
    const std::string& iface = objects.front();
    NetworkInterfaceConfig result;
    result.token = SimpleJson::getString(iface, "token");
    result.enabled = SimpleJson::getBool(iface, "Enabled", true);
    const std::string info = jsonObject(iface, "Info");
    result.name = SimpleJson::getString(info, "Name");
    result.hwAddress = SimpleJson::getString(info, "HwAddress");
    const std::string ipv4 = jsonObject(iface, "IPv4");
    result.ipv4Enabled = SimpleJson::getBool(ipv4, "Enabled", true);
    const std::string config = jsonObject(ipv4, "Config");
    result.dhcp = SimpleJson::getBool(config, "DHCP", false);
    const auto manualObjs = jsonArrayObjects(jsonArray(config, "Manual"));
    if (!manualObjs.empty()) {
        result.address = SimpleJson::getString(manualObjs.front(), "Address");
        result.prefixLength = subnetMaskToPrefixLength(
            SimpleJson::getString(manualObjs.front(), "SubnetMask"));
    }
    return result;
}

void HttpMgmtClient::setNetworkInterface(const NetworkInterfaceConfig& req) {
    std::ostringstream body;
    body << "{\"NetworkInterface\":{\"IPv4\":{\"Enabled\":" << (req.ipv4Enabled ? "true" : "false")
         << ",\"DHCP\":" << (req.dhcp ? "true" : "false");
    if (!req.dhcp) {
        body << ",\"Manual\":[{\"Address\":\"" << escapeJson(req.address)
             << "\",\"SubnetMask\":\""
             << escapeJson(prefixLengthToSubnetMask(req.prefixLength)) << "\"}]";
    }
    body << "}}}";
    const HttpResponse response = request("POST", "/mgmt/v1/SetNetworkInterfaces", body.str());
    const int resultCode = SimpleJson::getInt(response.body, "result", 0);
    // Giới hạn đã biết: MGMT trả result=1 ngay sau khi validate xong, việc
    // apply thật (nmcli) chạy trong background thread riêng — không có cách
    // xác nhận apply thành công đồng bộ qua chính response này.
    if (response.status == 200 && resultCode == 1) return;
    if (response.status == 400) {
        throw MgmtValidationError("MGMT rejected SetNetworkInterfaces: " +
                                   SimpleJson::getString(response.body, "error", "unknown"));
    }
    throw std::runtime_error("MGMT SetNetworkInterfaces failed (status=" +
                              std::to_string(response.status) + ")");
}

NetworkGatewayConfig HttpMgmtClient::getNetworkGateway() {
    const HttpResponse response = request("GET", "/mgmt/v1/GetNetworkDefaultGateway");
    if (response.status != 200) throw std::runtime_error("MGMT rejected GetNetworkDefaultGateway request");
    const std::string gw = jsonObject(response.body, "NetworkGateway");
    NetworkGatewayConfig result;
    const auto v4list = jsonArrayStrings(jsonArray(gw, "IPv4Address"));
    if (!v4list.empty()) result.ipv4Address = v4list.front();
    return result;
}

void HttpMgmtClient::setNetworkGateway(const NetworkGatewayConfig& req) {
    const std::string body = "{\"IPv4Address\":[\"" + escapeJson(req.ipv4Address) + "\"]}";
    const HttpResponse response = request("POST", "/mgmt/v1/SetNetworkDefaultGateway", body);
    const int resultCode = SimpleJson::getInt(response.body, "result", 0);
    if (response.status == 200 && resultCode == 1) return;
    if (response.status == 400) {
        throw MgmtValidationError("MGMT rejected SetNetworkDefaultGateway: " +
                                   SimpleJson::getString(response.body, "error", "unknown"));
    }
    throw std::runtime_error("MGMT SetNetworkDefaultGateway failed (status=" +
                              std::to_string(response.status) + ")");
}

std::vector<NetworkProtocolEntry> HttpMgmtClient::getNetworkProtocols() {
    const HttpResponse response = request("GET", "/mgmt/v1/GetNetworkProtocols");
    if (response.status != 200) throw std::runtime_error("MGMT rejected GetNetworkProtocols request");
    std::vector<NetworkProtocolEntry> result;
    for (const auto& item : jsonArrayObjects(jsonArray(response.body, "NetworkProtocols"))) {
        NetworkProtocolEntry entry;
        entry.name = SimpleJson::getString(item, "Name");
        entry.enabled = SimpleJson::getBool(item, "Enabled", false);
        entry.port = SimpleJson::getInt(item, "PortNumber", 0);
        result.push_back(entry);
    }
    return result;
}

void HttpMgmtClient::setNetworkProtocols(const std::vector<NetworkProtocolEntry>& req) {
    std::ostringstream body;
    body << "{\"NetworkProtocols\":[";
    for (std::size_t i = 0; i < req.size(); ++i) {
        if (i) body << ",";
        body << "{\"Name\":\"" << escapeJson(req[i].name) << "\",\"Enabled\":"
             << (req[i].enabled ? "true" : "false") << ",\"PortNumber\":" << req[i].port << "}";
    }
    body << "]}";
    const HttpResponse response = request("POST", "/mgmt/v1/SetNetworkProtocols", body.str());
    const int resultCode = SimpleJson::getInt(response.body, "result", 0);
    if (response.status == 200 && resultCode == 1) return;
    // MGMT dùng result âm (-1..-4) cho các case xung đột port HTTP/HTTPS —
    // là lỗi input/xung đột, không phải backend lỗi -> Sender fault.
    if (resultCode < 0 || response.status == 400) {
        throw MgmtValidationError("MGMT rejected SetNetworkProtocols: " +
                                   SimpleJson::getString(response.body, "error", "unknown"));
    }
    throw std::runtime_error("MGMT SetNetworkProtocols failed (status=" +
                              std::to_string(response.status) + ")");
}

OnvifAuthenticationResult HttpMgmtClient::verifyWssePasswordDigest(
    const WssePasswordDigest& credential) {
    const std::string body =
        "{\"Username\":\"" + escapeJson(credential.username) +
        "\",\"Nonce\":\"" + escapeJson(credential.nonce) +
        "\",\"Created\":\"" + escapeJson(credential.created) +
        "\",\"PasswordDigest\":\"" + escapeJson(credential.passwordDigest) + "\"}";
    const HttpResponse response = request(
        "POST", "/internal/v1/auth/onvif/wsse-password-digest", body);
    if (response.status == 200 && SimpleJson::getInt(response.body, "result", 0) == 1 &&
        SimpleJson::getBool(response.body, "Authenticated", false)) {
        return {true, SimpleJson::getString(response.body, "UserLevel")};
    }
    if (response.status == 400 || response.status == 401 || response.status == 403)
        return {};
    throw std::runtime_error("MGMT ONVIF authentication service unavailable");
}

OnvifAuthenticationResult HttpMgmtClient::verifyHttpDigest(
    const HttpDigestCredential& credential) {
    const std::string body =
        "{\"Username\":\"" + escapeJson(credential.username) +
        "\",\"Realm\":\"" + escapeJson(credential.realm) +
        "\",\"Method\":\"" + escapeJson(credential.method) +
        "\",\"Uri\":\"" + escapeJson(credential.uri) +
        "\",\"Nonce\":\"" + escapeJson(credential.nonce) +
        "\",\"Qop\":\"" + escapeJson(credential.qop) +
        "\",\"Nc\":\"" + escapeJson(credential.nc) +
        "\",\"Cnonce\":\"" + escapeJson(credential.cnonce) +
        "\",\"Algorithm\":\"" + escapeJson(credential.algorithm) +
        "\",\"Response\":\"" + escapeJson(credential.response) + "\"}";
    const HttpResponse response = request(
        "POST", "/internal/v1/auth/onvif/http-digest", body);
    const int resultCode = SimpleJson::getInt(response.body, "result", 0);
    const bool authenticated = SimpleJson::getBool(response.body, "Authenticated", false);
    std::cerr << "[HttpMgmtClient] HTTP Digest verification response: status="
              << response.status << " result=" << resultCode
              << " authenticated=" << (authenticated ? "true" : "false")
              << std::endl;
    if (response.status == 200 && resultCode == 1 && authenticated) {
        return {true, SimpleJson::getString(response.body, "UserLevel")};
    }
    if (response.status == 400 || response.status == 401 || response.status == 403)
        return {};
    throw std::runtime_error("MGMT ONVIF HTTP Digest service unavailable");
}
