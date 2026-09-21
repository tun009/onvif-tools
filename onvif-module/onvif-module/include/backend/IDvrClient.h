#pragma once

#include "interface/types/MediaTypes.h"
#include <string>

// Đợt 1: chỉ Media2 (Profile T) — GetProfiles/GetStreamUri/GetSnapshotUri
// đọc thật từ DVR mới (AlvisOS/DVR, REST /dvr/v3.0|v1.0/...).
// MediaLegacyHandler.cpp (Media1/Profile S) không đi qua interface này,
// giữ nguyên logic hiện tại — xem 01-IMPLEMENTATION_PLAN.md Phase 4.
struct DvrClientConfig {
    std::string baseUrl;      // "http://127.0.0.1:8200"
    std::string deviceIp;     // IP công bố ra ngoài cho VMS — thay cho host nội bộ
                               // (127.0.0.1) mà DVR tự chèn vào StreamUri/SnapshotUri.
    int connectTimeoutMs = 1000;
    int requestTimeoutMs = 3000;
};

class IDvrClient {
public:
    virtual ~IDvrClient() = default;

    virtual std::vector<StreamProfile> getProfiles() = 0;
    virtual StreamUri   getStreamUri(const std::string& profileToken,
                                     StreamProtocol protocol) = 0;
    virtual SnapshotUri getSnapshotUri(const std::string& profileToken) = 0;
};
