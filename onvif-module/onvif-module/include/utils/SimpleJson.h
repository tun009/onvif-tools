#pragma once
// Minimal JSON value extractor - no external dependency
// Only handles flat JSON objects with string/int/float/bool values
#include <string>
#include <cstdlib>

namespace SimpleJson {

inline std::string getString(const std::string& json, const std::string& key,
                              const std::string& def = "") {
    std::string search = "\"" + key + "\":\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return def;
    pos += search.size();
    auto end = json.find('"', pos);
    if (end == std::string::npos) return def;
    return json.substr(pos, end - pos);
}

inline int getInt(const std::string& json, const std::string& key, int def = 0) {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return def;
    pos += search.size();
    while (pos < json.size() && (json[pos] == ' ')) ++pos;
    if (pos >= json.size()) return def;
    return std::atoi(json.c_str() + pos);
}

inline float getFloat(const std::string& json, const std::string& key,
                       float def = 0.0f) {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return def;
    pos += search.size();
    while (pos < json.size() && json[pos] == ' ') ++pos;
    if (pos >= json.size()) return def;
    return (float)std::atof(json.c_str() + pos);
}

inline bool getBool(const std::string& json, const std::string& key,
                    bool def = false) {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return def;
    pos += search.size();
    while (pos < json.size() && json[pos] == ' ') ++pos;
    if (pos >= json.size()) return def;
    return json.substr(pos, 4) == "true";
}

} // namespace SimpleJson
