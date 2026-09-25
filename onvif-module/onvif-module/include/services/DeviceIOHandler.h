#pragma once
// DeviceIOHandler — trả manual XML cho DeviceIO service (ONVIF ver10/deviceIO).
// Profile T §7.10.3 mandate GetVideoSources trên DeviceIO service. Test tool
// helper (Media2ServiceExtensions.HelperConfigureMediaProfileWithVideoSource)
// gọi op này để lấy metadata VideoSource — nếu server không có DeviceIO,
// helper crash NullReferenceException (MEDIA2-2-2-1).
//
// Implement subset đủ pass Profile T tests:
//   - GetServiceCapabilities
//   - GetVideoSources (mandatory)
//   - GetAudioSources (mock rỗng)
//   - GetRelayOutputs (mock rỗng)
//   - GetDigitalInputs (mock rỗng)

#include "interface/ICameraBackend.h"
#include <memory>
#include <string>

class DeviceIOHandler {
public:
    // Nhận diện request DeviceIO (namespace ver10/deviceIO/wsdl) trong body;
    // trả response XML nếu match, "" nếu không phải op DeviceIO.
    // backend: dùng cho GetVideoSources (đọc sourceToken thật từ
    // backend_->getProfiles() — trước đây hardcode "src_main", khiến các test
    // negative dùng token thật (VD "1") tưởng nhầm là invalid — xem
    // IMAGING-2-1-16 ở docs/onvif-alvis/01-IMPLEMENTATION_PLAN.md).
    static std::string dispatch(const std::string& rawRequest,
                                const CameraBackendPtr& backend);

private:
    static std::string handleGetServiceCapabilities();
    static std::string handleGetVideoSources(const CameraBackendPtr& backend);
    static std::string handleGetAudioSources();
    static std::string handleGetRelayOutputs();
    static std::string handleGetDigitalInputs();
    static std::string extractMessageId(const std::string& xml);
    static std::string wrap(const std::string& action,
                            const std::string& relatesTo,
                            const std::string& bodyXml);
};
