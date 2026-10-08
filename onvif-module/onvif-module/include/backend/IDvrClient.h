#pragma once

#include "interface/types/MediaTypes.h"
#include <string>
#include <vector>

// Đợt 1: Media2 (Profile T) — GetProfiles/GetStreamUri/GetSnapshotUri
// đọc thật từ DVR mới (AlvisOS/DVR, REST /dvr/v3.0|v1.0/...).
// Đợt Profile G: trạng thái ghi hình (Recording Control) — xem
// 01-IMPLEMENTATION_PLAN.md Phase 7.
// MediaLegacyHandler.cpp (Media1/Profile S) không đi qua interface này,
// giữ nguyên logic hiện tại — xem 01-IMPLEMENTATION_PLAN.md Phase 4.
struct DvrClientConfig {
    std::string baseUrl;      // "http://127.0.0.1:8200"
    std::string deviceIp;     // IP công bố ra ngoài cho VMS — thay cho host nội bộ
                               // (127.0.0.1) mà DVR tự chèn vào StreamUri/SnapshotUri.
    int connectTimeoutMs = 1000;
    int requestTimeoutMs = 3000;
};

// Trạng thái ghi của 1 luồng (main/sub) thuộc 1 sensor. `isRecording` = ghi tay
// HOẶC ghi theo lịch (DVR không tách riêng hai nguồn này qua REST).
struct RecorderStreamState {
    std::string streamType;          // "main" | "sub" (| "third"...)
    bool        isRecording = false;
};

// 1 mục của GetListVideoSourceRecorder. DVR cũng trả thêm mục ảo "Overlay"
// (id "2", không có media profile ONVIF, không có file) — caller phải lọc.
struct RecorderSource {
    std::string videoSourceId;       // "0", "1"
    std::string name;                // "Context Camera"
    std::vector<RecorderStreamState> streams;
};

class IDvrClient {
public:
    virtual ~IDvrClient() = default;

    virtual std::vector<StreamProfile> getProfiles() = 0;
    virtual StreamUri   getStreamUri(const std::string& profileToken,
                                     StreamProtocol protocol) = 0;
    virtual SnapshotUri getSnapshotUri(const std::string& profileToken) = 0;

    // Recording Control (Profile G).
    virtual std::vector<RecorderSource> getRecorderSources() = 0;
    // Bật/tắt ghi TAY cho 1 luồng. DVR chỉ cho 1 luồng ghi tay mỗi sensor: bật luồng
    // này thì ghi tay của luồng kia tự tắt. Ném std::runtime_error khi DVR lỗi.
    // Lưu ý: DVR trả thành công cả khi từ chối âm thầm (luồng bị disable, camera
    // không có hình) — caller phải đọc lại trạng thái để xác nhận.
    virtual void setManualRecord(const std::string& videoSourceId,
                                 const std::string& streamType, bool enable) = 0;
};
