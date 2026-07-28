#pragma once
// SearchService — ONVIF Recording Search (tse, ver10/search/wsdl) cho Profile G.
//
// String-based service (IOnvifService), pattern giống AnalyticsService.
// pathPrefix "/onvif/search". Register 1 dòng trong OnvifServer.
//
// STUB-FEATURES: trả response schema-hợp-lệ cho mọi op §7.3 mandatory. Search là
// đồng bộ (FindRecordings/FindEvents trả token; GetXxxResults trả toàn bộ kết quả
// 1 lần, SearchState=Completed). 1 recording tĩnh Recording_0. XPath filter chỉ
// parse-cho-qua (trả superset). Đủ để DTT Feature Definition đánh dấu Profile G.
// ponytail: stub — kết quả tĩnh; lọc theo thời gian/XPath thật làm ở bước sau.

#include "core/IOnvifService.h"
#include <string>

class SearchService : public IOnvifService {
public:
    std::string pathPrefix() const override { return "/onvif/search"; }
    std::string name() const override { return "SearchService"; }
    std::string handle(const std::string& rawRequest) override;

private:
    static std::string extractRelatesTo(const std::string& xml);
    static std::string extractInnerTag(const std::string& xml, const std::string& tag);
    static std::string wrap(const std::string& action,
                            const std::string& relatesTo,
                            const std::string& bodyXml);
};
