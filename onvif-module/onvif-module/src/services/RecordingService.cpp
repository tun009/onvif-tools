// RecordingService.cpp — ONVIF Recording Control ver10 (Profile G) — string-based.
// STUB-FEATURES: trả response schema-hợp-lệ cho mọi op §9.1 mandatory (non-dynamic,
// 1 recording tĩnh Recording_0 = VIDEO_0 + META_0, 1 job Job_0). Đủ để DTT Feature
// Definition đánh dấu Profile G SUPPORTED. Data model tĩnh → không cần backend/IPC.

#include "services/RecordingService.h"
#include <sstream>

namespace {
const char* NS_RECORDING = "http://www.onvif.org/ver10/recording/wsdl";
const char* ACT = "http://www.onvif.org/ver10/recording/wsdl/RecordingPort/";

// ── Data model tĩnh (khớp scope đã chốt) ──────────────────────────────────────
const char* REC = "Recording_0";
const char* JOB = "Job_0";
const char* SRC_PROFILE = "profile_main";   // on-board media source (Media profile)

// RecordingConfiguration — dùng chung GetRecordings & GetRecordingConfiguration.
std::string recordingConfig() {
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

// RecordingJobConfiguration — 1 job ghi từ profile_main, mode Idle.
std::string jobConfig() {
    return
        "<tt:RecordingToken>" + std::string(REC) + "</tt:RecordingToken>"
        "<tt:Mode>Idle</tt:Mode>"
        "<tt:Priority>10</tt:Priority>"
        "<tt:Source>"
          "<tt:SourceToken>"
            "<tt:Token>" + std::string(SRC_PROFILE) + "</tt:Token>"
            "<tt:Type>http://www.onvif.org/ver10/schema/Profile</tt:Type>"
          "</tt:SourceToken>"
          "<tt:AutoCreateReceiver>false</tt:AutoCreateReceiver>"
          "<tt:Tracks>"
            "<tt:SourceTag>VIDEO</tt:SourceTag>"
            "<tt:Destination>VIDEO_0</tt:Destination>"
          "</tt:Tracks>"
        "</tt:Source>";
}
} // namespace

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

    if (has("GetServiceCapabilities"))
        return R("GetServiceCapabilitiesResponse",
            "<trc:GetServiceCapabilitiesResponse>"
              "<trc:Capabilities DynamicRecordings=\"false\" DynamicTracks=\"false\" "
               "DeleteData=\"false\" Encoding=\"H264\" MaxRate=\"20000\" "
               "MaxTotalRate=\"20000\" MaxRecordings=\"1\" MaxRecordingJobs=\"1\" "
               "Options=\"false\"/>"
            "</trc:GetServiceCapabilitiesResponse>");

    if (has("GetRecordingOptions"))
        return R("GetRecordingOptionsResponse",
            "<trc:GetRecordingOptionsResponse>"
              "<trc:Options>"
                "<tt:Job>"
                  "<tt:Spare>1</tt:Spare>"
                  "<tt:CompatibleSources>" + std::string(SRC_PROFILE) + "</tt:CompatibleSources>"
                "</tt:Job>"
                "<tt:Track>"
                  "<tt:SpareTotal>0</tt:SpareTotal>"
                  "<tt:SpareVideo>0</tt:SpareVideo>"
                  "<tt:SpareAudio>0</tt:SpareAudio>"
                  "<tt:SpareMetadata>0</tt:SpareMetadata>"
                "</tt:Track>"
              "</trc:Options>"
            "</trc:GetRecordingOptionsResponse>");

    if (has("GetRecordingConfiguration"))
        return R("GetRecordingConfigurationResponse",
            "<trc:GetRecordingConfigurationResponse>"
              "<trc:RecordingConfiguration>" + recordingConfig() +
              "</trc:RecordingConfiguration>"
            "</trc:GetRecordingConfigurationResponse>");

    if (has("SetRecordingConfiguration"))
        return R("SetRecordingConfigurationResponse",
            "<trc:SetRecordingConfigurationResponse/>");

    if (has("GetTrackConfiguration"))
        return R("GetTrackConfigurationResponse",
            "<trc:GetTrackConfigurationResponse>"
              "<trc:TrackConfiguration>"
                "<tt:TrackType>Video</tt:TrackType>"
                "<tt:Description>Video track</tt:Description>"
              "</trc:TrackConfiguration>"
            "</trc:GetTrackConfigurationResponse>");

    if (has("SetTrackConfiguration"))
        return R("SetTrackConfigurationResponse",
            "<trc:SetTrackConfigurationResponse/>");

    if (has("GetRecordingJobConfiguration"))
        return R("GetRecordingJobConfigurationResponse",
            "<trc:GetRecordingJobConfigurationResponse>"
              "<trc:JobConfiguration>" + jobConfig() + "</trc:JobConfiguration>"
            "</trc:GetRecordingJobConfigurationResponse>");

    if (has("SetRecordingJobConfiguration"))
        return R("SetRecordingJobConfigurationResponse",
            "<trc:SetRecordingJobConfigurationResponse>"
              "<trc:JobConfiguration>" + jobConfig() + "</trc:JobConfiguration>"
            "</trc:SetRecordingJobConfigurationResponse>");

    if (has("GetRecordingJobState"))
        return R("GetRecordingJobStateResponse",
            "<trc:GetRecordingJobStateResponse>"
              "<trc:State>"
                "<tt:RecordingToken>" + std::string(REC) + "</tt:RecordingToken>"
                "<tt:State>Idle</tt:State>"
                "<tt:Sources>"
                  "<tt:SourceToken>"
                    "<tt:Token>" + std::string(SRC_PROFILE) + "</tt:Token>"
                    "<tt:Type>http://www.onvif.org/ver10/schema/Profile</tt:Type>"
                  "</tt:SourceToken>"
                  "<tt:State>Idle</tt:State>"
                  "<tt:Tracks>"
                    "<tt:Track>"
                      "<tt:SourceTag>VIDEO</tt:SourceTag>"
                      "<tt:Destination>VIDEO_0</tt:Destination>"
                      "<tt:State>Idle</tt:State>"
                    "</tt:Track>"
                  "</tt:Tracks>"
                "</tt:Sources>"
              "</trc:State>"
            "</trc:GetRecordingJobStateResponse>");

    if (has("SetRecordingJobMode"))
        return R("SetRecordingJobModeResponse",
            "<trc:SetRecordingJobModeResponse/>");

    if (has("CreateRecordingJob"))
        return R("CreateRecordingJobResponse",
            "<trc:CreateRecordingJobResponse>"
              "<trc:JobToken>" + std::string(JOB) + "</trc:JobToken>"
              "<trc:JobConfiguration>" + jobConfig() + "</trc:JobConfiguration>"
            "</trc:CreateRecordingJobResponse>");

    if (has("DeleteRecordingJob"))
        return R("DeleteRecordingJobResponse",
            "<trc:DeleteRecordingJobResponse/>");

    if (has("GetRecordingJobs"))
        return R("GetRecordingJobsResponse",
            "<trc:GetRecordingJobsResponse>"
              "<trc:JobItem>"
                "<tt:JobToken>" + std::string(JOB) + "</tt:JobToken>"
                "<tt:JobConfiguration>" + jobConfig() + "</tt:JobConfiguration>"
              "</trc:JobItem>"
            "</trc:GetRecordingJobsResponse>");

    if (has("GetRecordings"))
        return R("GetRecordingsResponse",
            "<trc:GetRecordingsResponse>"
              "<trc:RecordingItem>"
                "<tt:RecordingToken>" + std::string(REC) + "</tt:RecordingToken>"
                "<tt:Configuration>" + recordingConfig() + "</tt:Configuration>"
                "<tt:Tracks>" + tracks() + "</tt:Tracks>"
              "</trc:RecordingItem>"
            "</trc:GetRecordingsResponse>");

    return "";  // op không nhận diện → OnvifServer fallback fault.
}
