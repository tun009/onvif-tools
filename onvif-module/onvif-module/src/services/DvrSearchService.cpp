// DvrSearchService.cpp — ONVIF Recording Search (Profile G) trên dữ liệu ghi thật của DVR.
#include "services/DvrSearchService.h"

#include "services/SearchSupport.h"
#include "utils/FaultBuilder.h"

#include <algorithm>
#include <utility>

using namespace searchsupport;

namespace {

constexpr const char* kTopicRecording = "RecordingHistory/Recording/State";
constexpr const char* kTopicTrack = "RecordingHistory/Track/State";
constexpr std::size_t kMaxSessions = 16;
constexpr std::size_t kDefaultPage = 1000;           // số kết quả tối đa mỗi lần Get khi client không đặt MaxResults
constexpr int64_t kOngoingMs = 90000;                // đoạn cuối kết thúc gần đây hơn mức này + đang ghi → chưa "đóng"
constexpr int64_t kForever = INT64_MAX / 4;

} // namespace

// ── RecData ───────────────────────────────────────────────────────────

bool DvrSearchService::RecData::hasData() const {
    for (const auto& t : tracks) if (!t.ranges.empty()) return true;
    return false;
}
int64_t DvrSearchService::RecData::earliest() const {
    int64_t best = kForever;
    for (const auto& t : tracks) if (!t.ranges.empty()) best = std::min(best, t.ranges.front().fromMs);
    return best;
}
int64_t DvrSearchService::RecData::latest() const {
    int64_t best = 0;
    for (const auto& t : tracks) if (!t.ranges.empty()) best = std::max(best, t.ranges.back().toMs);
    return best;
}

// ── Khởi tạo, dispatch, tiện ích ──────────────────────────────────────

DvrSearchService::DvrSearchService(DvrRecordingService& recording, std::shared_ptr<RecordingIndex> index)
    : recording_(recording), index_(std::move(index)) {
    char prefix[32];
    std::snprintf(prefix, sizeof(prefix), "search_%llx", static_cast<unsigned long long>(epochNowMs() / 1000));
    tokenPrefix_ = prefix;
}

std::string DvrSearchService::reply(const std::string& rel, const char* op, const std::string& body) {
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
          "<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\""
          " xmlns:wsa=\"http://www.w3.org/2005/08/addressing\""
          " xmlns:tse=\"http://www.onvif.org/ver10/search/wsdl\""
          " xmlns:tt=\"http://www.onvif.org/ver10/schema\""
          " xmlns:wsnt=\"http://docs.oasis-open.org/wsn/b-2\""
          " xmlns:tns1=\"http://www.onvif.org/ver10/topics\""
          " xmlns:ter=\"http://www.onvif.org/ver10/error\">"
          "<SOAP-ENV:Header><wsa:Action>http://www.onvif.org/ver10/search/wsdl/SearchPort/"
       << op << "</wsa:Action>";
    if (!rel.empty()) os << "<wsa:RelatesTo>" << rel << "</wsa:RelatesTo>";
    os << "</SOAP-ENV:Header><SOAP-ENV:Body>" << body << "</SOAP-ENV:Body></SOAP-ENV:Envelope>";
    return os.str();
}

std::string DvrSearchService::faultToken() {
    return FaultBuilder::sender("ter:InvalidArgVal", "ter:InvalidToken", "Invalid token");
}
std::string DvrSearchService::faultBackend() {
    return FaultBuilder::receiver("ter:Action", "Recording backend unavailable");
}

std::string DvrSearchService::handle(const std::string& req) {
    const std::string op = Svc::opName(req);
    const std::string rel = Svc::textOf(req, "MessageID");

    // Phải trùng <tse:Capabilities> trong GetServices (DeviceService.cpp, SEARCH-1-1-2).
    // GeneralStartEvents=true: sự kiện ảo duy nhất ta có là trạng thái Recording/Track (bắt buộc).
    if (op == "GetServiceCapabilities")
        return reply(rel, "GetServiceCapabilitiesResponse",
                     "<tse:GetServiceCapabilitiesResponse>"
                     "<tse:Capabilities MetadataSearch=\"false\" GeneralStartEvents=\"true\"/>"
                     "</tse:GetServiceCapabilitiesResponse>");
    if (op == "GetRecordingSummary")         return getRecordingSummary(rel);
    if (op == "GetRecordingInformation")     return getRecordingInformation(req, rel);
    if (op == "GetMediaAttributes")          return getMediaAttributes(req, rel);
    if (op == "FindRecordings")              return findRecordings(req, rel);
    if (op == "GetRecordingSearchResults")   return getResults(req, rel, false);
    if (op == "FindEvents")                  return findEvents(req, rel);
    if (op == "GetEventSearchResults")       return getResults(req, rel, true);
    if (op == "EndSearch")                   return endSearch(req, rel);
    return "";   // FindPTZPosition, FindMetadata... không hỗ trợ → OnvifServer trả fault mặc định
}

// ── Dữ liệu ───────────────────────────────────────────────────────────

std::vector<DvrSearchService::RecData> DvrSearchService::collect(const std::vector<Rec>& catalog) {
    std::vector<RecData> out;
    for (const auto& rec : catalog) {
        RecData data;
        data.rec = &rec;
        for (const auto& track : rec.tracks) {
            TrackData td;
            td.track = &track;
            for (TimeRange r : index_->ranges(rec.videoSourceId, track.streamType)) {
                r.fromMs = floorSec(r.fromMs);   // làm tròn ra ngoài: mọi mốc là số giây nguyên
                r.toMs = ceilSec(r.toMs);
                td.ranges.push_back(r);
            }
            data.tracks.push_back(std::move(td));
        }
        out.push_back(std::move(data));
    }
    return out;
}

bool DvrSearchService::resolveScope(const std::string& req, const std::vector<Rec>& catalog, Scope& out,
                                    std::string& fault) {
    const std::string scope = Svc::blockOf(req, "Scope");
    const std::vector<std::string> wantedRecordings = allTexts(scope, "IncludedRecordings");
    std::vector<std::string> wantedSources;
    for (const auto& block : allBlocks(scope, "IncludedSources"))
        for (const auto& token : allTexts(block, "Token")) wantedSources.push_back(token);
    out.filter = trimmed(Svc::unesc(Svc::textOf(scope, "RecordingInformationFilter")));

    for (const auto& token : wantedRecordings) {
        const bool known = std::any_of(catalog.begin(), catalog.end(), [&](const Rec& r) { return r.token == token; });
        if (!known) { fault = faultToken(); return false; }
    }
    for (const auto& token : wantedSources) {
        const bool known = std::any_of(catalog.begin(), catalog.end(), [&](const Rec& r) { return r.sourceId == token; });
        if (!known) {
            fault = FaultBuilder::sender("ter:InvalidArgVal", "ter:InvalidSource", "Invalid recording source");
            return false;
        }
    }

    const bool everything = wantedRecordings.empty() && wantedSources.empty();
    const std::vector<RecData> all = collect(catalog);
    out.recordings.clear();
    for (const auto& data : all) {   // hợp của hai danh sách (spec 5.2.4.1)
        const Rec& r = *data.rec;
        const bool picked = everything ||
            std::find(wantedRecordings.begin(), wantedRecordings.end(), r.token) != wantedRecordings.end() ||
            std::find(wantedSources.begin(), wantedSources.end(), r.sourceId) != wantedSources.end();
        if (!picked) continue;
        bool understood = true;
        if (!matchesFilter(data, out.filter, understood)) {
            if (!understood) {
                fault = FaultBuilder::sender("ter:InvalidArgVal", "ter:InvalidFilterFault",
                                             "Unsupported recording information filter");
                return false;
            }
            continue;
        }
        out.recordings.push_back(data);
    }
    return true;
}

bool DvrSearchService::matchesFilter(const RecData& data, const std::string& xpath, bool& understood) {
    understood = true;
    if (xpath.empty()) return true;
    std::set<std::string> types;   // chỉ track CÓ dữ liệu mới được tính (spec 5.3.3)
    for (const auto& t : data.tracks) if (!t.ranges.empty()) types.insert("Video");
    FilterParser parser{xpath, types};
    const bool value = parser.orExpr();
    parser.ws();
    if (!parser.ok || parser.pos != xpath.size()) { understood = false; return false; }
    return value;
}

// ── XML ───────────────────────────────────────────────────────────────

std::string DvrSearchService::trackInformation(const TrackData& data) {
    return "<tt:TrackToken>" + data.track->token + "</tt:TrackToken><tt:TrackType>Video</tt:TrackType>"
           "<tt:Description>" + Svc::esc(data.track->description) + "</tt:Description>"
           "<tt:DataFrom>" + formatTime(data.ranges.front().fromMs) + "</tt:DataFrom>"
           "<tt:DataTo>" + formatTime(data.ranges.back().toMs) + "</tt:DataTo>";
}

std::string DvrSearchService::recordingInformation(const RecData& data) const {
    std::string xml = "<tt:RecordingToken>" + data.rec->token + "</tt:RecordingToken>" + data.rec->sourceXml;
    if (data.hasData())
        xml += "<tt:EarliestRecording>" + formatTime(data.earliest()) + "</tt:EarliestRecording>"
               "<tt:LatestRecording>" + formatTime(data.latest()) + "</tt:LatestRecording>";
    xml += data.rec->contentXml;
    bool recording = false;
    for (const auto& t : data.tracks) {
        recording = recording || t.track->isRecording;
        if (!t.ranges.empty()) xml += "<tt:Track>" + trackInformation(t) + "</tt:Track>";
    }
    // Initiated = chưa từng có dữ liệu; Stopped = có dữ liệu nhưng đang không ghi.
    xml += std::string("<tt:RecordingStatus>") +
           (recording ? "Recording" : data.hasData() ? "Stopped" : "Initiated") + "</tt:RecordingStatus>";
    return xml;
}

std::string DvrSearchService::eventXml(const std::string& recording, const std::string& track, int64_t timeMs,
                                       const char* topic, const char* operation, const char* dataName,
                                       bool value, bool startState) {
    const std::string time = formatTime(timeMs);
    const bool trackTopic = std::string(topic) == kTopicTrack;
    return "<tt:Result><tt:RecordingToken>" + recording + "</tt:RecordingToken>"
           "<tt:TrackToken>" + track + "</tt:TrackToken><tt:Time>" + time + "</tt:Time>"
           "<tt:Event><wsnt:Topic Dialect=\"http://www.onvif.org/ver10/tev/topicExpression/ConcreteSet\">tns1:" +
           topic + "</wsnt:Topic><wsnt:Message>"
           "<tt:Message UtcTime=\"" + time + "\" PropertyOperation=\"" + operation + "\">"
           "<tt:Source><tt:SimpleItem Name=\"RecordingToken\" Value=\"" + recording + "\"/>" +
           (trackTopic ? "<tt:SimpleItem Name=\"Track\" Value=\"" + track + "\"/>" : "") + "</tt:Source>"
           "<tt:Data><tt:SimpleItem Name=\"" + dataName + "\" Value=\"" + (value ? "true" : "false") + "\"/></tt:Data>"
           "</tt:Message></wsnt:Message></tt:Event>"
           "<tt:StartStateEvent>" + (startState ? "true" : "false") + "</tt:StartStateEvent></tt:Result>";
}

// Sự kiện lịch sử của 1 recording trong [lo, hi], tăng dần theo thời gian.
std::vector<std::pair<int64_t, std::string>> DvrSearchService::historyEvents(
    const RecData& data, int64_t lo, int64_t hi, const std::string& topicFilter) const {
    std::vector<std::pair<int64_t, std::string>> out;
    const int64_t now = epochNowMs();
    const std::string& rec = data.rec->token;
    const bool wantRecording = topicMatches(topicFilter, kTopicRecording);
    const bool wantTrack = topicMatches(topicFilter, kTopicTrack);
    // Đoạn cuối còn "mở" nếu luồng đang ghi và đoạn vừa kết thúc (file kế tiếp chưa đóng).
    auto stillOpen = [&](const TimeRange& r, bool recording) { return recording && now - r.toMs < kOngoingMs; };

    std::string anyTrack = data.tracks.empty() ? "" : data.tracks.front().track->token;
    std::vector<TimeRange> all;
    bool anyRecording = false;
    for (const auto& t : data.tracks) {
        anyRecording = anyRecording || t.track->isRecording;
        all.insert(all.end(), t.ranges.begin(), t.ranges.end());
        if (!wantTrack) continue;
        for (std::size_t i = 0; i < t.ranges.size(); ++i) {
            const TimeRange& r = t.ranges[i];
            const bool last = i + 1 == t.ranges.size();
            if (r.fromMs >= lo && r.fromMs <= hi)
                out.push_back({r.fromMs, eventXml(rec, t.track->token, r.fromMs, kTopicTrack, "Changed",
                                                  "IsDataPresent", true, false)});
            if (r.toMs >= lo && r.toMs <= hi && !(last && stillOpen(r, t.track->isRecording)))
                out.push_back({r.toMs, eventXml(rec, t.track->token, r.toMs, kTopicTrack, "Changed",
                                                "IsDataPresent", false, false)});
        }
    }
    if (wantRecording) {
        const std::vector<TimeRange> slices = mergeRanges(all);
        for (std::size_t i = 0; i < slices.size(); ++i) {
            const TimeRange& r = slices[i];
            const bool last = i + 1 == slices.size();
            if (r.fromMs >= lo && r.fromMs <= hi)
                out.push_back({r.fromMs, eventXml(rec, anyTrack, r.fromMs, kTopicRecording, "Changed",
                                                  "IsRecording", true, false)});
            if (r.toMs >= lo && r.toMs <= hi && !(last && stillOpen(r, anyRecording)))
                out.push_back({r.toMs, eventXml(rec, anyTrack, r.toMs, kTopicRecording, "Changed",
                                                "IsRecording", false, false)});
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return out;
}

// ── Thao tác tra cứu đơn ──────────────────────────────────────────────

std::string DvrSearchService::getRecordingSummary(const std::string& rel) {
    std::vector<Rec> catalog;
    if (!recording_.catalog(catalog)) return faultBackend();
    int64_t from = kForever, until = 0;
    for (const auto& data : collect(catalog)) {
        if (!data.hasData()) continue;
        from = std::min(from, data.earliest());
        until = std::max(until, data.latest());
    }
    if (until == 0) from = until = epochNowMs();   // chưa có dữ liệu: spec cho phép bỏ qua hai giá trị
    return reply(rel, "GetRecordingSummaryResponse",
                 "<tse:GetRecordingSummaryResponse><tse:Summary><tt:DataFrom>" + formatTime(from) +
                 "</tt:DataFrom><tt:DataUntil>" + formatTime(until) + "</tt:DataUntil><tt:NumberRecordings>" +
                 std::to_string(catalog.size()) + "</tt:NumberRecordings></tse:Summary>"
                 "</tse:GetRecordingSummaryResponse>");
}

std::string DvrSearchService::getRecordingInformation(const std::string& req, const std::string& rel) {
    std::vector<Rec> catalog;
    if (!recording_.catalog(catalog)) return faultBackend();
    const std::string token = trimmed(Svc::textOf(req, "RecordingToken"));
    for (const auto& data : collect(catalog))
        if (data.rec->token == token)
            return reply(rel, "GetRecordingInformationResponse",
                         "<tse:GetRecordingInformationResponse><tse:RecordingInformation>" +
                         recordingInformation(data) + "</tse:RecordingInformation></tse:GetRecordingInformationResponse>");
    return faultToken();
}

std::string DvrSearchService::getMediaAttributes(const std::string& req, const std::string& rel) {
    std::vector<Rec> catalog;
    if (!recording_.catalog(catalog)) return faultBackend();
    int64_t at = 0;
    if (!parseDateTime(trimmed(Svc::textOf(req, "Time")), at))
        return FaultBuilder::invalidArgVal("Time is missing or not a valid dateTime");
    const std::vector<std::string> wanted = allTexts(req, "RecordingTokens");
    for (const auto& token : wanted)
        if (std::none_of(catalog.begin(), catalog.end(), [&](const Rec& r) { return r.token == token; }))
            return faultToken();

    std::string body;
    for (const auto& data : collect(catalog)) {
        if (!wanted.empty() && std::find(wanted.begin(), wanted.end(), data.rec->token) == wanted.end()) continue;
        std::string tracks;
        int64_t from = kForever, until = 0;
        for (const auto& t : data.tracks) {
            for (const auto& r : t.ranges) {   // đoạn liên tục chứa thời điểm hỏi
                if (r.fromMs > at || at > r.toMs) continue;
                from = std::min(from, r.fromMs);
                until = std::max(until, r.toMs);
                // Thông số là cấu hình encoder hiện tại; Encoding khớp capability Recording (H264).
                tracks += "<tt:TrackAttributes><tt:TrackInformation>" + trackInformation(t) +
                          "</tt:TrackInformation><tt:VideoAttributes><tt:Bitrate>" + std::to_string(t.track->bitrate) +
                          "</tt:Bitrate><tt:Width>" + std::to_string(t.track->width) + "</tt:Width><tt:Height>" +
                          std::to_string(t.track->height) + "</tt:Height><tt:Encoding>H264</tt:Encoding>"
                          "<tt:Framerate>" + std::to_string(t.track->framerate) + "</tt:Framerate>"
                          "</tt:VideoAttributes></tt:TrackAttributes>";
                break;
            }
        }
        if (tracks.empty()) continue;   // mỗi recording cho 0 hoặc 1 MediaAttributes
        body += "<tse:MediaAttributes><tt:RecordingToken>" + data.rec->token + "</tt:RecordingToken>" + tracks +
                "<tt:From>" + formatTime(from) + "</tt:From><tt:Until>" + formatTime(until) + "</tt:Until>"
                "</tse:MediaAttributes>";
    }
    return reply(rel, "GetMediaAttributesResponse", "<tse:GetMediaAttributesResponse>" + body + "</tse:GetMediaAttributesResponse>");
}

// ── Phiên tìm kiếm ────────────────────────────────────────────────────

void DvrSearchService::purgeExpiredLocked(int64_t nowMs) {
    for (auto it = sessions_.begin(); it != sessions_.end();)
        it = it->second.expiresMs <= nowMs ? sessions_.erase(it) : std::next(it);
}

// Trả token phiên mới; rỗng khi đã đủ số phiên.
std::string DvrSearchService::newSessionLocked(Session session) {
    const int64_t now = steadyNowMs();
    purgeExpiredLocked(now);
    if (sessions_.size() >= kMaxSessions) return "";
    session.expiresMs = now + session.keepAliveMs;
    const std::string token = tokenPrefix_ + "_" + std::to_string(++counter_);   // không bao giờ dùng lại
    sessions_[token] = std::move(session);
    return token;
}

namespace {
// KeepAliveTime: thiết bị phải hỗ trợ tới 10 s (spec); tôn trọng giá trị client trong [1 s, 10 phút].
int64_t keepAliveOf(const std::string& req) {
    const int64_t ms = parseDurationMs(trimmed(Svc::textOf(req, "KeepAliveTime")));
    return ms < 0 ? 10000 : std::min<int64_t>(std::max<int64_t>(ms, 1000), 600000);
}
int64_t intOf(const std::string& req, const char* name) {
    const std::string text = trimmed(Svc::textOf(req, name));
    return text.empty() ? 0 : std::strtoll(text.c_str(), nullptr, 10);
}
} // namespace

std::string DvrSearchService::findRecordings(const std::string& req, const std::string& rel) {
    std::vector<Rec> catalog;
    if (!recording_.catalog(catalog)) return faultBackend();
    Scope scope;
    std::string fault;
    if (!resolveScope(req, catalog, scope, fault)) return fault;

    Session session;
    session.keepAliveMs = keepAliveOf(req);
    const int64_t maxMatches = intOf(req, "MaxMatches");
    for (const auto& data : scope.recordings) {
        if (maxMatches > 0 && static_cast<int64_t>(session.items.size()) >= maxMatches) break;
        session.items.push_back("<tt:RecordingInformation>" + recordingInformation(data) + "</tt:RecordingInformation>");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string token = newSessionLocked(std::move(session));
    if (token.empty()) return FaultBuilder::receiver("ter:Action", "Unable to create a new search session");
    return reply(rel, "FindRecordingsResponse",
                 "<tse:FindRecordingsResponse><tse:SearchToken>" + token + "</tse:SearchToken></tse:FindRecordingsResponse>");
}

std::string DvrSearchService::findEvents(const std::string& req, const std::string& rel) {
    int64_t start = 0, end = 0;
    if (!parseDateTime(trimmed(Svc::textOf(req, "StartPoint")), start))
        return FaultBuilder::invalidArgVal("StartPoint is missing or not a valid dateTime");
    const std::string endText = trimmed(Svc::textOf(req, "EndPoint"));
    const bool hasEnd = !endText.empty();
    if (hasEnd && !parseDateTime(endText, end))
        return FaultBuilder::invalidArgVal("EndPoint is not a valid dateTime");

    std::vector<Rec> catalog;
    if (!recording_.catalog(catalog)) return faultBackend();
    Scope scope;
    std::string fault;
    if (!resolveScope(req, catalog, scope, fault)) return fault;

    // MessageContent (XPath trên nội dung sự kiện) không được áp dụng: ta chỉ có hai topic lịch sử.
    const std::string topicFilter = Svc::unesc(Svc::textOf(Svc::blockOf(req, "SearchFilter"), "TopicExpression"));
    const bool includeStart = trimmed(Svc::textOf(req, "IncludeStartState")) == "true" ||
                              trimmed(Svc::textOf(req, "IncludeStartState")) == "1";
    const bool backward = hasEnd && end < start;
    const int64_t lo = hasEnd ? std::min(start, end) : start;
    const int64_t hi = hasEnd ? std::max(start, end) : kForever;

    std::vector<std::pair<int64_t, std::string>> events;
    for (const auto& data : scope.recordings) {
        auto part = historyEvents(data, lo, hi, topicFilter);
        events.insert(events.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
    }
    std::stable_sort(events.begin(), events.end(), [&](const auto& a, const auto& b) {
        return backward ? a.first > b.first : a.first < b.first;
    });

    // Sự kiện ảo mô tả trạng thái tại một thời điểm: bắt buộc cho recording event (spec 5.9).
    auto virtualAt = [&](int64_t t) {
        std::vector<std::pair<int64_t, std::string>> out;
        for (const auto& data : scope.recordings) {
            std::vector<TimeRange> all;
            for (const auto& tr : data.tracks) all.insert(all.end(), tr.ranges.begin(), tr.ranges.end());
            const std::string anyTrack = data.tracks.empty() ? "" : data.tracks.front().track->token;
            if (topicMatches(topicFilter, kTopicRecording))
                out.push_back({t, eventXml(data.rec->token, anyTrack, t, kTopicRecording, "Initialized",
                                           "IsRecording", containsTime(all, t), true)});
            if (topicMatches(topicFilter, kTopicTrack))
                for (const auto& tr : data.tracks)
                    out.push_back({t, eventXml(data.rec->token, tr.track->token, t, kTopicTrack, "Initialized",
                                               "IsDataPresent", containsTime(tr.ranges, t), true)});
        }
        return out;
    };

    std::vector<std::pair<int64_t, std::string>> results;
    auto append = [&](std::vector<std::pair<int64_t, std::string>> part) {
        results.insert(results.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
    };
    if (includeStart) append(virtualAt(start));
    append(std::move(events));
    if (includeStart && backward) append(virtualAt(end));   // lùi: trạng thái tại cả hai đầu

    Session session;
    session.events = true;
    session.keepAliveMs = keepAliveOf(req);
    session.startPointMs = start;
    session.endPointMs = end;
    session.hasEndPoint = hasEnd;
    const int64_t maxMatches = intOf(req, "MaxMatches");
    for (auto& r : results) {
        if (maxMatches > 0 && static_cast<int64_t>(session.items.size()) >= maxMatches) break;
        session.times.push_back(r.first);
        session.items.push_back(std::move(r.second));
    }
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string token = newSessionLocked(std::move(session));
    if (token.empty()) return FaultBuilder::receiver("ter:Action", "Unable to create a new search session");
    return reply(rel, "FindEventsResponse",
                 "<tse:FindEventsResponse><tse:SearchToken>" + token + "</tse:SearchToken></tse:FindEventsResponse>");
}

std::string DvrSearchService::getResults(const std::string& req, const std::string& rel, bool events) {
    const std::string token = trimmed(Svc::textOf(req, "SearchToken"));
    const int64_t maxResults = intOf(req, "MaxResults");
    std::lock_guard<std::mutex> lock(mutex_);
    const int64_t now = steadyNowMs();
    purgeExpiredLocked(now);
    const auto it = sessions_.find(token);
    if (it == sessions_.end() || it->second.events != events) return faultToken();
    Session& session = it->second;
    session.expiresMs = now + session.keepAliveMs;

    // Kết quả đã dựng sẵn khi Find nên không bao giờ phải chờ MinResults/WaitTime (spec cho phép
    // trả sớm khi tìm đã xong).
    const std::size_t remaining = session.items.size() - session.next;
    const std::size_t take = std::min<std::size_t>(remaining, maxResults > 0 ? static_cast<std::size_t>(maxResults) : kDefaultPage);
    std::string items;
    for (std::size_t i = 0; i < take; ++i) items += session.items[session.next + i];
    session.next += take;
    const char* state = session.next >= session.items.size() ? "Completed" : "Searching";

    const char* op = events ? "GetEventSearchResultsResponse" : "GetRecordingSearchResultsResponse";
    return reply(rel, op, std::string("<tse:") + op + "><tse:ResultList><tt:SearchState>" + state +
                              "</tt:SearchState>" + items + "</tse:ResultList></tse:" + op + ">");
}

std::string DvrSearchService::endSearch(const std::string& req, const std::string& rel) {
    const std::string token = trimmed(Svc::textOf(req, "SearchToken"));
    std::lock_guard<std::mutex> lock(mutex_);
    purgeExpiredLocked(steadyNowMs());
    const auto it = sessions_.find(token);
    if (it == sessions_.end()) return faultToken();
    const Session& s = it->second;

    // Điểm tìm đã tới (spec 5.15): chưa bắt đầu → StartPoint; dở dang → thời gian kết quả cuối đã giao;
    // xong → EndPoint gốc (không có EndPoint = tìm tới hiện tại). Tìm recording không có mốc → hiện tại.
    int64_t point = epochNowMs();
    if (s.events) {
        if (s.next == 0 && !s.items.empty()) point = s.startPointMs;
        else if (s.next < s.items.size()) point = s.times[s.next - 1];
        else if (s.hasEndPoint) point = s.endPointMs;
    }
    sessions_.erase(it);
    return reply(rel, "EndSearchResponse",
                 "<tse:EndSearchResponse><tse:Endpoint>" + formatTime(point) + "</tse:Endpoint></tse:EndSearchResponse>");
}
