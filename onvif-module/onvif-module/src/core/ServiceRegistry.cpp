#include "core/ServiceRegistry.h"

void ServiceRegistry::registerService(std::unique_ptr<IOnvifService> svc) {
    services_.push_back(std::move(svc));
}

IOnvifService* ServiceRegistry::route(const std::string& path) const {
    // Longest-prefix match: tránh nhầm /onvif/media vs /onvif/media2 khi
    // nhiều service có prefix lồng nhau. Chọn service có pathPrefix dài nhất
    // mà path chứa được.
    IOnvifService* best = nullptr;
    std::size_t bestLen = 0;
    for (const auto& svc : services_) {
        const std::string& prefix = svc->pathPrefix();
        if (path.find(prefix) != std::string::npos && prefix.size() > bestLen) {
            best = svc.get();
            bestLen = prefix.size();
        }
    }
    return best;
}

std::vector<IOnvifService*> ServiceRegistry::matching(const std::string& path) const {
    std::vector<IOnvifService*> out;
    for (const auto& svc : services_) {
        if (path.find(svc->pathPrefix()) != std::string::npos)
            out.push_back(svc.get());
    }
    return out;
}

std::vector<IOnvifService*> ServiceRegistry::all() const {
    std::vector<IOnvifService*> out;
    out.reserve(services_.size());
    for (const auto& s : services_) out.push_back(s.get());
    return out;
}
