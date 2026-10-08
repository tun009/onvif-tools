// DvrRecordingJobs.cpp — Recording Job của DvrRecordingService (tạo/xóa/đổi/đọc job, trạng thái).
//
// Job do onvif-module giữ (RecordingJobStore). Mode Active/Idle điều khiển ghi TAY của DVR;
// trạng thái thật luôn đọc từ DVR. Mỗi Recording tối đa 1 job. Ngoài job đã lưu còn có job
// "quan sát được": luồng đang ghi (từ web hoặc lịch) mà recording đó chưa có job nào.
#include "services/DvrRecordingService.h"

#include "utils/FaultBuilder.h"

#include <cctype>
#include <cstdio>
#include <exception>

namespace {

const char* kProfileType = "http://www.onvif.org/ver10/schema/Profile";   // Type của SourceToken
const char* kAutoPrefix = "auto_";                                        // token job quan sát được

bool validMode(const std::string& mode) { return mode == "Idle" || mode == "Active"; }

// Spec: Priority là số không âm (0 = thấp nhất). Tối đa 9 chữ số để không tràn.
bool validPriority(const std::string& value) {
    if (value.empty() || value.size() > 9) return false;
    for (const char ch : value)
        if (!std::isdigit(static_cast<unsigned char>(ch))) return false;
    return true;
}

std::string faultNotStarted() {
    return FaultBuilder::receiver("ter:Action", "Recording could not be started");
}

// Phần tử Tracks trong RecordingJobStateInformation: nguồn → track đích kèm trạng thái.
std::string trackStateXml(const std::string& trackToken, const std::string& state) {
    if (trackToken.empty()) return "<tt:Tracks/>";
    return "<tt:Tracks><tt:SourceTag>video</tt:SourceTag><tt:Destination>" + trackToken +
           "</tt:Destination><tt:State>" + state + "</tt:State></tt:Tracks>";
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

std::string DvrRecordingService::trackOf(const RecordingJobRecord& job, const std::vector<Source>& sources) {
    const Source* source = findSource(sources, job.recordingToken);
    const Stream* stream = source ? source->byProfile(job.sourceToken) : nullptr;
    return stream ? stream->trackToken : "";
}

std::string DvrRecordingService::jobConfigXml(const RecordingJobRecord& job,
                                              const std::vector<Source>* sources) const {
    // Spec: thiết bị trả cấu hình đầy đủ gồm track đích. Job của ta ghi đúng 1 luồng vào 1 track.
    const std::string track = sources ? trackOf(job, *sources) : "";
    const std::string tracks = track.empty() ? "" :
        "<tt:Tracks><tt:SourceTag>video</tt:SourceTag><tt:Destination>" + esc(track) + "</tt:Destination></tt:Tracks>";
    return "<tt:RecordingToken>" + esc(job.recordingToken) + "</tt:RecordingToken>"
           "<tt:Mode>" + esc(job.mode) + "</tt:Mode>"
           "<tt:Priority>" + esc(job.priority) + "</tt:Priority>"
           "<tt:Source><tt:SourceToken Type=\"" + esc(job.sourceType) + "\">"
           "<tt:Token>" + esc(job.sourceToken) + "</tt:Token></tt:SourceToken>" + tracks + "</tt:Source>";
}

std::string DvrRecordingService::jobStateOf(const RecordingJobRecord& job, const std::vector<Source>& sources) {
    if (job.mode != "Active") return "Idle";
    const Source* source = findSource(sources, job.recordingToken);
    const Stream* stream = source ? source->byProfile(job.sourceToken) : nullptr;
    return (stream && stream->isRecording) ? "Active" : "Idle";
}

RecordingJobEvent DvrRecordingService::jobEvent(const RecordingJobRecord& job, const std::string& state,
                                                const std::string& trackToken) {
    return {job.token, job.recordingToken, job.sourceToken, job.sourceType, state, trackToken};
}

// ── Danh sách job: đã lưu + quan sát được ─────────────────────────────

std::vector<DvrRecordingService::JobView> DvrRecordingService::listJobs(const std::vector<Source>& sources) const {
    std::vector<JobView> views;
    for (const auto& job : store_->jobs()) views.push_back({job, false});

    // Recording chưa có job nào mà đang có luồng ghi → hiện 1 job quan sát được cho luồng đó
    // (luồng đầu tiên đang ghi nếu có nhiều luồng, vì mỗi recording tối đa 1 job).
    for (const auto& source : sources) {
        RecordingJobRecord stored;
        if (store_->findJobByRecording(source.recordingToken, stored)) continue;
        for (const auto& stream : source.streams) {
            if (!stream.isRecording) continue;
            RecordingJobRecord job;
            job.token = kAutoPrefix + source.recordingToken + "_" + stream.streamType;
            job.recordingToken = source.recordingToken;
            job.mode = "Active";
            job.priority = "0";
            job.sourceToken = stream.profileToken;
            job.sourceType = kProfileType;
            views.push_back({job, true});
            break;
        }
    }
    return views;
}

bool DvrRecordingService::findJobView(const std::string& token, const std::vector<Source>& sources,
                                      JobView& out) const {
    for (auto& view : listJobs(sources)) {
        if (view.job.token != token) continue;
        out = std::move(view);
        return true;
    }
    return false;
}

std::vector<RecordingJobEvent> DvrRecordingService::initialJobEvents() const {
    // Chạy từ PullMessages, không giữ khóa nào của event manager nên được gọi xuống DVR.
    std::vector<Source> sources;
    std::string ignored;
    const bool known = loadSources(sources, ignored);   // DVR lỗi → chỉ job đã lưu, báo Idle
    std::vector<RecordingJobEvent> events;
    for (const auto& view : listJobs(sources))
        events.push_back(jobEvent(view.job, known ? jobStateOf(view.job, sources) : "Idle",
                                  known ? trackOf(view.job, sources) : ""));
    return events;
}

// ── Đọc ───────────────────────────────────────────────────────────────

std::string DvrRecordingService::getJobs(const std::string& rel) {
    std::vector<Source> sources;
    std::string ignored;
    const bool known = loadSources(sources, ignored);   // DVR lỗi → chỉ job đã lưu, không kèm Tracks
    std::string items;
    for (const auto& view : listJobs(sources))
        items += "<trc:JobItem><tt:JobToken>" + esc(view.job.token) + "</tt:JobToken>"
                 "<tt:JobConfiguration>" + jobConfigXml(view.job, known ? &sources : nullptr) +
                 "</tt:JobConfiguration></trc:JobItem>";
    return reply(rel, "GetRecordingJobsResponse", "<trc:GetRecordingJobsResponse>" + items + "</trc:GetRecordingJobsResponse>");
}

std::string DvrRecordingService::getJobConfiguration(const std::string& req, const std::string& rel) {
    std::vector<Source> sources;
    std::string ignored;
    const bool known = loadSources(sources, ignored);
    JobView view;
    if (!findJobView(unesc(textOf(req, "JobToken")), sources, view)) return faultNoJob();
    return reply(rel, "GetRecordingJobConfigurationResponse",
                 "<trc:GetRecordingJobConfigurationResponse><trc:JobConfiguration>" +
                 jobConfigXml(view.job, known ? &sources : nullptr) +
                 "</trc:JobConfiguration></trc:GetRecordingJobConfigurationResponse>");
}

std::string DvrRecordingService::getJobState(const std::string& req, const std::string& rel) {
    std::vector<Source> sources;
    std::string fault;
    if (!loadSources(sources, fault)) return fault;
    JobView view;
    if (!findJobView(unesc(textOf(req, "JobToken")), sources, view)) return faultNoJob();

    const RecordingJobRecord& job = view.job;
    const std::string state = jobStateOf(job, sources);
    return reply(rel, "GetRecordingJobStateResponse",
                 "<trc:GetRecordingJobStateResponse><trc:State>"
                 "<tt:RecordingToken>" + esc(job.recordingToken) + "</tt:RecordingToken>"
                 "<tt:State>" + state + "</tt:State>"
                 "<tt:Sources><tt:SourceToken Type=\"" + esc(job.sourceType) + "\">"
                 "<tt:Token>" + esc(job.sourceToken) + "</tt:Token></tt:SourceToken>"
                 "<tt:State>" + state + "</tt:State>" + trackStateXml(trackOf(job, sources), state) +
                 "</tt:Sources></trc:State></trc:GetRecordingJobStateResponse>");
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

    // Chỉ job đã lưu mới chiếm chỗ: job quan sát được (luồng đang ghi từ web) bị thay bằng job thật.
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

    MockSubscriptionManager::getInstance().fireRecordingJobState(jobEvent(job, job.mode, stream->trackToken));
    return reply(rel, "CreateRecordingJobResponse",
                 "<trc:CreateRecordingJobResponse><trc:JobToken>" + esc(job.token) + "</trc:JobToken>"
                 "<trc:JobConfiguration>" + jobConfigXml(job, &sources) + "</trc:JobConfiguration>"
                 "</trc:CreateRecordingJobResponse>");
}

std::string DvrRecordingService::deleteJob(const std::string& req, const std::string& rel) {
    std::lock_guard<std::mutex> lock(mutateMutex_);
    const std::string token = unesc(textOf(req, "JobToken"));

    // Xóa job phải dừng việc ghi do job gây ra. Không dừng được thì giữ nguyên job để client
    // thử lại, không để lại một lần ghi không còn job nào quản lý.
    RecordingJobRecord job;
    if (store_->findJob(token, job)) {
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

    // Job quan sát được không nằm trong kho: xóa = tắt ghi tay của luồng đó (ghi theo lịch của
    // DVR không bị ảnh hưởng, nên nếu lịch đang ghi thì job vẫn còn hiện).
    if (token.rfind(kAutoPrefix, 0) != 0) return faultNoJob();
    std::vector<Source> sources;
    std::string fault;
    if (!loadSources(sources, fault)) return fault;
    JobView view;
    if (!findJobView(token, sources, view)) return faultNoJob();
    const Source* source = findSource(sources, view.job.recordingToken);
    const Stream* stream = source ? source->byProfile(view.job.sourceToken) : nullptr;
    if (stream) {
        try {
            setManualRecord(*source, *stream, false);
        } catch (const std::exception& e) {
            fprintf(stderr, "[DvrRecording] DeleteRecordingJob could not stop recording: %s\n", e.what());
            return faultBackend();
        }
    }
    return reply(rel, "DeleteRecordingJobResponse", "<trc:DeleteRecordingJobResponse/>");
}

std::string DvrRecordingService::setJobMode(const std::string& req, const std::string& rel) {
    std::lock_guard<std::mutex> lock(mutateMutex_);
    const std::string token = unesc(textOf(req, "JobToken"));
    RecordingJobRecord probe;
    if (!store_->findJob(token, probe) && token.rfind(kAutoPrefix, 0) != 0) return faultNoJob();
    const std::string mode = textOf(req, "Mode");
    if (!validMode(mode)) return FaultBuilder::sender("ter:InvalidArgVal", "ter:BadMode", "The Mode is invalid");

    std::vector<Source> sources;
    std::string fault;
    if (!loadSources(sources, fault)) return fault;
    JobView view;
    if (!findJobView(token, sources, view)) return faultNoJob();
    RecordingJobRecord job = view.job;
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
    // Job quan sát được được "nhận": từ giờ nằm trong kho, giữ nguyên token client đã thấy.
    if (view.observed) store_->addJobWithToken(job);
    else store_->updateJob(job);
    MockSubscriptionManager::getInstance().fireRecordingJobState(
        jobEvent(job, job.mode, stream ? stream->trackToken : ""));
    return reply(rel, "SetRecordingJobModeResponse", "<trc:SetRecordingJobModeResponse/>");
}

std::string DvrRecordingService::setJobConfiguration(const std::string& req, const std::string& rel) {
    std::lock_guard<std::mutex> lock(mutateMutex_);
    const std::string token = unesc(textOf(req, "JobToken"));
    RecordingJobRecord probe;
    if (!store_->findJob(token, probe) && token.rfind(kAutoPrefix, 0) != 0) return faultNoJob();
    const std::string scope = blockOf(req, "JobConfiguration");
    if (scope.empty()) return faultBadConfig("Missing JobConfiguration");
    const JobFields f = parseJob(scope);

    std::vector<Source> sources;
    std::string fault;
    if (!loadSources(sources, fault)) return fault;
    JobView view;
    if (!findJobView(token, sources, view)) return faultNoJob();
    const RecordingJobRecord job = view.job;

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

    // Job quan sát được được "nhận": từ giờ nằm trong kho, giữ nguyên token client đã thấy.
    if (view.observed) store_->addJobWithToken(updated);
    else store_->updateJob(updated);
    auto& events = MockSubscriptionManager::getInstance();
    events.fireRecordingConfigChanged(
        "RecordingJobConfiguration",
        "<tt:SimpleItem Name=\"RecordingJobToken\" Value=\"" + updated.token + "\"/>",
        "<tt:RecordingJobConfiguration>" + jobConfigXml(updated, &sources) + "</tt:RecordingJobConfiguration>");
    const std::string newState = newActive ? "Active" : "Idle";
    if (newState != oldState) events.fireRecordingJobState(jobEvent(updated, newState, newStream->trackToken));

    return reply(rel, "SetRecordingJobConfigurationResponse",
                 "<trc:SetRecordingJobConfigurationResponse><trc:JobConfiguration>" + jobConfigXml(updated, &sources) +
                 "</trc:JobConfiguration></trc:SetRecordingJobConfigurationResponse>");
}
