#pragma once
// RecordingService — ONVIF Recording Control (trc, ver10/recording/wsdl) Profile G.
//
// String-based service (IOnvifService), pattern giống AnalyticsService.
// pathPrefix "/onvif/recording". Register 1 dòng trong OnvifServer.
//
// SKELETON (define-features): mới chỉ GetServiceCapabilities để DTT nhận diện
// Profile G. GetRecordings/CreateRecordingJob... làm ở bước sau.
// Scope đã chốt: non-dynamic (DynamicRecordings/Tracks=false), 1 recording dựng sẵn.
// ponytail: skeleton — chỉ caps tĩnh; add recording ops khi DTT lộ test case con.

#include "core/IOnvifService.h"
#include <string>

class RecordingService : public IOnvifService {
public:
    std::string pathPrefix() const override { return "/onvif/recording"; }
    std::string name() const override { return "RecordingService"; }
    std::string handle(const std::string& rawRequest) override;

private:
    std::string handleGetServiceCapabilities();
    static std::string extractRelatesTo(const std::string& xml);
    static std::string wrap(const std::string& action,
                            const std::string& relatesTo,
                            const std::string& bodyXml);
};
