#pragma once
// MediaLegacyHandler — xử lý các op Media ver10 (legacy) mà test tool ONVIF
// vẫn gọi để phát hiện VideoSource, ngay cả khi thiết bị dùng Media2 (Profile T).
// Trả XML thủ công (giống Event/Discovery) vì Profile T chỉ dùng Media2;
// chỉ implement subset đủ để test IMAGING pass STEP "Get video sources".

#include <string>

class MediaLegacyHandler {
public:
    // Nhận diện request Media ver10 trong body; trả response XML nếu match,
    // trả "" nếu không phải op ver10 (để Media2 xử lý tiếp).
    static std::string dispatch(const std::string& rawRequest);

private:
    static std::string handleGetVideoSources();
    static std::string extractMessageId(const std::string& xml);
    static std::string wrap(const std::string& action,
                            const std::string& relatesTo,
                            const std::string& bodyXml);
};
