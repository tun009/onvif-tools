#pragma once
// ServiceRegistry — đăng ký + route ONVIF service theo URL path.
//
// Chuẩn kiến trúc: xem docs/10-refactor-plan-multi-profile.md PHẦN II.
// Thay thế God-Object if-else routing trong OnvifServer. Thêm service mới =
// registerService() trong main/OnvifServer, KHÔNG sửa logic route.

#include "core/IOnvifService.h"
#include <memory>
#include <vector>
#include <string>

class ServiceRegistry {
public:
    // Đăng ký 1 service. Registry sở hữu (own) service qua unique_ptr.
    void registerService(std::unique_ptr<IOnvifService> svc);

    // Tìm service phụ trách path (longest-prefix match). nullptr nếu không có.
    IOnvifService* route(const std::string& path) const;

    // Chain of responsibility: trả TẤT CẢ service khớp prefix (theo thứ tự
    // đăng ký). OnvifServer thử lần lượt đến khi 1 service trả non-empty.
    // Cho phép nhiều service cùng prefix (VD /onvif/media: MediaLegacyService
    // xử lý Media1, Media2MetadataService xử lý metadata/analytics Media2).
    std::vector<IOnvifService*> matching(const std::string& path) const;

    // Liệt kê tất cả service đã đăng ký (cho GetServices sau này).
    std::vector<IOnvifService*> all() const;

private:
    std::vector<std::unique_ptr<IOnvifService>> services_;
};
