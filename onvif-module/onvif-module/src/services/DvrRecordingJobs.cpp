// DvrRecordingJobs.cpp — Recording Job của DvrRecordingService (tạo/xóa/đổi/đọc job, trạng thái).
//
// Job do onvif-module giữ (RecordingJobStore). Mode Active/Idle điều khiển ghi TAY của DVR;
// trạng thái thật luôn đọc từ DVR. Mỗi Recording tối đa 1 job.
#include "services/DvrRecordingService.h"

#include "utils/FaultBuilder.h"

#include <cctype>
#include <cstdio>
#include <exception>

namespace {

const char* kProfileType = "http://www.onvif.org/ver10/schema/Profile";   // Type của SourceToken

bool validMode(const std::string& mode) { return mode == "Idle" || mode == "Active"; }

// xs:int dạng thập phân, tối đa 9 chữ số (đủ cho mọi mức ưu tiên hợp lý, tránh tràn).
bool validPriority(const std::string& value) {
    std::size_t i = (!value.empty() && value[0] == '-') ? 1 : 0;
    if (i >= value.size() || value.size() - i > 9) return false;
    for (; i < value.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(value[i]))) return false;
    return true;
}

std::string faultNotStarted() {
    return FaultBuilder::receiver("ter:Action", "Recording could not be started");
}

} // namespace

// Các trường client gửi trong <JobConfiguration>; trường vắng mặt để rỗng.
struct DvrRecordingService::JobFields {
    std::string recordingToken, mode, priority, sourceToken, sourceType;
};

DvrRecordingService::JobFields DvrRecordingService::parseJob(const std::string& scope) {
    JobFields f;
    f.recordingToken = unesc(textOf(scope, "RecordingToken"));
    f.mode = textOf(scope, "Mode");
    f.priority = textOf(scope, "Priority");
    const std::string source = blockOf(scope, "SourceToken");
    f.sourceToken = unesc(textOf(source, "Token"));
    f.sourceType = unesc(attrOf(source, "Type"));
    return f;
}

std::string DvrRecordingService::jobConfigXml(const RecordingJobRecord& job) const {
    return "<tt:RecordingToken>" + esc(job.recordingToken) + "</tt:RecordingToken>"
           "<tt:Mode>" + esc(job.mode) + "</tt:Mode>"
           "<tt:Priority>" + esc(job.priority) + "</tt:Priority>"
           "<tt:Source><tt:SourceToken Type=\"" + esc(job.sourceType) + "\">"
           "<tt:Token>" + esc(job.sourceToken) + "</tt:Token></tt:SourceToken></tt:Source>";
}

std::string DvrRecordingService::jobStateOf(const RecordingJobRecord& job, const std::vector<Source>& sources) {
    if (job.mode != "Active") return "Idle";
    const Source* source = findSource(sources, job.recordingToken);
    const Stream* stream = source ? source->byProfile(job.sourceToken) : nullptr;
    return (stream && stream->isRecording) ? "Active" : "Idle";
}

RecordingJobEvent DvrRecordingService::jobEvent(const RecordingJobRecord& job, const std::string& state) {
    return {job.token, job.recordingToken, job.sourceToken, job.sourceType, state};
}

std::vector<RecordingJobEvent> DvrRecordingService::initialJobEvents() const {
    // Chạy từ PullMessages, không giữ khóa nào của event manager nên được gọi xuống DVR.
    std::vector<Source> sources;
    std::string ignored;
    const bool known = loadSources(sources, ignored);   // DVR lỗi → mọi job báo Idle
    std::vector<RecordingJobEvent> events;
    for (const auto& job : store_->jobs())
        events.push_back(jobEvent(job, known ? jobStateOf(job, sources) : "Idle"));
    return events;
}

// ── Đọc ───────────────────────────────────────────────────────────────

std::string DvrRecordingService::getJobs(const std::string& rel) {
    std::string items;
    for (const auto& job : store_->jobs())
        items += "<trc:JobItem><tt:JobToken>" + esc(job.token) + "</tt:JobToken>"
                 "<tt:JobConfiguration>" + jobConfigXml(job) + "</tt:JobConfiguration></trc:JobItem>";
    return reply(rel, "GetRecordingJobsResponse", "<trc:GetRecordingJobsResponse>" + items + "</trc:GetRecordingJobsResponse>");
}

std::string DvrRecordingService::getJobConfiguration(const std::string& req, const std::string& rel) {
    RecordingJobRecord job;
    if (!store_->findJob(unesc(textOf(req, "JobToken")), job)) return faultNoJob();
    return reply(rel, "GetRecordingJobConfigurationResponse",
                 "<trc:GetRecordingJobConfigurationResponse><trc:JobConfiguration>" + jobConfigXml(job) +
                 "</trc:JobConfiguration></trc:GetRecordingJobConfigurationResponse>");
}

std::string DvrRecordingService::getJobState(const std::string& req, const std::string& rel) {
    RecordingJobRecord job;
    if (!store_->findJob(unesc(textOf(req, "JobToken")), job)) return faultNoJob();
    std::vector<Source> sources;
    std::string fault;
    if (!loadSources(sources, fault)) return fault;

    const std::string state = jobStateOf(job, sources);
    return reply(rel, "GetRecordingJobStateResponse",
                 "<trc:GetRecordingJobStateResponse><trc:State>"
                 "<tt:RecordingToken>" + esc(job.recordingToken) + "</tt:RecordingToken>"
                 "<tt:State>" + state + "</tt:State>"
                 "<tt:Sources><tt:SourceToken Type=\"" + esc(job.sourceType) + "\">"
                 "<tt:Token>" + esc(job.sourceToken) + "</tt:Token></tt:SourceToken>"
                 "<tt:State>" + state + "</tt:State><tt:Tracks/></tt:Sources>"
                 "</trc:State></trc:GetRecordingJobStateResponse>");
}

// ── Ghi ───────────────────────────────────────────────────────────────

std::string DvrRecordingService::createJob(const std::string& req, const std::string& rel) {
    std::lock_guard<std::mutex> lock(mutateMutex_);
    const std::string scope = blockOf(req, "JobConfiguration");
    if (scope.empty()) return faultBadConfig("Missing JobConfiguration");
    JobFields f = parseJob(scope);

    std::vector<Source> sources;
    std::string fault;
    if (!loadSources(sources, fault)) return fault;
    const Source* source = findSource(sources, f.recordingToken);
    if (!source) return faultNoRecording();

    if (!validMode(f.mode)) return faultBadConfig("Invalid Mode");
    if (f.priority.empty()) f.priority = "0";
    if (!validPriority(f.priority)) return faultBadConfig("Invalid Priority");
    // Spec: từ chối cấu hình không có nguồn (AutoCreateReceiver không được hỗ trợ).
    if (f.sourceToken.empty()) return faultBadConfig("Missing source token");
    if (f.sourceType.empty()) f.sourceType = kProfileType;
    if (f.sourceType != kProfileType) return faultBadConfig("Only Media profiles can be recorded");
    const Stream* stream = source->byProfile(f.sourceToken);
    if (!stream) return faultBadConfig("Source is not compatible with the recording");

    RecordingJobRecord existing;
    if (store_->findJobByRecording(source->recordingToken, existing))
        return FaultBuilder::receiver("ter:Action", "Maximum number of recording jobs reached");

    if (f.mode == "Active") {
        try {
            setManualRecord(*source, *stream, true);
        } catch (const RefusedError& e) {
            fprintf(stderr, "[DvrRecording] CreateRecordingJob refused: %s\n", e.what());
            return faultNotStarted();
        } catch (const std::exception& e) {
            fprintf(stderr, "[DvrRecording] CreateRecordingJob failed: %s\n", e.what());
            return faultBackend();
        }
    }

    RecordingJobRecord job;
    job.recordingToken = source->recordingToken;
    job.mode = f.mode;
    job.priority = f.priority;
    job.sourceToken = f.sourceToken;
    job.sourceType = f.sourceType;
    job = store_->addJob(job);

    MockSubscriptionManager::getInstance().fireRecordingJobState(jobEvent(job, job.mode));
    return reply(rel, "CreateRecordingJobResponse",
                 "<trc:CreateRecordingJobResponse><trc:JobToken>" + esc(job.token) + "</trc:JobToken>"
                 "<trc:JobConfiguration>" + jobConfigXml(job) + "</trc:JobConfiguration>"
                 "</trc:CreateRecordingJobResponse>");
}

std::string DvrRecordingService::deleteJob(const std::string& req, const std::string& rel) {
    std::lock_guard<std::mutex> lock(mutateMutex_);
    RecordingJobRecord job;
    if (!store_->findJob(unesc(textOf(req, "JobToken")), job)) return faultNoJob();

    // Xóa job phải dừng việc ghi do job gây ra. Không dừng được thì giữ nguyên job để client
    // thử lại, không để lại một lần ghi không còn job nào quản lý.
    if (job.mode == "Active") {
        std::vector<Source> sources;
        std::string fault;
        if (!loadSources(sources, fault)) return fault;
        const Source* source = findSource(sources, job.recordingToken);
        const Stream* stream = source ? source->byProfile(job.sourceToken) : nullptr;
        if (stream) {
            try {
                setManualRecord(*source, *stream, false);
            } catch (const std::exception& e) {
                fprintf(stderr, "[DvrRecording] DeleteRecordingJob could not stop recording: %s\n", e.what());
                return faultBackend();
            }
        }
    }
    store_->removeJob(job.token);
    return reply(rel, "DeleteRecordingJobResponse", "<trc:DeleteRecordingJobResponse/>");
}

std::string DvrRecordingService::setJobMode(const std::string& req, const std::string& rel) {
    std::lock_guard<std::mutex> lock(mutateMutex_);
    RecordingJobRecord job;
    if (!store_->findJob(unesc(textOf(req, "JobToken")), job)) return faultNoJob();
    const std::string mode = textOf(req, "Mode");
    if (!validMode(mode)) return FaultBuilder::sender("ter:InvalidArgVal", "ter:BadMode", "The Mode is invalid");

    std::vector<Source> sources;
    std::string fault;
    if (!loadSources(sources, fault)) return fault;
    const Source* source = findSource(sources, job.recordingToken);
    const Stream* stream = source ? source->byProfile(job.sourceToken) : nullptr;

    try {
        if (mode == "Active") {
            if (!stream) return faultBadConfig("Job source no longer exists");
            setManualRecord(*source, *stream, true);
        } else if (job.mode == "Active" && stream) {
            setManualRecord(*source, *stream, false);
        }
    } catch (const RefusedError& e) {
        fprintf(stderr, "[DvrRecording] SetRecordingJobMode refused: %s\n", e.what());
        return faultNotStarted();
    } catch (const std::exception& e) {
        fprintf(stderr, "[DvrRecording] SetRecordingJobMode failed: %s\n", e.what());
        return faultBackend();
    }

    job.mode = mode;
    store_->updateJob(job);
    MockSubscriptionManager::getInstance().fireRecordingJobState(jobEvent(job, job.mode));
    return reply(rel, "SetRecordingJobModeResponse", "<trc:SetRecordingJobModeResponse/>");
}

std::string DvrRecordingService::setJobConfiguration(const std::string& req, const std::string& rel) {
    std::lock_guard<std::mutex> lock(mutateMutex_);
    RecordingJobRecord job;
    if (!store_->findJob(unesc(textOf(req, "JobToken")), job)) return faultNoJob();
    const std::string scope = blockOf(req, "JobConfiguration");
    if (scope.empty()) return faultBadConfig("Missing JobConfiguration");
    const JobFields f = parseJob(scope);

    // Spec: phải từ chối yêu cầu đổi RecordingToken của job.
    if (!f.recordingToken.empty() && f.recordingToken != job.recordingToken)
        return faultBadConfig("RecordingToken of a job cannot be changed");

    RecordingJobRecord updated = job;
    if (!f.mode.empty()) updated.mode = f.mode;
    if (!f.priority.empty()) updated.priority = f.priority;
    if (!f.sourceToken.empty()) updated.sourceToken = f.sourceToken;
    if (!f.sourceType.empty()) updated.sourceType = f.sourceType;
    if (!validMode(updated.mode)) return faultBadConfig("Invalid Mode");
    if (!validPriority(updated.priority)) return faultBadConfig("Invalid Priority");
    if (updated.sourceType != kProfileType) return faultBadConfig("Only Media profiles can be recorded");

    std::vector<Source> sources;
    std::string fault;
    if (!loadSources(sources, fault)) return fault;
    const Source* source = findSource(sources, job.recordingToken);
    if (!source) return faultNoRecording();
    const Stream* newStream = source->byProfile(updated.sourceToken);
    if (!newStream) return faultBadConfig("Source is not compatible with the recording");
    const Stream* oldStream = source->byProfile(job.sourceToken);

    const std::string oldState = jobStateOf(job, sources);
    const bool oldActive = job.mode == "Active";
    const bool newActive = updated.mode == "Active";
    const bool sourceChanged = updated.sourceToken != job.sourceToken;
    try {
        // Bật luồng mới TRƯỚC: DVR tự tắt ghi tay của luồng kia, và nếu bật thất bại thì luồng
        // cũ chưa bị đụng tới.
        if (newActive) setManualRecord(*source, *newStream, true);
        if (oldActive && oldStream && (!newActive || sourceChanged)) setManualRecord(*source, *oldStream, false);
    } catch (const RefusedError& e) {
        fprintf(stderr, "[DvrRecording] SetRecordingJobConfiguration refused: %s\n", e.what());
        return faultNotStarted();
    } catch (const std::exception& e) {
        fprintf(stderr, "[DvrRecording] SetRecordingJobConfiguration failed: %s\n", e.what());
        return faultBackend();
    }

    store_->updateJob(updated);
    auto& events = MockSubscriptionManager::getInstance();
    events.fireRecordingConfigChanged(
        "RecordingJobConfiguration",
        "<tt:SimpleItem Name=\"RecordingJobToken\" Value=\"" + updated.token + "\"/>",
        "<tt:RecordingJobConfiguration>" + jobConfigXml(updated) + "</tt:RecordingJobConfiguration>");
    const std::string newState = newActive ? "Active" : "Idle";
    if (newState != oldState) events.fireRecordingJobState(jobEvent(updated, newState));

    return reply(rel, "SetRecordingJobConfigurationResponse",
                 "<trc:SetRecordingJobConfigurationResponse><trc:JobConfiguration>" + jobConfigXml(updated) +
                 "</trc:JobConfiguration></trc:SetRecordingJobConfigurationResponse>");
}
