#pragma once
// RecordingService — ONVIF Recording Control (trc, ver10/recording/wsdl) Profile G.
//
// String-based service (IOnvifService), pattern giống AnalyticsService.
// pathPrefix "/onvif/recording". Register 1 dòng trong OnvifServer.
//
// SCOPE (user chốt 2026-07-22): non-dynamic (DynamicRecordings/Tracks=false),
// 1 recording dựng sẵn (Recording_0) gồm 1 video track (VIDEO_0) + 1 metadata
// track (META_0), 1 recording job (Job_0), KHÔNG audio. Source = profile_main.
// Toàn bộ data model TĨNH → hardcode XML, KHÔNG cần backend/IPC.
//
// ponytail: stub-features — trả response schema-hợp-lệ cho mọi op §9.1 mandatory
// để DTT Feature Definition đánh dấu Profile G SUPPORTED. Ngữ nghĩa động (job
// thật, retention, apply config...) làm ở bước sau khi DTT lộ test case con.

#include "core/IOnvifService.h"
#include <string>

class RecordingService : public IOnvifService {
public:
    std::string pathPrefix() const override { return "/onvif/recording"; }
    std::string name() const override { return "RecordingService"; }
    std::string handle(const std::string& rawRequest) override;

private:
    static std::string extractRelatesTo(const std::string& xml);
    static std::string wrap(const std::string& action,
                            const std::string& relatesTo,
                            const std::string& bodyXml);
};
