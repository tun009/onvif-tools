#pragma once

#include "interface/types/DeviceTypes.h"
#include "interface/types/ImagingTypes.h"
#include <stdexcept>
#include <string>
#include <vector>

struct MgmtClientConfig { std::string baseUrl; int connectTimeoutMs = 1000; int requestTimeoutMs = 3000; };

// MGMT từ chối request vì input sai (result=-1, kèm field_error) — khác với
// lỗi vận hành (result=0, HTTP lỗi, mạng lỗi...) vốn nên là SOAP Receiver
// fault. Phân biệt hai loại lỗi này bằng exception type để caller (DeviceService)
// map đúng SOAP-ENV:Sender / SOAP-ENV:Receiver.
class MgmtValidationError : public std::runtime_error {
public:
    explicit MgmtValidationError(const std::string& message) : std::runtime_error(message) {}
};

struct WssePasswordDigest {
    std::string username;
    std::string nonce;
    std::string created;
    std::string passwordDigest;
};

struct HttpDigestCredential {
    std::string username;
    std::string realm;
    std::string method;
    std::string uri;
    std::string nonce;
    std::string qop;
    std::string nc;
    std::string cnonce;
    std::string algorithm;
    std::string response;
};

struct OnvifAuthenticationResult {
    bool authenticated = false;
    std::string userLevel;
};

// Zoom/focus lens thật — MGMT LensApiController (GET/PUT /mgmt/v1/Config/ZoomFocus,
// GET /mgmt/v1/Config/LensInfo). Đây là contract riêng của MGMT (3 field luôn
// gửi cùng nhau, FocusMode quyết định field nào áp dụng — xem
// docs/onvif-alvis/01-IMPLEMENTATION_PLAN.md mục Phase 5), không map thẳng
// 1-1 vào PTZVector/ImagingSettings nên tách struct riêng; AlvisBackendFacade
// là nơi dịch sang/từ PTZVector cho ONVIF PTZ service.
enum class ZoomFocusMode { AUTO_FOCUS = 0, MANUAL_FOCUS = 1, RUN_AF = 2, ZF_SYNC = 3 };

struct ZoomFocusState {
    float zoomValue  = 1.0f;   // tỉ lệ quang học, 1.0-4.0 (MGMT ZoomValue)
    float focusValue = 50.0f;  // 0-100 (MGMT FocusValue)
    ZoomFocusMode focusMode = ZoomFocusMode::AUTO_FOCUS;
    bool zoomMoving  = false;  // MGMT ZMActive — motor zoom đang bận
    bool focusMoving = false;  // MGMT FMActive — motor focus đang bận
};

// Biên độ zoom/focus thật của lens đang gắn (MGMT GET /mgmt/v1/Config/LensInfo).
struct LensBounds {
    float minZoom = 1.0f;
    float maxZoom = 4.0f;
    float stepZoom = 0.1f;
    float minFocus = 0.0f;
    float maxFocus = 100.0f;
    float stepFocus = 1.0f;
};

class IMgmtClient {
public:
    virtual ~IMgmtClient() = default;
    virtual DeviceInfo getDeviceInformation() = 0;
    virtual SystemDateTime getSystemDateAndTime() = 0;
    // Ném MgmtValidationError khi MGMT từ chối do input sai (result=-1); ném
    // std::runtime_error (hoặc lớp con khác) khi lỗi vận hành/kết nối.
    virtual void setSystemDateAndTime(const SystemDateTime& request) = 0;

    // Network configuration (Profile T 7.4) — chỉ IPv4, xem DeviceTypes.h.
    virtual HostnameConfig getHostname() = 0;
    virtual void setHostname(const HostnameConfig& request) = 0;
    virtual DnsConfig getDns() = 0;
    virtual void setDns(const DnsConfig& request) = 0;
    virtual NetworkInterfaceConfig getNetworkInterface() = 0;
    virtual void setNetworkInterface(const NetworkInterfaceConfig& request) = 0;
    virtual NetworkGatewayConfig getNetworkGateway() = 0;
    virtual void setNetworkGateway(const NetworkGatewayConfig& request) = 0;
    virtual std::vector<NetworkProtocolEntry> getNetworkProtocols() = 0;
    virtual void setNetworkProtocols(const std::vector<NetworkProtocolEntry>& request) = 0;

    virtual OnvifAuthenticationResult verifyWssePasswordDigest(
        const WssePasswordDigest& credential) = 0;
    virtual OnvifAuthenticationResult verifyHttpDigest(
        const HttpDigestCredential& credential) = 0;

    // Imaging (Profile T §7.9 mandatory) — MGMT ImagingSettingsApiController.
    // sourceToken: "0" (context) / "1" (ALPR), khớp VideoSourceToken thật.
    // Chỉ map field đã có tương ứng rõ ràng cả 2 phía (Brightness/Contrast/
    // ColorSaturation/Sharpness/BLC/WDR) — Exposure/WhiteBalance/IrCutFilter
    // vẫn giữ echo cache cục bộ ở ImagingService (xem plan doc), không đụng ở
    // đây.
    virtual ImagingSettings getImagingSettings(const std::string& sourceToken) = 0;
    virtual void setImagingSettings(const std::string& sourceToken,
                                     const ImagingSettings& settings) = 0;

    // Zoom/focus lens thật — MGMT LensApiController. sourceToken: "0"/"1".
    virtual ZoomFocusState getZoomFocus(const std::string& sourceToken) = 0;
    virtual void setZoomFocus(const std::string& sourceToken,
                               const ZoomFocusState& state) = 0;
    virtual LensBounds getLensBounds(const std::string& sourceToken) = 0;
};
