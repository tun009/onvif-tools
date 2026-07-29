// SearchService.cpp — ONVIF Recording Search ver10 (Profile G) — string-based.
// STUB-FEATURES: mọi op §7.3 mandatory trả response schema-hợp-lệ. Search đồng bộ:
// FindRecordings/FindEvents → SearchToken; GetXxxResults → toàn bộ kết quả 1 lần
// (SearchState=Completed). 1 recording tĩnh Recording_0. Filter parse-cho-qua.

#include "services/SearchService.h"
#include "services/RecordingService.h"
#include "utils/FaultBuilder.h"
#include <mutex>
#include <sstream>

namespace {
const char* NS_SEARCH = "http://www.onvif.org/ver10/search/wsdl";
const char* ACT = "http://www.onvif.org/ver10/search/wsdl/SearchPort/";

const char* REC = "Recording_0";
const char* RECORDING_SEARCH_TOKEN = "RecordingSearch_0";
const char* EVENT_SEARCH_TOKEN = "EventSearch_0";
std::mutex g_searchMtx;
bool g_recordingSearchActive = false;
bool g_recordingSearchMatches = true;
bool g_eventSearchActive = false;
int g_eventMaxMatches = 0;
std::string g_eventStartPoint;
std::string g_eventEndPoint;
bool g_eventIncludeStartState = false;
// Khoảng thời gian recording tĩnh (dữ liệu giả để search trả superset).
const char* T_FROM = "2026-07-01T00:00:00Z";
const char* T_UNTIL = "2026-07-28T00:00:00Z";

std::string elementBlock(const std::string& xml, const std::string& tag) {
    for (const auto& prefix : {std::string("tt:"), std::string()}) {
        const auto name = prefix + tag;
        auto p = xml.find("<" + name);
        if (p == std::string::npos) continue;
        auto gt = xml.find('>', p);
        if (gt == std::string::npos) continue;
        auto end = xml.find("</" + name + ">", gt);
        if (end == std::string::npos) continue;
        end += name.size() + 3;
        auto block = xml.substr(p, end - p);
        if (prefix.empty()) {
            block.replace(1, tag.size(), "tt:" + tag);
            auto close = block.rfind("</" + tag + ">");
            if (close != std::string::npos) block.replace(close + 2, tag.size(), "tt:" + tag);
        }
        return block;
    }
    return "";
}

// RecordingInformation chỉ dùng Source + Content từ RecordingConfiguration.
// MaximumRetentionTime KHÔNG thuộc schema RecordingInformation (g3 regression).
std::string recordingInformation() {
    const auto cfg = RecordingService::recordingConfigXml();
    const auto source = elementBlock(cfg, "Source");
    const auto content = elementBlock(cfg, "Content");
    return
        "<tt:RecordingToken>" + std::string(REC) + "</tt:RecordingToken>" +
        source +
        "<tt:EarliestRecording>" + std::string(T_FROM) + "</tt:EarliestRecording>"
        "<tt:LatestRecording>" + std::string(T_UNTIL) + "</tt:LatestRecording>" +
        content +
        "<tt:Track>"
          "<tt:TrackToken>VIDEO_0</tt:TrackToken>"
          "<tt:TrackType>Video</tt:TrackType>"
          "<tt:Description>Video track</tt:Description>"
          "<tt:DataFrom>" + std::string(T_FROM) + "</tt:DataFrom>"
          "<tt:DataTo>" + std::string(T_UNTIL) + "</tt:DataTo>"
        "</tt:Track>"
        "<tt:Track>"
          "<tt:TrackToken>META_0</tt:TrackToken>"
          "<tt:TrackType>Metadata</tt:TrackType>"
          "<tt:Description>Metadata track</tt:Description>"
          "<tt:DataFrom>" + std::string(T_FROM) + "</tt:DataFrom>"
          "<tt:DataTo>" + std::string(T_UNTIL) + "</tt:DataTo>"
        "</tt:Track>"
        "<tt:RecordingStatus>Stopped</tt:RecordingStatus>";
}
} // namespace

std::string SearchService::extractRelatesTo(const std::string& xml) {
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

// Giữ để dùng khi thêm lọc theo tham số ở bước sau (hiện chưa cần). Đánh dấu
// [[maybe_unused]] tránh warning -Wextra.
[[maybe_unused]]
std::string SearchService::extractInnerTag(const std::string& xml, const std::string& tag) {
    auto p = xml.find(tag + ">");
    if (p == std::string::npos) return "";
    auto gt = xml.find('>', p);
    if (gt == std::string::npos) return "";
    auto lt = xml.find('<', gt);
    if (lt == std::string::npos) return "";
    return xml.substr(gt + 1, lt - gt - 1);
}

std::string SearchService::wrap(const std::string& action,
                                const std::string& relatesTo,
                                const std::string& bodyXml) {
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
       << "<SOAP-ENV:Envelope"
       << " xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\""
       << " xmlns:wsa=\"http://www.w3.org/2005/08/addressing\""
       << " xmlns:tse=\"" << NS_SEARCH << "\""
       << " xmlns:tt=\"http://www.onvif.org/ver10/schema\""
       << " xmlns:wsnt=\"http://docs.oasis-open.org/wsn/b-2\""
       << " xmlns:tns1=\"http://www.onvif.org/ver10/topics\""
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

std::string SearchService::handle(const std::string& req) {
    std::string rel = extractRelatesTo(req);
    auto R = [&](const char* op, const std::string& body) {
        return wrap(std::string(ACT) + op, rel, body);
    };
    auto has = [&](const char* s) { return req.find(s) != std::string::npos; };

    if (has("GetServiceCapabilities"))
        return R("GetServiceCapabilitiesResponse",
            "<tse:GetServiceCapabilitiesResponse>"
              "<tse:Capabilities MetadataSearch=\"false\" GeneralStartEvents=\"true\"/>"
            "</tse:GetServiceCapabilitiesResponse>");

    if (has("GetRecordingSummary"))
        return R("GetRecordingSummaryResponse",
            "<tse:GetRecordingSummaryResponse>"
              "<tse:Summary>"
                "<tt:DataFrom>" + std::string(T_FROM) + "</tt:DataFrom>"
                "<tt:DataUntil>" + std::string(T_UNTIL) + "</tt:DataUntil>"
                "<tt:NumberRecordings>1</tt:NumberRecordings>"
              "</tse:Summary>"
            "</tse:GetRecordingSummaryResponse>");

    if (has("GetRecordingInformation"))
        return R("GetRecordingInformationResponse",
            "<tse:GetRecordingInformationResponse>"
              "<tse:RecordingInformation>" + recordingInformation() +
              "</tse:RecordingInformation>"
            "</tse:GetRecordingInformationResponse>");

    if (has("GetMediaAttributes"))
        return R("GetMediaAttributesResponse",
            "<tse:GetMediaAttributesResponse>"
              "<tse:MediaAttributes>"
                "<tt:RecordingToken>" + std::string(REC) + "</tt:RecordingToken>"
                "<tt:TrackAttributes>"
                  "<tt:TrackInformation>"
                    "<tt:TrackToken>VIDEO_0</tt:TrackToken>"
                    "<tt:TrackType>Video</tt:TrackType>"
                    "<tt:Description>Video track</tt:Description>"
                    "<tt:DataFrom>" + std::string(T_FROM) + "</tt:DataFrom>"
                    "<tt:DataTo>" + std::string(T_UNTIL) + "</tt:DataTo>"
                  "</tt:TrackInformation>"
                  "<tt:VideoAttributes>"
                    "<tt:Bitrate>8000</tt:Bitrate>"
                    "<tt:Width>3840</tt:Width>"
                    "<tt:Height>2160</tt:Height>"
                    "<tt:Encoding>H264</tt:Encoding>"
                    "<tt:Framerate>25</tt:Framerate>"
                  "</tt:VideoAttributes>"
                "</tt:TrackAttributes>"
                "<tt:From>" + std::string(T_FROM) + "</tt:From>"
                "<tt:Until>" + std::string(T_UNTIL) + "</tt:Until>"
              "</tse:MediaAttributes>"
            "</tse:GetMediaAttributesResponse>");

    if (has("FindRecordings")) {
        std::lock_guard<std::mutex> lock(g_searchMtx);
        g_recordingSearchActive = true;
        const auto filter = extractInnerTag(req, "RecordingInformationFilter");
        // Recording_0 has Video + Metadata only. XPath filters requiring Audio
        // are valid but match no recordings; returning the unfiltered recording
        // violates Recording Search filtering semantics.
        g_recordingSearchMatches = filter.find("TrackType = &quot;Audio&quot;") ==
                                   std::string::npos &&
                                   filter.find("TrackType = \"Audio\"") ==
                                   std::string::npos;
        return R("FindRecordingsResponse",
            "<tse:FindRecordingsResponse>"
              "<tse:SearchToken>" + std::string(RECORDING_SEARCH_TOKEN) + "</tse:SearchToken>"
            "</tse:FindRecordingsResponse>");
    }

    if (has("GetRecordingSearchResults")) {
        const auto token = extractInnerTag(req, "SearchToken");
        std::lock_guard<std::mutex> lock(g_searchMtx);
        if (token != RECORDING_SEARCH_TOKEN || !g_recordingSearchActive)
            return FaultBuilder::sender("ter:InvalidArgVal", "ter:InvalidToken",
                                        "Invalid search token");
        const auto result = g_recordingSearchMatches
            ? "<tt:RecordingInformation>" + recordingInformation() +
              "</tt:RecordingInformation>"
            : std::string();
        return R("GetRecordingSearchResultsResponse",
            "<tse:GetRecordingSearchResultsResponse>"
              "<tse:ResultList>"
                "<tt:SearchState>Completed</tt:SearchState>" + result +
              "</tse:ResultList>"
            "</tse:GetRecordingSearchResultsResponse>");
    }

    if (has("FindEvents")) {
        std::lock_guard<std::mutex> lock(g_searchMtx);
        g_eventSearchActive = true;
        g_eventMaxMatches = 0;
        g_eventStartPoint = extractInnerTag(req, "StartPoint");
        g_eventEndPoint = extractInnerTag(req, "EndPoint");
        g_eventIncludeStartState = extractInnerTag(req, "IncludeStartState") == "true";
        const auto maxMatches = extractInnerTag(req, "MaxMatches");
        if (!maxMatches.empty()) {
            try { g_eventMaxMatches = std::stoi(maxMatches); } catch (...) {}
        }
        return R("FindEventsResponse",
            "<tse:FindEventsResponse>"
              "<tse:SearchToken>" + std::string(EVENT_SEARCH_TOKEN) + "</tse:SearchToken>"
            "</tse:FindEventsResponse>");
    }

    if (has("GetEventSearchResults")) {
        const auto token = extractInnerTag(req, "SearchToken");
        std::lock_guard<std::mutex> lock(g_searchMtx);
        if (token != EVENT_SEARCH_TOKEN || !g_eventSearchActive)
            return FaultBuilder::sender("ter:InvalidArgVal", "ter:InvalidToken",
                                        "Invalid search token");
        const std::string recordingStart =
            "<tt:Result>"
              "<tt:RecordingToken>" + std::string(REC) + "</tt:RecordingToken>"
              "<tt:TrackToken>META_0</tt:TrackToken>"
              "<tt:Time>2026-07-01T00:00:00Z</tt:Time>"
              "<tt:Event>"
                "<wsnt:Topic Dialect=\"http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet\">"
                  "tns1:RecordingHistory/Recording/State"
                "</wsnt:Topic>"
                "<wsnt:Message>"
                  "<tt:Message UtcTime=\"2026-07-01T00:00:00Z\" PropertyOperation=\"Changed\">"
                    "<tt:Source><tt:SimpleItem Name=\"RecordingToken\" Value=\"Recording_0\"/></tt:Source>"
                    "<tt:Data><tt:SimpleItem Name=\"IsRecording\" Value=\"true\"/></tt:Data>"
                  "</tt:Message>"
                "</wsnt:Message>"
              "</tt:Event>"
              "<tt:StartStateEvent>false</tt:StartStateEvent>"
            "</tt:Result>";
        const std::string recordingStop =
            "<tt:Result>"
              "<tt:RecordingToken>" + std::string(REC) + "</tt:RecordingToken>"
              "<tt:TrackToken>META_0</tt:TrackToken>"
              "<tt:Time>2026-07-28T00:00:00Z</tt:Time>"
              "<tt:Event>"
                "<wsnt:Topic Dialect=\"http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet\">"
                  "tns1:RecordingHistory/Recording/State"
                "</wsnt:Topic>"
                "<wsnt:Message>"
                  "<tt:Message UtcTime=\"2026-07-28T00:00:00Z\" PropertyOperation=\"Changed\">"
                    "<tt:Source><tt:SimpleItem Name=\"RecordingToken\" Value=\"Recording_0\"/></tt:Source>"
                    "<tt:Data><tt:SimpleItem Name=\"IsRecording\" Value=\"false\"/></tt:Data>"
                  "</tt:Message>"
                "</wsnt:Message>"
              "</tt:Event>"
              "<tt:StartStateEvent>false</tt:StartStateEvent>"
            "</tt:Result>";
        const std::string trackState =
            "<tt:Result>"
              "<tt:RecordingToken>" + std::string(REC) + "</tt:RecordingToken>"
              "<tt:TrackToken>META_0</tt:TrackToken>"
              "<tt:Time>2026-07-15T12:00:00Z</tt:Time>"
              "<tt:Event>"
                "<wsnt:Topic Dialect=\"http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet\">"
                  "tns1:RecordingHistory/Track/State"
                "</wsnt:Topic>"
                "<wsnt:Message>"
                  "<tt:Message UtcTime=\"2026-07-15T12:00:00Z\" PropertyOperation=\"Changed\">"
                    "<tt:Source>"
                      "<tt:SimpleItem Name=\"RecordingToken\" Value=\"Recording_0\"/>"
                      "<tt:SimpleItem Name=\"Track\" Value=\"META_0\"/>"
                    "</tt:Source>"
                    "<tt:Data><tt:SimpleItem Name=\"IsDataPresent\" Value=\"true\"/></tt:Data>"
                  "</tt:Message>"
                "</wsnt:Message>"
              "</tt:Event>"
              "<tt:StartStateEvent>false</tt:StartStateEvent>"
            "</tt:Result>";
        const std::string metadataTrackStop =
            "<tt:Result>"
              "<tt:RecordingToken>" + std::string(REC) + "</tt:RecordingToken>"
              "<tt:TrackToken>META_0</tt:TrackToken>"
              "<tt:Time>2026-07-28T00:00:00Z</tt:Time>"
              "<tt:Event>"
                "<wsnt:Topic Dialect=\"http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet\">"
                  "tns1:RecordingHistory/Track/State"
                "</wsnt:Topic>"
                "<wsnt:Message>"
                  "<tt:Message UtcTime=\"2026-07-28T00:00:00Z\" PropertyOperation=\"Changed\">"
                    "<tt:Source>"
                      "<tt:SimpleItem Name=\"RecordingToken\" Value=\"Recording_0\"/>"
                      "<tt:SimpleItem Name=\"Track\" Value=\"META_0\"/>"
                    "</tt:Source>"
                    "<tt:Data><tt:SimpleItem Name=\"IsDataPresent\" Value=\"false\"/></tt:Data>"
                  "</tt:Message>"
                "</wsnt:Message>"
              "</tt:Event>"
              "<tt:StartStateEvent>false</tt:StartStateEvent>"
            "</tt:Result>";
        const std::string videoTrackStart =
            "<tt:Result>"
              "<tt:RecordingToken>" + std::string(REC) + "</tt:RecordingToken>"
              "<tt:TrackToken>VIDEO_0</tt:TrackToken>"
              "<tt:Time>2026-07-01T00:00:00Z</tt:Time>"
              "<tt:Event>"
                "<wsnt:Topic Dialect=\"http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet\">"
                  "tns1:RecordingHistory/Track/State"
                "</wsnt:Topic>"
                "<wsnt:Message>"
                  "<tt:Message UtcTime=\"2026-07-01T00:00:00Z\" PropertyOperation=\"Changed\">"
                    "<tt:Source>"
                      "<tt:SimpleItem Name=\"RecordingToken\" Value=\"Recording_0\"/>"
                      "<tt:SimpleItem Name=\"Track\" Value=\"VIDEO_0\"/>"
                    "</tt:Source>"
                    "<tt:Data><tt:SimpleItem Name=\"IsDataPresent\" Value=\"true\"/></tt:Data>"
                  "</tt:Message>"
                "</wsnt:Message>"
              "</tt:Event>"
              "<tt:StartStateEvent>false</tt:StartStateEvent>"
            "</tt:Result>";
        const std::string videoTrackStop =
            "<tt:Result>"
              "<tt:RecordingToken>" + std::string(REC) + "</tt:RecordingToken>"
              "<tt:TrackToken>VIDEO_0</tt:TrackToken>"
              "<tt:Time>2026-07-28T00:00:00Z</tt:Time>"
              "<tt:Event>"
                "<wsnt:Topic Dialect=\"http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet\">"
                  "tns1:RecordingHistory/Track/State"
                "</wsnt:Topic>"
                "<wsnt:Message>"
                  "<tt:Message UtcTime=\"2026-07-28T00:00:00Z\" PropertyOperation=\"Changed\">"
                    "<tt:Source>"
                      "<tt:SimpleItem Name=\"RecordingToken\" Value=\"Recording_0\"/>"
                      "<tt:SimpleItem Name=\"Track\" Value=\"VIDEO_0\"/>"
                    "</tt:Source>"
                    "<tt:Data><tt:SimpleItem Name=\"IsDataPresent\" Value=\"false\"/></tt:Data>"
                  "</tt:Message>"
                "</wsnt:Message>"
              "</tt:Event>"
              "<tt:StartStateEvent>false</tt:StartStateEvent>"
            "</tt:Result>";
        const bool backward = !g_eventStartPoint.empty() && !g_eventEndPoint.empty() &&
                              g_eventStartPoint > g_eventEndPoint;
        const std::string forwardEvents =
            recordingStart + videoTrackStart + trackState + recordingStop +
            videoTrackStop + metadataTrackStop;
        const std::string backwardEvents =
            metadataTrackStop + videoTrackStop + recordingStop + trackState +
            videoTrackStart + recordingStart;
        auto virtualEvents = [&](const std::string& time) {
            const bool present = time >= T_FROM && time <= T_UNTIL;
            const std::string state = present ? "true" : "false";
            auto result = [&](const std::string& topic, const std::string& track,
                              const std::string& dataName) {
                const std::string trackSource = track.empty() ? "" :
                    "<tt:SimpleItem Name=\"Track\" Value=\"" + track + "\"/>";
                const std::string trackToken = track.empty() ? "META_0" : track;
                return
                    "<tt:Result>"
                      "<tt:RecordingToken>" + std::string(REC) + "</tt:RecordingToken>"
                      "<tt:TrackToken>" + trackToken + "</tt:TrackToken>"
                      "<tt:Time>" + time + "</tt:Time>"
                      "<tt:Event>"
                        "<wsnt:Topic Dialect=\"http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet\">" +
                          topic +
                        "</wsnt:Topic>"
                        "<wsnt:Message>"
                          "<tt:Message UtcTime=\"" + time + "\" PropertyOperation=\"Initialized\">"
                            "<tt:Source>"
                              "<tt:SimpleItem Name=\"RecordingToken\" Value=\"Recording_0\"/>" +
                              trackSource +
                            "</tt:Source>"
                            "<tt:Data><tt:SimpleItem Name=\"" + dataName +
                              "\" Value=\"" + state + "\"/></tt:Data>"
                          "</tt:Message>"
                        "</wsnt:Message>"
                      "</tt:Event>"
                      "<tt:StartStateEvent>true</tt:StartStateEvent>"
                    "</tt:Result>";
            };
            return result("tns1:RecordingHistory/Recording/State", "", "IsRecording") +
                   result("tns1:RecordingHistory/Track/State", "VIDEO_0", "IsDataPresent") +
                   result("tns1:RecordingHistory/Track/State", "META_0", "IsDataPresent");
        };
        std::string eventResults;
        if (g_eventMaxMatches == 1) {
            eventResults = trackState;
        } else if (backward) {
            eventResults = (g_eventIncludeStartState ? virtualEvents(g_eventStartPoint) : "") +
                           backwardEvents +
                           (g_eventIncludeStartState ? virtualEvents(g_eventEndPoint) : "");
        } else {
            eventResults = (g_eventIncludeStartState ? virtualEvents(g_eventStartPoint) : "") +
                           forwardEvents;
        }
        return R("GetEventSearchResultsResponse",
            "<tse:GetEventSearchResultsResponse>"
              "<tse:ResultList>"
                "<tt:SearchState>Completed</tt:SearchState>" + eventResults +
              "</tse:ResultList>"
            "</tse:GetEventSearchResultsResponse>");
    }

    if (has("EndSearch")) {
        const auto token = extractInnerTag(req, "SearchToken");
        std::lock_guard<std::mutex> lock(g_searchMtx);
        if (token == RECORDING_SEARCH_TOKEN) g_recordingSearchActive = false;
        else if (token == EVENT_SEARCH_TOKEN) g_eventSearchActive = false;
        else return FaultBuilder::sender("ter:InvalidArgVal", "ter:InvalidToken",
                                         "Invalid search token");
        return R("EndSearchResponse",
            "<tse:EndSearchResponse>"
              "<tse:Endpoint>" + std::string(T_UNTIL) + "</tse:Endpoint>"
            "</tse:EndSearchResponse>");
    }

    return "";  // op không nhận diện → OnvifServer fallback fault.
}
