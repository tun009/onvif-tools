#pragma once
// SearchSupport — hàm thuần dùng riêng cho DvrSearchService: thời gian xs:dateTime / xs:duration,
// cắt phần tử XML lặp lại, bộ lọc topic và bộ lọc RecordingInformation (XPath dialect của Search).
// Header-only để DvrSearchService.cpp gọn dưới 600 dòng; không phải API dùng chung.

#include "services/DvrRecordingService.h"
#include "services/RecordingIndex.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iterator>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace searchsupport {

using Svc = DvrRecordingService;

inline int64_t epochNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}
inline int64_t steadyNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline int64_t floorSec(int64_t ms) { return ms >= 0 ? ms / 1000 * 1000 : -((-ms + 999) / 1000) * 1000; }
inline int64_t ceilSec(int64_t ms) { return floorSec(ms + 999); }

// xs:dateTime → mili giây epoch UTC. Chấp nhận phần thập phân và múi giờ Z / ±hh:mm (không có = UTC).
inline bool parseDateTime(const std::string& text, int64_t& ms) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, used = 0;
    if (std::sscanf(text.c_str(), "%d-%d-%dT%d:%d:%n", &y, &mo, &d, &h, &mi, &used) != 5 || used == 0) return false;
    char* end = nullptr;
    const double sec = std::strtod(text.c_str() + used, &end);
    if (end == text.c_str() + used || sec < 0 || sec >= 61) return false;
    int64_t offsetMin = 0;
    if (*end == '+' || *end == '-') {
        int oh = 0, om = 0;
        if (std::sscanf(end + 1, "%d:%d", &oh, &om) != 2) return false;
        offsetMin = (oh * 60 + om) * (*end == '-' ? -1 : 1);
    } else if (*end != 'Z' && *end != '\0') {
        return false;
    }
    std::tm tm{};
    tm.tm_year = y - 1900; tm.tm_mon = mo - 1; tm.tm_mday = d; tm.tm_hour = h; tm.tm_min = mi;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 24 || mi > 59) return false;
    const int64_t whole = static_cast<int64_t>(timegm(&tm));
    ms = (whole - offsetMin * 60) * 1000 + static_cast<int64_t>((sec - static_cast<int64_t>(sec)) * 1000 + 0.5) +
         static_cast<int64_t>(sec) * 1000;
    return true;
}

inline std::string formatTime(int64_t ms) {   // cắt xuống giây, luôn có hậu tố Z (spec 5.2.6)
    const std::time_t t = static_cast<std::time_t>(floorSec(ms) / 1000);
    std::tm tm{};
    gmtime_r(&t, &tm);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

// xs:duration ("PT10S", "PT0.5S", "P1DT2H") → mili giây; -1 nếu sai dạng.
inline int64_t parseDurationMs(const std::string& text) {
    if (text.size() < 2 || text[0] != 'P') return -1;
    double total = 0;
    bool inTime = false, any = false;
    for (std::size_t i = 1; i < text.size();) {
        if (text[i] == 'T') { inTime = true; ++i; continue; }
        char* end = nullptr;
        const double value = std::strtod(text.c_str() + i, &end);
        if (end == text.c_str() + i || *end == '\0' || value < 0) return -1;
        double unit = 0;
        switch (*end) {
            case 'Y': unit = inTime ? 0 : 365.0 * 86400000; break;
            case 'D': unit = inTime ? 0 : 86400000.0; break;
            case 'H': unit = inTime ? 3600000.0 : 0; break;
            case 'S': unit = inTime ? 1000.0 : 0; break;
            case 'M': unit = inTime ? 60000.0 : 30.0 * 86400000; break;
            default: break;
        }
        if (unit == 0) return -1;
        total += value * unit;
        any = true;
        i = static_cast<std::size_t>(end - text.c_str()) + 1;
    }
    return any ? static_cast<int64_t>(total) : -1;
}

inline std::string trimmed(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

// Mọi phần tử tên `name` (nội dung văn bản, bỏ phần tử rỗng).
inline std::vector<std::string> allTexts(const std::string& xml, const std::string& name) {
    std::vector<std::string> out;
    std::string rest = xml;
    for (;;) {
        const std::string block = Svc::blockOf(rest, name);
        if (block.empty()) break;
        const std::string value = trimmed(Svc::unesc(Svc::textOf(block, name)));
        if (!value.empty()) out.push_back(value);
        rest = rest.substr(rest.find(block) + block.size());
    }
    return out;
}

inline std::vector<std::string> allBlocks(const std::string& xml, const std::string& name) {
    std::vector<std::string> out;
    std::string rest = xml;
    for (;;) {
        const std::string block = Svc::blockOf(rest, name);
        if (block.empty()) break;
        out.push_back(block);
        rest = rest.substr(rest.find(block) + block.size());
    }
    return out;
}

// Nối các đoạn cách nhau ≤ RecordingIndex::kGapMs.
inline std::vector<TimeRange> mergeRanges(std::vector<TimeRange> in) {
    std::sort(in.begin(), in.end(), [](const TimeRange& a, const TimeRange& b) { return a.fromMs < b.fromMs; });
    std::vector<TimeRange> out;
    for (const auto& r : in) {
        if (!out.empty() && r.fromMs - out.back().toMs <= RecordingIndex::kGapMs)
            out.back().toMs = std::max(out.back().toMs, r.toMs);
        else
            out.push_back(r);
    }
    return out;
}

inline bool containsTime(const std::vector<TimeRange>& ranges, int64_t t) {
    for (const auto& r : ranges) if (r.fromMs <= t && t <= r.toMs) return true;
    return false;
}

// Bộ lọc topic của FindEvents: rỗng = mọi topic; "a|b" hợp; "x//." tiền tố; còn lại khớp đúng.
inline bool topicMatches(const std::string& expression, const std::string& topic) {
    if (trimmed(expression).empty()) return true;
    std::istringstream parts(expression);
    for (std::string part; std::getline(parts, part, '|');) {
        part = trimmed(part);
        const auto colon = part.find(':');
        if (colon != std::string::npos) part = part.substr(colon + 1);   // bỏ "tns1:"
        if (part.size() >= 3 && part.compare(part.size() - 3, 3, "//.") == 0) {
            const std::string prefix = part.substr(0, part.size() - 3);
            if (prefix.empty() || topic == prefix || topic.compare(0, prefix.size() + 1, prefix + "/") == 0) return true;
        } else if (part == topic) {
            return true;
        }
    }
    return false;
}

// Trình phân tích dialect bộ lọc RecordingInformation (spec Search §5.18) — phần DTT dùng:
//   boolean(//Track[TrackType = "Video"]) kết hợp bằng and / or / not( ) / ( ).
struct FilterParser {
    const std::string& s;
    const std::set<std::string>& types;
    std::size_t pos = 0;
    bool ok = true;

    void ws() { while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos; }
    bool accept(const char* lit) {
        ws();
        const std::size_t n = std::char_traits<char>::length(lit);
        if (s.compare(pos, n, lit) != 0) return false;
        pos += n;
        return true;
    }
    void need(const char* lit) { if (!accept(lit)) ok = false; }

    bool orExpr() {
        bool value = andExpr();
        while (ok && wordAhead("or")) { pos += 2; const bool rhs = andExpr(); value = value || rhs; }
        return value;
    }
    bool andExpr() {
        bool value = unary();
        while (ok && wordAhead("and")) { pos += 3; const bool rhs = unary(); value = value && rhs; }
        return value;
    }
    bool wordAhead(const char* word) {
        ws();
        const std::size_t n = std::char_traits<char>::length(word);
        return s.compare(pos, n, word) == 0 && pos + n < s.size() && !std::isalnum(static_cast<unsigned char>(s[pos + n]));
    }
    bool unary() {
        if (accept("not")) { need("("); const bool v = orExpr(); need(")"); return !v; }
        if (accept("(")) { const bool v = orExpr(); need(")"); return v; }
        need("boolean"); need("("); need("//Track"); need("["); need("TrackType"); need("=");
        ws();
        if (pos >= s.size() || (s[pos] != '"' && s[pos] != '\'')) { ok = false; return false; }
        const char quote = s[pos++];
        const std::size_t close = s.find(quote, pos);
        if (close == std::string::npos) { ok = false; return false; }
        const std::string type = s.substr(pos, close - pos);
        pos = close + 1;
        need("]"); need(")");
        return types.count(type) > 0;
    }
};

} // namespace searchsupport
