#pragma once
// SearchService — ONVIF Recording Search (tse, ver10/search/wsdl) cho Profile G.
//
// String-based service (IOnvifService), pattern giống AnalyticsService.
// pathPrefix "/onvif/search". Register 1 dòng trong OnvifServer.
//
// SKELETON (define-features): mới chỉ GetServiceCapabilities để DTT nhận diện
// Profile G. FindRecordings/GetRecordingSearchResults... làm ở bước sau.
// ponytail: skeleton — chỉ caps tĩnh; add search ops khi DTT lộ test case con.

#include "core/IOnvifService.h"
#include <string>

class SearchService : public IOnvifService {
public:
    std::string pathPrefix() const override { return "/onvif/search"; }
    std::string name() const override { return "SearchService"; }
    std::string handle(const std::string& rawRequest) override;

private:
    std::string handleGetServiceCapabilities();
    static std::string extractRelatesTo(const std::string& xml);
    static std::string wrap(const std::string& action,
                            const std::string& relatesTo,
                            const std::string& bodyXml);
};
