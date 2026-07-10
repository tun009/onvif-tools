#pragma once
// AnalyticsModuleStore — state analytics module DÙNG CHUNG giữa AnalyticsService
// (Analytics service /onvif/analytics) và Media2MetadataService (GetAnalyticsConfigurations
// trên /onvif/media). Tool tạo module qua Analytics → verify qua Media2 VAC → phải
// thấy cùng list. Singleton header-only, thread-safe.
//
// Bắt đầu RỖNG (không seed default) — tool tự Create rồi verify/Delete. Seed default
// gây nhiễu test Delete (module mặc định luôn tồn tại).

#include <string>
#include <map>
#include <mutex>
#include <vector>

struct AnalyticsModuleEntry {
    std::string name;
    std::string type;   // QName chuẩn hóa "tt:CellMotionEngine"
};

class AnalyticsModuleStore {
public:
    static AnalyticsModuleStore& instance() {
        static AnalyticsModuleStore s;
        return s;
    }

    void add(const std::string& name, const std::string& type) {
        std::lock_guard<std::mutex> lk(m_);
        modules_[name] = {name, type};
    }
    void remove(const std::string& name) {
        std::lock_guard<std::mutex> lk(m_);
        modules_.erase(name);
    }
    std::vector<AnalyticsModuleEntry> list() {
        std::lock_guard<std::mutex> lk(m_);
        std::vector<AnalyticsModuleEntry> out;
        for (const auto& kv : modules_) out.push_back(kv.second);
        return out;
    }

private:
    std::mutex m_;
    std::map<std::string, AnalyticsModuleEntry> modules_;
};
