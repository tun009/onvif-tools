#pragma once
// IOnvifService — interface chung cho MỌI ONVIF service (string-based dispatch).
//
// Chuẩn kiến trúc: xem docs/10-refactor-plan-multi-profile.md PHẦN II.
// Mỗi service nhận raw request (HTTP headers + SOAP body), tự nhận diện
// operation, trả về response XML string. Trả "" nếu không nhận diện được op
// (để ServiceRegistry/OnvifServer fallback).
//
// Pattern này áp dụng cho service manual-XML (DeviceIOHandler, MediaLegacyHandler,
// MockSubscriptionManager, AnalyticsService...). Service dùng gSOAP binding
// (Media2Service, DeviceService) sẽ được bọc adapter khi migrate (Phase 3+).

#include <string>

class IOnvifService {
public:
    virtual ~IOnvifService() = default;

    // URL path service này phụ trách, VD "/onvif/deviceIO", "/onvif/analytics".
    // ServiceRegistry route theo longest-prefix match.
    virtual std::string pathPrefix() const = 0;

    // Tên service (log/debug).
    virtual std::string name() const = 0;

    // Xử lý request → trả response XML. Trả "" nếu op không thuộc service này.
    virtual std::string handle(const std::string& rawRequest) = 0;

    // Service có yêu cầu authentication không (đa số = true).
    // OnvifServer/AuthMiddleware dùng để quyết định check auth trước dispatch.
    virtual bool requiresAuth() const { return true; }
};
