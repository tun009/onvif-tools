#include "services/RecordingJobStore.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <utility>

// Định dạng file: mỗi dòng 1 bản ghi, các trường cách nhau bằng TAB, ký tự đặc biệt
// trong giá trị được thoát (\\, \t, \n, \r):
//   N <số job kế tiếp>
//   J <token> <recording> <mode> <priority> <source> <sourceType>
//   C <recording> <sourceId> <name> <location> <description> <address> <content> <retention>
//   T <recording> <track> <description>
namespace {

std::string escapeField(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '\t': out += "\\t";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            default:   out += ch;
        }
    }
    return out;
}

std::string unescapeField(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\' || i + 1 >= value.size()) { out += value[i]; continue; }
        switch (value[++i]) {
            case 't': out += '\t'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            default:  out += value[i];
        }
    }
    return out;
}

std::vector<std::string> splitFields(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0;
    for (;;) {
        const auto tab = line.find('\t', start);
        if (tab == std::string::npos) { fields.push_back(unescapeField(line.substr(start))); break; }
        fields.push_back(unescapeField(line.substr(start, tab - start)));
        start = tab + 1;
    }
    return fields;
}

std::string joinFields(const std::vector<std::string>& fields) {
    std::string line;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i) line += '\t';
        line += escapeField(fields[i]);
    }
    return line;
}

// "job_12" -> 12; khác dạng đó -> 0.
unsigned long jobNumber(const std::string& token) {
    if (token.rfind("job_", 0) != 0 || token.size() == 4) return 0;
    char* end = nullptr;
    const unsigned long n = std::strtoul(token.c_str() + 4, &end, 10);
    return (end && *end == '\0') ? n : 0;
}

} // namespace

RecordingJobStore::RecordingJobStore(std::string path) : path_(std::move(path)) {
    if (path_.empty()) {
        fprintf(stderr, "[RecordingStore] No store path configured: recording jobs and "
                        "configuration will NOT survive a restart\n");
        return;
    }
    load();
}

void RecordingJobStore::load() {
    std::ifstream in(path_);
    if (!in) {
        fprintf(stderr, "[RecordingStore] %s not found; starting with an empty store\n", path_.c_str());
        return;
    }
    std::string line;
    unsigned long declaredNext = 1;
    int bad = 0;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const auto f = splitFields(line);
        if (f[0] == "N" && f.size() == 2) {
            declaredNext = std::strtoul(f[1].c_str(), nullptr, 10);
        } else if (f[0] == "J" && f.size() == 7) {
            jobs_.push_back({f[1], f[2], f[3], f[4], f[5], f[6]});
        } else if (f[0] == "C" && f.size() == 9) {
            configs_[f[1]] = {f[2], f[3], f[4], f[5], f[6], f[7], f[8]};
        } else if (f[0] == "T" && f.size() == 4) {
            trackDescriptions_[f[1] + "|" + f[2]] = f[3];
        } else {
            ++bad;
        }
    }
    nextJobNumber_ = declaredNext ? declaredNext : 1;
    for (const auto& job : jobs_)
        if (jobNumber(job.token) >= nextJobNumber_) nextJobNumber_ = jobNumber(job.token) + 1;
    printf("[RecordingStore] Loaded %zu job(s), %zu recording config(s) from %s%s\n", jobs_.size(),
           configs_.size(), path_.c_str(), bad ? " (some lines skipped)" : "");
}

void RecordingJobStore::saveLocked() const {
    if (path_.empty()) return;
    std::ostringstream os;
    os << joinFields({"N", std::to_string(nextJobNumber_)}) << '\n';
    for (const auto& j : jobs_)
        os << joinFields({"J", j.token, j.recordingToken, j.mode, j.priority, j.sourceToken, j.sourceType}) << '\n';
    for (const auto& kv : configs_) {
        const auto& c = kv.second;
        os << joinFields({"C", kv.first, c.sourceId, c.name, c.location, c.description, c.address,
                          c.content, c.maxRetention}) << '\n';
    }
    for (const auto& kv : trackDescriptions_) {
        const auto bar = kv.first.find('|');
        os << joinFields({"T", kv.first.substr(0, bar), kv.first.substr(bar + 1), kv.second}) << '\n';
    }

    // Ghi file tạm rồi rename: mất điện giữa chừng chỉ để lại file tạm, file chính vẫn nguyên.
    const std::string tmp = path_ + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << os.str();
        out.flush();
        if (!out) {
            fprintf(stderr, "[RecordingStore] Cannot write %s: change kept in memory only\n", tmp.c_str());
            return;
        }
    }
    if (std::rename(tmp.c_str(), path_.c_str()) != 0)
        fprintf(stderr, "[RecordingStore] Cannot replace %s: change kept in memory only\n", path_.c_str());
}

std::vector<RecordingJobRecord> RecordingJobStore::jobs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return jobs_;
}

bool RecordingJobStore::findJob(const std::string& token, RecordingJobRecord& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& job : jobs_)
        if (job.token == token) { out = job; return true; }
    return false;
}

bool RecordingJobStore::findJobByRecording(const std::string& recordingToken,
                                           RecordingJobRecord& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& job : jobs_)
        if (job.recordingToken == recordingToken) { out = job; return true; }
    return false;
}

RecordingJobRecord RecordingJobStore::addJob(RecordingJobRecord job) {
    std::lock_guard<std::mutex> lock(mutex_);
    job.token = "job_" + std::to_string(nextJobNumber_++);
    jobs_.push_back(job);
    saveLocked();
    return job;
}

bool RecordingJobStore::updateJob(const RecordingJobRecord& job) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& existing : jobs_) {
        if (existing.token != job.token) continue;
        existing = job;
        saveLocked();
        return true;
    }
    return false;
}

bool RecordingJobStore::removeJob(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = jobs_.begin(); it != jobs_.end(); ++it) {
        if (it->token != token) continue;
        jobs_.erase(it);
        saveLocked();
        return true;
    }
    return false;
}

bool RecordingJobStore::getConfig(const std::string& recordingToken, RecordingConfigRecord& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = configs_.find(recordingToken);
    if (found == configs_.end()) return false;
    out = found->second;
    return true;
}

void RecordingJobStore::setConfig(const std::string& recordingToken, const RecordingConfigRecord& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    configs_[recordingToken] = config;
    saveLocked();
}

bool RecordingJobStore::getTrackDescription(const std::string& recordingToken,
                                            const std::string& trackToken, std::string& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = trackDescriptions_.find(recordingToken + "|" + trackToken);
    if (found == trackDescriptions_.end()) return false;
    out = found->second;
    return true;
}

void RecordingJobStore::setTrackDescription(const std::string& recordingToken,
                                            const std::string& trackToken,
                                            const std::string& description) {
    std::lock_guard<std::mutex> lock(mutex_);
    trackDescriptions_[recordingToken + "|" + trackToken] = description;
    saveLocked();
}
