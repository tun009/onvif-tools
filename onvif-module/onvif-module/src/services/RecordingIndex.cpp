// RecordingIndex.cpp — quét thư mục file ghi của DVR, dựng danh mục khoảng thời gian có dữ liệu.
#include "services/RecordingIndex.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <utility>

namespace {

// Thư mục con của DVR → VideoSourceId. DVR không công bố quy ước này qua REST; đây là bố cục
// quan sát được trên máy thật (context = sensor 0, alpr = sensor 1). Thư mục "overlay" là nguồn
// ảo, không có file.
const struct { const char* dir; const char* videoSourceId; } kSourceDirs[] = {
    {"context", "0"},
    {"alpr", "1"},
};

int64_t steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

uint32_t be32(const unsigned char* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

// Thời lượng (ms) từ hộp mvhd của MP4 đã +faststart (moov nằm trước mdat nên chỉ cần đọc đầu
// file). 0 nếu không đọc được (file đang ghi dở chưa có moov, hoặc không phải MP4).
int64_t mp4DurationMs(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return 0;
    unsigned char buf[4096];
    const size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);

    for (size_t at = 0; at + 8 <= n;) {   // hộp cấp cao nhất: size(4) type(4)
        uint64_t size = be32(buf + at);
        const bool moov = std::equal(buf + at + 4, buf + at + 8, "moov");
        if (moov) {
            const size_t m = at + 8;   // hộp con đầu tiên của moov phải là mvhd
            if (m + 32 > n || !std::equal(buf + m + 4, buf + m + 8, "mvhd")) return 0;
            const unsigned version = buf[m + 8];
            uint64_t timescale = 0, duration = 0;
            if (version == 0 && m + 28 <= n) {
                timescale = be32(buf + m + 20);
                duration = be32(buf + m + 24);
            } else if (version == 1 && m + 40 <= n) {
                timescale = be32(buf + m + 28);
                duration = (uint64_t(be32(buf + m + 32)) << 32) | be32(buf + m + 36);
            }
            if (timescale == 0 || duration == 0) return 0;
            const int64_t ms = static_cast<int64_t>(duration * 1000 / timescale);
            return ms > 0 && ms <= 24LL * 3600 * 1000 ? ms : 0;   // loại giá trị rác
        }
        if (size < 8) return 0;           // size 0/1 (mở rộng/đến cuối file) không gặp ở đầu file DVR
        at += static_cast<size_t>(size);
    }
    return 0;
}

} // namespace

RecordingIndex::RecordingIndex(std::string rootDir) : root_(std::move(rootDir)) {
    while (root_.size() > 1 && root_.back() == '/') root_.pop_back();
}

void RecordingIndex::refreshLocked() {
    std::map<std::string, Entry> seen;
    for (const auto& source : kSourceDirs) {
        const std::string dir = root_ + "/" + source.dir;
        DIR* d = opendir(dir.c_str());
        if (!d) continue;
        while (const dirent* e = readdir(d)) {
            const std::string name = e->d_name;
            if (name.size() < 5 || name.compare(name.size() - 4, 4, ".mp4") != 0) continue;
            const char* stream = name.find("_main_") != std::string::npos ? "main"
                               : name.find("_sub_") != std::string::npos ? "sub" : nullptr;
            if (!stream) continue;
            const std::string path = dir + "/" + name;
            struct stat st;
            if (stat(path.c_str(), &st) != 0) continue;

            Entry entry;
            entry.videoSourceId = source.videoSourceId;
            entry.streamType = stream;
            entry.mtimeMs = static_cast<int64_t>(st.st_mtim.tv_sec) * 1000 + st.st_mtim.tv_nsec / 1000000;
            entry.size = st.st_size;
            const auto cached = files_.find(path);
            if (cached != files_.end() && cached->second.mtimeMs == entry.mtimeMs &&
                cached->second.size == entry.size) {
                entry.durationMs = cached->second.durationMs;   // không đổi → khỏi đọc lại
            } else {
                entry.durationMs = mp4DurationMs(path);
            }
            seen.emplace(path, std::move(entry));
        }
        closedir(d);
    }
    files_ = std::move(seen);   // file DVR đã xóa tự biến mất khỏi danh mục
}

std::vector<TimeRange> RecordingIndex::ranges(const std::string& videoSourceId, const std::string& streamType) {
    std::lock_guard<std::mutex> lock(mutex_);
    const int64_t now = steadyNowMs();
    if (lastRefreshMs_ == 0 || now - lastRefreshMs_ >= kRefreshMs) {
        refreshLocked();
        lastRefreshMs_ = now;
    }

    std::vector<TimeRange> segments;
    for (const auto& kv : files_) {
        const Entry& e = kv.second;
        if (e.durationMs <= 0 || e.videoSourceId != videoSourceId || e.streamType != streamType) continue;
        segments.push_back({e.mtimeMs - e.durationMs, e.mtimeMs});
    }
    std::sort(segments.begin(), segments.end(),
              [](const TimeRange& a, const TimeRange& b) { return a.fromMs < b.fromMs; });

    std::vector<TimeRange> merged;
    for (const auto& s : segments) {
        if (!merged.empty() && s.fromMs - merged.back().toMs <= kGapMs)
            merged.back().toMs = std::max(merged.back().toMs, s.toMs);
        else
            merged.push_back(s);
    }
    return merged;
}
