// RecordingService.cpp — ONVIF Recording Control ver10 (Profile G) — string-based.
// STUB-FEATURES: trả response schema-hợp-lệ cho mọi op §9.1 mandatory (non-dynamic,
// 1 recording tĩnh Recording_0 = VIDEO_0 + META_0, 1 job Job_0). Đủ để DTT Feature
// Definition đánh dấu Profile G SUPPORTED. Data model tĩnh → không cần backend/IPC.

#include "services/RecordingService.h"
#include "services/MockSubscriptionManager.h"
#include "utils/FaultBuilder.h"
#include <mutex>
#include <sstream>

namespace {
const char* NS_RECORDING = "http://www.onvif.org/ver10/recording/wsdl";
const char* ACT = "http://www.onvif.org/ver10/recording/wsdl/RecordingPort/";

// ── Data model tĩnh (khớp scope đã chốt) ──────────────────────────────────────
const char* REC = "Recording_0";
const char* JOB = "Job_0";
const char* SRC_PROFILE = "profile_main";   // on-board media source (Media profile)

// RecordingConfiguration — dùng chung GetRecordings & GetRecordingConfiguration.
std::string defaultRecordingConfig() {
    return
        "<tt:Source>"
          "<tt:SourceId>http://localhost/sourceId</tt:SourceId>"
          "<tt:Name>MockCam-4K</tt:Name>"
          "<tt:Location>MockSite</tt:Location>"
          "<tt:Description>On-board recording</tt:Description>"
          "<tt:Address>http://localhost/recording</tt:Address>"
        "</tt:Source>"
        "<tt:Content>Mock on-board recording</tt:Content>"
        "<tt:MaximumRetentionTime>P30D</tt:MaximumRetentionTime>";
}

std::mutex g_configMtx;
std::string g_recordingConfig = defaultRecordingConfig();
std::string g_videoTrackConfig =
    "<tt:TrackType>Video</tt:TrackType>"
    "<tt:Description>Video track</tt:Description>";
std::string g_metadataTrackConfig =
    "<tt:TrackType>Metadata</tt:TrackType>"
    "<tt:Description>Metadata track</tt:Description>";

std::string getRecordingConfig() {
    std::lock_guard<std::mutex> lock(g_configMtx);
    return g_recordingConfig;
}

void setRecordingConfig(const std::string& config) {
    std::lock_guard<std::mutex> lock(g_configMtx);
    g_recordingConfig = config;
}

std::string getTrackConfig(const std::string& token) {
    std::lock_guard<std::mutex> lock(g_configMtx);
    return token == "META_0" ? g_metadataTrackConfig : g_videoTrackConfig;
}

void setTrackConfig(const std::string& token, const std::string& config) {
    std::lock_guard<std::mutex> lock(g_configMtx);
    (token == "META_0" ? g_metadataTrackConfig : g_videoTrackConfig) = config;
}

std::string extractElementContent(const std::string& xml, const std::string& tag) {
    auto p = xml.find("<" + tag);
    if (p == std::string::npos) return "";
    auto gt = xml.find('>', p);
    if (gt == std::string::npos) return "";
    auto end = xml.find("</" + tag + ">", gt);
    if (end == std::string::npos) return "";
    return xml.substr(gt + 1, end - gt - 1);
}

// 2 track: VIDEO_0 + META_0 (không audio).
std::string tracks() {
    return
        "<tt:Track>"
          "<tt:TrackToken>VIDEO_0</tt:TrackToken>"
          "<tt:Configuration>"
            "<tt:TrackType>Video</tt:TrackType>"
            "<tt:Description>Video track</tt:Description>"
          "</tt:Configuration>"
        "</tt:Track>"
        "<tt:Track>"
          "<tt:TrackToken>META_0</tt:TrackToken>"
          "<tt:Configuration>"
            "<tt:TrackType>Metadata</tt:TrackType>"
            "<tt:Description>Metadata track</tt:Description>"
          "</tt:Configuration>"
        "</tt:Track>";
}

struct JobState {
    bool exists = true;
    std::string mode = "Idle";
    std::string priority = "10";
    std::string sourceToken = SRC_PROFILE;
    std::string sourceType = "http://www.onvif.org/ver10/schema/Profile";
};
std::mutex g_jobMtx;
JobState g_job;

std::string extractElementBlock(const std::string& xml, const std::string& element) {
    for (const auto& prefix : {std::string("tt:"), std::string()}) {
        const auto name = prefix + element;
        auto p = xml.find("<" + name);
        if (p == std::string::npos) continue;
        auto end = xml.find("</" + name + ">", p);
        if (end == std::string::npos) continue;
        return xml.substr(p, end + name.size() + 3 - p);
    }
    return "";
}

std::string extractAttribute(const std::string& xml, const std::string& element,
                             const std::string& attr) {
    auto p = xml.find("<" + element);
    if (p == std::string::npos) return "";
    auto gt = xml.find('>', p);
    if (gt == std::string::npos) return "";
    auto a = xml.find(attr + "=\"", p);
    if (a == std::string::npos || a > gt) return "";
    a += attr.size() + 2;
    auto end = xml.find('"', a);
    return end == std::string::npos ? "" : xml.substr(a, end - a);
}

std::string jobConfig(const JobState& job) {
    return
        "<tt:RecordingToken>" + std::string(REC) + "</tt:RecordingToken>"
        "<tt:Mode>" + job.mode + "</tt:Mode>"
        "<tt:Priority>" + job.priority + "</tt:Priority>"
        "<tt:Source>"
          "<tt:SourceToken Type=\"" + job.sourceType + "\">"
            "<tt:Token>" + job.sourceToken + "</tt:Token>"
          "</tt:SourceToken>"
        "</tt:Source>";
}

JobState getJob() {
    std::lock_guard<std::mutex> lock(g_jobMtx);
    return g_job;
}

void setJob(const JobState& job) {
    std::lock_guard<std::mutex> lock(g_jobMtx);
    g_job = job;
}
} // namespace

std::string RecordingService::recordingConfigXml() {
    return getRecordingConfig();
}

std::string RecordingService::extractRelatesTo(const std::string& xml) {
    for (const char* tag : {"MessageID", "wsa:MessageID", "wsa5:MessageID"}) {
        auto p = xml.find(tag);
        if (p == std::string::npos) continue;
        auto gt = xml.find('>', p);
        if (gt == std::string::npos) continue;
        auto lt = xml.find('<', gt);
        if (lt == std::string::npos) continue;
        return xml.substr(gt + 1, lt - gt - 1);
    }
    return "";
}

std::string RecordingService::extractInnerTag(const std::string& xml,
                                              const std::string& tag) {
    size_t open = 0;
    while ((open = xml.find('<', open)) != std::string::npos) {
        const size_t nameStart = open + 1;
        if (nameStart >= xml.size() || xml[nameStart] == '/' ||
            xml[nameStart] == '!' || xml[nameStart] == '?') {
            ++open;
            continue;
        }
        const auto nameEnd = xml.find_first_of(" >\t\r\n", nameStart);
        if (nameEnd == std::string::npos) return "";
        const auto colon = xml.rfind(':', nameEnd);
        const size_t localStart =
            (colon != std::string::npos && colon >= nameStart) ? colon + 1 : nameStart;
        if (xml.compare(localStart, nameEnd - localStart, tag) != 0) {
            open = nameEnd;
            continue;
        }
        const auto gt = xml.find('>', nameEnd);
        if (gt == std::string::npos) return "";
        const auto lt = xml.find('<', gt + 1);
        if (lt == std::string::npos) return "";
        return xml.substr(gt + 1, lt - gt - 1);
    }
    return "";
}

std::string RecordingService::wrap(const std::string& action,
                                   const std::string& relatesTo,
                                   const std::string& bodyXml) {
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
       << "<SOAP-ENV:Envelope"
       << " xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\""
       << " xmlns:wsa=\"http://www.w3.org/2005/08/addressing\""
       << " xmlns:trc=\"" << NS_RECORDING << "\""
       << " xmlns:tt=\"http://www.onvif.org/ver10/schema\""
       << " xmlns:ter=\"http://www.onvif.org/ver10/error\">"
       << "<SOAP-ENV:Header>"
       << "<wsa:Action>" << action << "</wsa:Action>";
    if (!relatesTo.empty())
        os << "<wsa:RelatesTo>" << relatesTo << "</wsa:RelatesTo>";
    os << "</SOAP-ENV:Header>"
       << "<SOAP-ENV:Body>" << bodyXml << "</SOAP-ENV:Body>"
       << "</SOAP-ENV:Envelope>";
    return os.str();
}

// Dispatch theo op-name. THỨ TỰ QUAN TRỌNG: op tên dài chứa op tên ngắn phải
// check trước (GetRecordingJobConfiguration/JobState trước GetRecordingJobs;
// mọi *RecordingJob* trước GetRecordings tránh nuốt nhầm là không xảy ra vì
// "GetRecordings" không phải substring của op khác, nhưng vẫn để nhóm rõ ràng).
std::string RecordingService::handle(const std::string& req) {
    std::string rel = extractRelatesTo(req);
    auto R = [&](const char* op, const std::string& body) {
        return wrap(std::string(ACT) + op, rel, body);
    };
    auto has = [&](const char* s) { return req.find(s) != std::string::npos; };
    auto fault = [](const char* code, const char* reason) {
        return FaultBuilder::sender("ter:InvalidArgVal", code, reason);
    };
    auto recordingToken = [&]() { return extractInnerTag(req, "RecordingToken"); };
    auto jobToken = [&]() { return extractInnerTag(req, "JobToken"); };

    if (has("GetServiceCapabilities"))
        return R("GetServiceCapabilitiesResponse",
            "<trc:GetServiceCapabilitiesResponse>"
              "<trc:Capabilities DynamicRecordings=\"false\" DynamicTracks=\"false\" "
               "DeleteData=\"false\" Encoding=\"H264\" MaxRate=\"20000\" "
               "MaxTotalRate=\"20000\" MaxRecordings=\"1\" MaxRecordingJobs=\"1\" "
               "Options=\"true\"/>"
            "</trc:GetServiceCapabilitiesResponse>");

    if (has("GetRecordingOptions")) {
        if (recordingToken() != REC)
            return fault("ter:NoRecording", "No recording with the given token");
        return R("GetRecordingOptionsResponse",
            "<trc:GetRecordingOptionsResponse>"
              "<trc:Options>"
                "<trc:Job Spare=\"1\" CompatibleSources=\"" +
                    std::string(SRC_PROFILE) + "\"/>"
                // DynamicTracks=false: báo không còn slot track động.
                "<trc:Track SpareTotal=\"0\" SpareVideo=\"0\" "
                           "SpareAudio=\"0\" SpareMetadata=\"0\"/>"
              "</trc:Options>"
            "</trc:GetRecordingOptionsResponse>");
    }

    if (has("GetRecordingConfiguration")) {
        if (recordingToken() != REC)
            return fault("ter:NoRecording", "No recording with the given token");
        return R("GetRecordingConfigurationResponse",
            "<trc:GetRecordingConfigurationResponse>"
              "<trc:RecordingConfiguration>" + getRecordingConfig() +
              "</trc:RecordingConfiguration>"
            "</trc:GetRecordingConfigurationResponse>");
    }

    if (has("SetRecordingConfiguration")) {
        if (recordingToken() != REC)
            return fault("ter:NoRecording", "No recording with the given token");
        auto config = extractElementContent(req, "RecordingConfiguration");
        if (!config.empty()) {
            setRecordingConfig(config);
            MockSubscriptionManager::getInstance().fireRecordingConfigChanged(
                "RecordingConfiguration",
                "<tt:SimpleItem Name=\"RecordingToken\" Value=\"Recording_0\"/>",
                "<tt:RecordingConfiguration>" + config + "</tt:RecordingConfiguration>");
        }
        return R("SetRecordingConfigurationResponse",
            "<trc:SetRecordingConfigurationResponse/>");
    }

    if (has("GetTrackConfiguration")) {
        if (recordingToken() != REC)
            return fault("ter:NoRecording", "No recording with the given token");
        const auto trackToken = extractInnerTag(req, "TrackToken");
        if (trackToken != "VIDEO_0" && trackToken != "META_0")
            return fault("ter:NoTrack", "No track with the given token");
        return R("GetTrackConfigurationResponse",
            "<trc:GetTrackConfigurationResponse>"
              "<trc:TrackConfiguration>" + getTrackConfig(trackToken) +
              "</trc:TrackConfiguration>"
            "</trc:GetTrackConfigurationResponse>");
    }

    if (has("SetTrackConfiguration")) {
        if (recordingToken() != REC)
            return fault("ter:NoRecording", "No recording with the given token");
        const auto trackToken = extractInnerTag(req, "TrackToken");
        if (trackToken != "VIDEO_0" && trackToken != "META_0")
            return fault("ter:NoTrack", "No track with the given token");
        const auto config = extractElementContent(req, "TrackConfiguration");
        if (!config.empty()) {
            setTrackConfig(trackToken, config);
            MockSubscriptionManager::getInstance().fireRecordingConfigChanged(
                "TrackConfiguration",
                "<tt:SimpleItem Name=\"RecordingToken\" Value=\"Recording_0\"/>"
                "<tt:SimpleItem Name=\"TrackToken\" Value=\"" + trackToken + "\"/>",
                "<tt:TrackConfiguration>" + config + "</tt:TrackConfiguration>");
        }
        return R("SetTrackConfigurationResponse",
            "<trc:SetTrackConfigurationResponse/>");
    }

    if (has("GetRecordingJobConfiguration")) {
        const auto job = getJob();
        if (jobToken() != JOB || !job.exists)
            return fault("ter:NoRecordingJob", "No recording job with the given token");
        return R("GetRecordingJobConfigurationResponse",
            "<trc:GetRecordingJobConfigurationResponse>"
              "<trc:JobConfiguration>" + jobConfig(job) + "</trc:JobConfiguration>"
            "</trc:GetRecordingJobConfigurationResponse>");
    }

    if (has("SetRecordingJobConfiguration")) {
        auto job = getJob();
        if (jobToken() != JOB || !job.exists)
            return fault("ter:NoRecordingJob", "No recording job with the given token");
        auto mode = extractInnerTag(req, "Mode");
        auto priority = extractInnerTag(req, "Priority");
        const auto sourceBlock = extractElementBlock(req, "SourceToken");
        auto sourceToken = extractInnerTag(sourceBlock, "Token");
        auto sourceType = extractAttribute(sourceBlock, "SourceToken", "Type");
        if (!mode.empty()) job.mode = mode;
        if (!priority.empty()) job.priority = priority;
        if (!sourceToken.empty()) job.sourceToken = sourceToken;
        if (!sourceType.empty()) job.sourceType = sourceType;
        setJob(job);
        MockSubscriptionManager::getInstance().fireRecordingConfigChanged(
            "RecordingJobConfiguration",
            "<tt:SimpleItem Name=\"RecordingJobToken\" Value=\"Job_0\"/>",
            "<tt:RecordingJobConfiguration>" + jobConfig(job) +
            "</tt:RecordingJobConfiguration>");
        return R("SetRecordingJobConfigurationResponse",
            "<trc:SetRecordingJobConfigurationResponse>"
              "<trc:JobConfiguration>" + jobConfig(job) + "</trc:JobConfiguration>"
            "</trc:SetRecordingJobConfigurationResponse>");
    }

    if (has("GetRecordingJobState")) {
        const auto job = getJob();
        if (jobToken() != JOB || !job.exists)
            return fault("ter:NoRecordingJob", "No recording job with the given token");
        return R("GetRecordingJobStateResponse",
            "<trc:GetRecordingJobStateResponse>"
              "<trc:State>"
                "<tt:RecordingToken>" + std::string(REC) + "</tt:RecordingToken>"
                "<tt:State>" + job.mode + "</tt:State>"
                "<tt:Sources>"
                  "<tt:SourceToken Type=\"" + job.sourceType + "\">"
                    "<tt:Token>" + job.sourceToken + "</tt:Token>"
                  "</tt:SourceToken>"
                  "<tt:State>" + job.mode + "</tt:State>"
                "</tt:Sources>"
              "</trc:State>"
            "</trc:GetRecordingJobStateResponse>");
    }

    if (has("SetRecordingJobMode")) {
        auto job = getJob();
        if (jobToken() != JOB || !job.exists)
            return fault("ter:NoRecordingJob", "No recording job with the given token");
        auto mode = extractInnerTag(req, "Mode");
        if (!mode.empty()) job.mode = mode;
        setJob(job);
        MockSubscriptionManager::getInstance().fireRecordingJobState(JOB, job.mode);
        return R("SetRecordingJobModeResponse",
            "<trc:SetRecordingJobModeResponse/>");
    }

    if (has("CreateRecordingJob")) {
        JobState job;
        job.exists = true;
        auto mode = extractInnerTag(req, "Mode");
        auto priority = extractInnerTag(req, "Priority");
        const auto sourceBlock = extractElementBlock(req, "SourceToken");
        auto sourceToken = extractInnerTag(sourceBlock, "Token");
        auto sourceType = extractAttribute(sourceBlock, "SourceToken", "Type");
        if (!mode.empty()) job.mode = mode;
        if (!priority.empty()) job.priority = priority;
        if (!sourceToken.empty()) job.sourceToken = sourceToken;
        if (!sourceType.empty()) job.sourceType = sourceType;
        setJob(job);
        MockSubscriptionManager::getInstance().fireRecordingJobState(JOB, job.mode);
        return R("CreateRecordingJobResponse",
            "<trc:CreateRecordingJobResponse>"
              "<trc:JobToken>" + std::string(JOB) + "</trc:JobToken>"
              "<trc:JobConfiguration>" + jobConfig(job) + "</trc:JobConfiguration>"
            "</trc:CreateRecordingJobResponse>");
    }

    if (has("DeleteRecordingJob")) {
        auto job = getJob();
        if (jobToken() != JOB || !job.exists)
            return fault("ter:NoRecordingJob", "No recording job with the given token");
        job.exists = false;
        setJob(job);
        return R("DeleteRecordingJobResponse",
            "<trc:DeleteRecordingJobResponse/>");
    }

    if (has("GetRecordingJobs")) {
        const auto job = getJob();
        return R("GetRecordingJobsResponse",
            "<trc:GetRecordingJobsResponse>" +
              (job.exists ?
                "<trc:JobItem>"
                  "<tt:JobToken>" + std::string(JOB) + "</tt:JobToken>"
                  "<tt:JobConfiguration>" + jobConfig(job) + "</tt:JobConfiguration>"
                "</trc:JobItem>" : "") +
            "</trc:GetRecordingJobsResponse>");
    }

    if (has("GetRecordings"))
        return R("GetRecordingsResponse",
            "<trc:GetRecordingsResponse>"
              "<trc:RecordingItem>"
                "<tt:RecordingToken>" + std::string(REC) + "</tt:RecordingToken>"
                "<tt:Configuration>" + getRecordingConfig() + "</tt:Configuration>"
                "<tt:Tracks>" + tracks() + "</tt:Tracks>"
              "</trc:RecordingItem>"
            "</trc:GetRecordingsResponse>");

    return "";  // op không nhận diện → OnvifServer fallback fault.
}
