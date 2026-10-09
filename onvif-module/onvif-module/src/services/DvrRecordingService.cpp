// DvrRecordingService.cpp — Recording Control trên DVR thật: dispatch, truy cập DVR, helper XML,
// Recording / Track / Options. Phần Recording Job nằm ở DvrRecordingJobs.cpp.
#include "services/DvrRecordingService.h"

#include "utils/FaultBuilder.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <exception>
#include <sstream>
#include <utility>

namespace {

// Chuỗi client đặt tối đa bấy nhiêu ký tự: chặn dữ liệu rác phình file lưu. Từ chối thay vì
// cắt để Get luôn trả đúng thứ đã Set.
constexpr std::size_t kMaxTextLength = 4096;

std::atomic<bool> g_realRunning{false};        // đã có DvrRecordingService đang chạy
std::atomic<std::size_t> g_knownSources{1};    // số sensor lần đọc DVR thành công gần nhất

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

// Tên local của thẻ bắt đầu tại `open`; rỗng nếu là thẻ đóng/chú thích/khai báo.
std::string openTagName(const std::string& xml, std::size_t open, std::size_t& nameEnd) {
    const std::size_t start = open + 1;
    if (start >= xml.size() || xml[start] == '/' || xml[start] == '!' || xml[start] == '?') {
        nameEnd = start;
        return "";
    }
    nameEnd = xml.find_first_of(" >/\t\r\n", start);
    if (nameEnd == std::string::npos) { nameEnd = xml.size(); return ""; }
    const auto colon = xml.rfind(':', nameEnd);
    const std::size_t local = (colon != std::string::npos && colon >= start) ? colon + 1 : start;
    return xml.substr(local, nameEnd - local);
}

std::size_t findOpenTag(const std::string& xml, const std::string& name) {
    std::size_t open = 0;
    while ((open = xml.find('<', open)) != std::string::npos) {
        std::size_t nameEnd = 0;
        if (openTagName(xml, open, nameEnd) == name) return open;
        open = nameEnd > open ? nameEnd : open + 1;
    }
    return std::string::npos;
}

} // namespace

// ── Cắt chuỗi / envelope ──────────────────────────────────────────────

std::string DvrRecordingService::opName(const std::string& request) {
    const std::size_t body = findOpenTag(request, "Body");
    if (body == std::string::npos) return "";
    std::size_t open = request.find('>', body);
    while (open != std::string::npos && (open = request.find('<', open + 1)) != std::string::npos) {
        std::size_t nameEnd = 0;
        const std::string name = openTagName(request, open, nameEnd);
        if (!name.empty()) return name;
        if (open + 1 < request.size() && request[open + 1] == '/') return "";   // Body rỗng
    }
    return "";
}

std::string DvrRecordingService::textOf(const std::string& xml, const std::string& name) {
    const std::size_t open = findOpenTag(xml, name);
    if (open == std::string::npos) return "";
    const std::size_t gt = xml.find('>', open);
    if (gt == std::string::npos || xml[gt - 1] == '/') return "";
    const std::size_t lt = xml.find('<', gt + 1);
    // Văn bản thuần phải kết thúc bằng thẻ đóng; gặp thẻ mở con → coi như rỗng.
    if (lt == std::string::npos || lt + 1 >= xml.size() || xml[lt + 1] != '/') return "";
    return trim(xml.substr(gt + 1, lt - gt - 1));
}

std::string DvrRecordingService::blockOf(const std::string& xml, const std::string& name) {
    const std::size_t open = findOpenTag(xml, name);
    if (open == std::string::npos) return "";
    const std::size_t gt = xml.find('>', open);
    if (gt == std::string::npos) return "";
    if (xml[gt - 1] == '/') return xml.substr(open, gt - open + 1);   // thẻ tự đóng
    for (std::size_t close = gt; (close = xml.find("</", close + 1)) != std::string::npos;) {
        const std::size_t end = xml.find('>', close);
        if (end == std::string::npos) return "";
        const std::string closing = trim(xml.substr(close + 2, end - close - 2));
        const auto colon = closing.rfind(':');
        if ((colon == std::string::npos ? closing : closing.substr(colon + 1)) == name)
            return xml.substr(open, end - open + 1);
    }
    return "";
}

std::string DvrRecordingService::attrOf(const std::string& element, const std::string& name) {
    const std::string tag = element.substr(0, element.find('>'));
    for (const char quote : {'"', '\''}) {
        const std::string key = name + "=" + quote;
        for (std::size_t at = 0; (at = tag.find(key, at)) != std::string::npos; at += key.size()) {
            // chỉ nhận khi đứng ngay sau khoảng trắng (tránh trúng đuôi thuộc tính khác)
            if (at == 0 || std::string(" \t\r\n").find(tag[at - 1]) == std::string::npos) continue;
            const std::size_t valueStart = at + key.size();
            const std::size_t valueEnd = tag.find(quote, valueStart);
            return valueEnd == std::string::npos ? "" : tag.substr(valueStart, valueEnd - valueStart);
        }
    }
    return "";
}

std::string DvrRecordingService::reply(const std::string& rel, const char* op, const std::string& body) {
    std::ostringstream os;
    os << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
          "<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\""
          " xmlns:wsa=\"http://www.w3.org/2005/08/addressing\""
          " xmlns:trc=\"http://www.onvif.org/ver10/recording/wsdl\""
          " xmlns:tt=\"http://www.onvif.org/ver10/schema\""
          " xmlns:ter=\"http://www.onvif.org/ver10/error\">"
          "<SOAP-ENV:Header><wsa:Action>http://www.onvif.org/ver10/recording/wsdl/RecordingPort/"
       << op << "</wsa:Action>";
    if (!rel.empty()) os << "<wsa:RelatesTo>" << rel << "</wsa:RelatesTo>";
    os << "</SOAP-ENV:Header><SOAP-ENV:Body>" << body << "</SOAP-ENV:Body></SOAP-ENV:Envelope>";
    return os.str();
}

std::string DvrRecordingService::esc(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (const char raw : text) {
        switch (raw) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   // bỏ ký tự điều khiển không hợp lệ trong XML 1.0 (trừ tab, xuống dòng)
                if (static_cast<unsigned char>(raw) >= 0x20 || raw == '\t' || raw == '\n' || raw == '\r')
                    out += raw;
        }
    }
    return out;
}

std::string DvrRecordingService::unesc(const std::string& text) {
    static const struct { const char* entity; char ch; } kEntities[] = {
        {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''}};
    std::string out;
    for (std::size_t i = 0; i < text.size();) {
        bool matched = false;
        if (text[i] == '&') {
            for (const auto& e : kEntities) {
                const std::string entity = e.entity;
                if (text.compare(i, entity.size(), entity) != 0) continue;
                out += e.ch;
                i += entity.size();
                matched = true;
                break;
            }
        }
        if (!matched) out += text[i++];
    }
    return out;
}

std::string DvrRecordingService::faultNoRecording() {
    return FaultBuilder::sender("ter:InvalidArgVal", "ter:NoRecording", "No recording with the given token");
}
std::string DvrRecordingService::faultNoTrack() {
    return FaultBuilder::sender("ter:InvalidArgVal", "ter:NoTrack", "No track with the given token");
}
std::string DvrRecordingService::faultNoJob() {
    return FaultBuilder::sender("ter:InvalidArgVal", "ter:NoRecordingJob", "No recording job with the given token");
}
std::string DvrRecordingService::faultBadConfig(const char* reason) {
    return FaultBuilder::sender("ter:InvalidArgVal", "ter:BadConfiguration", reason);
}
std::string DvrRecordingService::faultBackend() {
    return FaultBuilder::receiver("ter:Action", "Recording backend unavailable");
}

// ── Khởi tạo, capability, dispatch ────────────────────────────────────

const DvrRecordingService::Stream* DvrRecordingService::Source::byProfile(const std::string& token) const {
    for (const auto& s : streams) if (s.profileToken == token) return &s;
    return nullptr;
}
const DvrRecordingService::Stream* DvrRecordingService::Source::byTrack(const std::string& token) const {
    for (const auto& s : streams) if (s.trackToken == token) return &s;
    return nullptr;
}

DvrRecordingService::DvrRecordingService(std::shared_ptr<IDvrClient> dvr,
                                         std::shared_ptr<RecordingJobStore> store,
                                         std::string serviceAddress)
    : dvr_(std::move(dvr)), store_(std::move(store)), serviceAddress_(std::move(serviceAddress)) {
    // Gieo số sensor thật cho Capabilities; DVR chưa sẵn sàng thì giữ mặc định, thao tác sau cập nhật.
    std::vector<Source> sources;
    std::string ignored;
    loadSources(sources, ignored);

    g_realRunning = true;
    MockSubscriptionManager::getInstance().setRecordingJobProvider([this] { return initialJobEvents(); });
}

DvrRecordingService::~DvrRecordingService() {
    g_realRunning = false;
    MockSubscriptionManager::getInstance().setRecordingJobProvider(nullptr);
}

std::string DvrRecordingService::capabilitiesXml() {
    if (!g_realRunning)   // giá trị mock cũ (1 recording, 1 job), khớp RecordingService mock
        return "<trc:Capabilities DynamicRecordings=\"false\" DynamicTracks=\"false\" "
               "DeleteData=\"false\" Encoding=\"H264\" MaxRate=\"20000\" "
               "MaxTotalRate=\"20000\" MaxRecordings=\"1\" MaxRecordingJobs=\"1\" Options=\"true\"/>";
    // MaxRate/MaxTotalRate (kbps) là giá trị khai báo, chưa đối chiếu giới hạn thật của DVR. Encoding
    // chỉ H264 (giá trị mock đã qua DTT): DVR cũng ghi được H.265 nhưng chưa rõ chuỗi hợp lệ.
    const std::size_t n = g_knownSources.load();   // mỗi Recording tối đa 1 job
    return "<trc:Capabilities DynamicRecordings=\"false\" DynamicTracks=\"false\" "
           "DeleteData=\"false\" Encoding=\"H264\" MaxRate=\"20000\" MaxTotalRate=\"" +
           std::to_string(20000 * n) + "\" MaxRecordings=\"" + std::to_string(n) +
           "\" MaxRecordingJobs=\"" + std::to_string(n) + "\" Options=\"true\"/>";
}

std::string DvrRecordingService::handle(const std::string& req) {
    const std::string op = opName(req);
    const std::string rel = textOf(req, "MessageID");

    if (op == "GetServiceCapabilities")
        return reply(rel, "GetServiceCapabilitiesResponse",
                     "<trc:GetServiceCapabilitiesResponse>" + capabilitiesXml() +
                     "</trc:GetServiceCapabilitiesResponse>");
    if (op == "GetRecordings")                return getRecordings(rel);
    if (op == "GetRecordingConfiguration")    return getRecordingConfiguration(req, rel);
    if (op == "SetRecordingConfiguration")    return setRecordingConfiguration(req, rel);
    if (op == "GetTrackConfiguration")        return getTrackConfiguration(req, rel);
    if (op == "SetTrackConfiguration")        return setTrackConfiguration(req, rel);
    if (op == "GetRecordingOptions")          return getRecordingOptions(req, rel);
    if (op == "CreateRecordingJob")           return createJob(req, rel);
    if (op == "DeleteRecordingJob")           return deleteJob(req, rel);
    if (op == "GetRecordingJobs")             return getJobs(rel);
    if (op == "GetRecordingJobConfiguration") return getJobConfiguration(req, rel);
    if (op == "SetRecordingJobConfiguration") return setJobConfiguration(req, rel);
    if (op == "SetRecordingJobMode")          return setJobMode(req, rel);
    if (op == "GetRecordingJobState")         return getJobState(req, rel);
    return "";   // không nhận diện (gồm các thao tác dynamic) → OnvifServer trả fault mặc định
}

// ── DVR ───────────────────────────────────────────────────────────────

bool DvrRecordingService::loadSources(std::vector<Source>& sources, std::string& fault) const {
    try {
        const std::vector<StreamProfile> profiles = dvr_->getProfiles();
        sources.clear();
        for (const auto& recorder : dvr_->getRecorderSources()) {
            Source source;
            source.recordingToken = "rec_" + recorder.videoSourceId;
            source.videoSourceId = recorder.videoSourceId;
            source.name = recorder.name;
            for (const auto& state : recorder.streams) {
                // Media profile của luồng: cùng sensor ("sourceToken" của profile chính là
                // VideoSourceId của DVR) và cùng loại luồng. "third"/"fourth" của DVR bị dồn vào SUB2
                // nên không phân biệt được — chỉ hỗ trợ main và sub.
                const auto profile = std::find_if(profiles.begin(), profiles.end(),
                    [&](const StreamProfile& p) {
                        const char* type = p.streamType == StreamType::MAIN ? "main"
                                         : p.streamType == StreamType::SUB1 ? "sub" : "";
                        return p.sourceToken == recorder.videoSourceId && state.streamType == type;
                    });
                if (profile == profiles.end()) continue;
                source.streams.push_back(
                    {profile->token, state.streamType, "VIDEO_" + state.streamType, state.isRecording});
                Stream& added = source.streams.back();
                added.width = profile->videoConfig.resolution.width;
                added.height = profile->videoConfig.resolution.height;
                added.framerate = profile->videoConfig.framerate;
                added.bitrate = profile->videoConfig.bitrate;
            }
            if (!source.streams.empty()) sources.push_back(std::move(source));
        }
        g_knownSources = std::max<std::size_t>(sources.size(), 1);
        return true;
    } catch (const std::exception& e) {
        fprintf(stderr, "[DvrRecording] DVR unavailable: %s\n", e.what());
        fault = faultBackend();
        return false;
    }
}

const DvrRecordingService::Source* DvrRecordingService::findSource(const std::vector<Source>& sources,
                                                                   const std::string& token) {
    for (const auto& s : sources) if (s.recordingToken == token) return &s;
    return nullptr;
}

void DvrRecordingService::setManualRecord(const Source& source, const Stream& stream, bool enable) const {
    dvr_->setManualRecord(source.videoSourceId, stream.streamType, enable);
    if (!enable) return;
    for (const auto& recorder : dvr_->getRecorderSources()) {
        if (recorder.videoSourceId != source.videoSourceId) continue;
        for (const auto& state : recorder.streams)
            if (state.streamType == stream.streamType && state.isRecording) return;
    }
    throw RefusedError("DVR did not start recording (stream disabled or camera not streaming)");
}

// ── XML ───────────────────────────────────────────────────────────────

RecordingConfigRecord DvrRecordingService::effectiveConfig(const Source& source) const {
    RecordingConfigRecord config;
    if (store_->getConfig(source.recordingToken, config)) return config;
    const std::string name = source.name.empty() ? "Sensor " + source.videoSourceId : source.name;
    config.sourceId = "urn:alvis:videosource:" + source.videoSourceId;
    config.name = name;
    config.description = "Recording of " + name;
    config.address = serviceAddress_;
    config.content = "Recorded video of " + name;
    config.maxRetention = "PT0S";   // DVR chỉ xóa khi đầy đĩa, không giới hạn theo thời gian
    return config;
}

std::string DvrRecordingService::sourceXml(const RecordingConfigRecord& c) {
    return "<tt:Source>"
             "<tt:SourceId>" + esc(c.sourceId) + "</tt:SourceId>"
             "<tt:Name>" + esc(c.name) + "</tt:Name>"
             "<tt:Location>" + esc(c.location) + "</tt:Location>"
             "<tt:Description>" + esc(c.description) + "</tt:Description>"
             "<tt:Address>" + esc(c.address) + "</tt:Address>"
           "</tt:Source>";
}

std::string DvrRecordingService::contentXml(const RecordingConfigRecord& c) {
    return "<tt:Content>" + esc(c.content) + "</tt:Content>";
}

std::string DvrRecordingService::configXml(const Source& source) const {
    const RecordingConfigRecord c = effectiveConfig(source);
    return sourceXml(c) + contentXml(c) +
           "<tt:MaximumRetentionTime>" + esc(c.maxRetention) + "</tt:MaximumRetentionTime>";
}

bool DvrRecordingService::catalog(std::vector<CatalogRecording>& out) const {
    std::vector<Source> sources;
    std::string ignored;
    if (!loadSources(sources, ignored)) return false;
    out.clear();
    for (const auto& source : sources) {
        const RecordingConfigRecord config = effectiveConfig(source);
        CatalogRecording rec;
        rec.token = source.recordingToken;
        rec.videoSourceId = source.videoSourceId;
        rec.sourceId = config.sourceId;
        rec.sourceXml = sourceXml(config);
        rec.contentXml = contentXml(config);
        for (const auto& stream : source.streams) {
            CatalogTrack track;
            track.token = stream.trackToken;
            track.streamType = stream.streamType;
            if (!store_->getTrackDescription(source.recordingToken, stream.trackToken, track.description))
                track.description = "Video " + stream.streamType + " stream";
            track.width = stream.width;
            track.height = stream.height;
            track.framerate = stream.framerate;
            track.bitrate = stream.bitrate;
            track.isRecording = stream.isRecording;
            rec.tracks.push_back(std::move(track));
        }
        out.push_back(std::move(rec));
    }
    return true;
}

std::string DvrRecordingService::tracksXml(const Source& source) const {
    std::string xml;
    for (const auto& stream : source.streams) {
        std::string description;
        if (!store_->getTrackDescription(source.recordingToken, stream.trackToken, description))
            description = "Video " + stream.streamType + " stream";
        xml += "<tt:Track><tt:TrackToken>" + stream.trackToken + "</tt:TrackToken>"
               "<tt:Configuration><tt:TrackType>Video</tt:TrackType>"
               "<tt:Description>" + esc(description) + "</tt:Description></tt:Configuration></tt:Track>";
    }
    return xml;
}

// ── Recording ─────────────────────────────────────────────────────────

std::string DvrRecordingService::getRecordings(const std::string& rel) {
    std::vector<Source> sources;
    std::string fault;
    if (!loadSources(sources, fault)) return fault;
    std::string items;
    for (const auto& source : sources)
        items += "<trc:RecordingItem><tt:RecordingToken>" + source.recordingToken + "</tt:RecordingToken>"
                 "<tt:Configuration>" + configXml(source) + "</tt:Configuration>"
                 "<tt:Tracks>" + tracksXml(source) + "</tt:Tracks></trc:RecordingItem>";
    return reply(rel, "GetRecordingsResponse", "<trc:GetRecordingsResponse>" + items + "</trc:GetRecordingsResponse>");
}

std::string DvrRecordingService::getRecordingConfiguration(const std::string& req, const std::string& rel) {
    std::vector<Source> sources;
    std::string fault;
    if (!loadSources(sources, fault)) return fault;
    const Source* source = findSource(sources, textOf(req, "RecordingToken"));
    if (!source) return faultNoRecording();
    return reply(rel, "GetRecordingConfigurationResponse",
                 "<trc:GetRecordingConfigurationResponse><trc:RecordingConfiguration>" + configXml(*source) +
                 "</trc:RecordingConfiguration></trc:GetRecordingConfigurationResponse>");
}

std::string DvrRecordingService::setRecordingConfiguration(const std::string& req, const std::string& rel) {
    std::lock_guard<std::mutex> lock(mutateMutex_);
    std::vector<Source> sources;
    std::string fault;
    if (!loadSources(sources, fault)) return fault;
    const Source* source = findSource(sources, textOf(req, "RecordingToken"));
    if (!source) return faultNoRecording();

    const std::string scope = blockOf(req, "RecordingConfiguration");
    if (scope.empty()) return faultBadConfig("Missing RecordingConfiguration");
    RecordingConfigRecord c = effectiveConfig(*source);
    // phần tử vắng mặt → giữ giá trị hiện tại
    auto get = [&](const char* name, const std::string& current) {
        return blockOf(scope, name).empty() ? current : unesc(textOf(scope, name));
    };
    c.sourceId = get("SourceId", c.sourceId);
    c.name = get("Name", c.name);
    c.location = get("Location", c.location);
    c.description = get("Description", c.description);
    c.address = get("Address", c.address);
    c.content = get("Content", c.content);
    c.maxRetention = get("MaximumRetentionTime", c.maxRetention);

    for (const std::string* v : {&c.sourceId, &c.name, &c.location, &c.description, &c.address, &c.content})
        if (v->size() > kMaxTextLength) return faultBadConfig("Configuration value too long");
    // xs:duration ("PT0S", "P30D"); chỉ kiểm dạng vì DVR không áp retention theo thời gian.
    if (c.maxRetention.empty() || c.maxRetention.size() > 64 || c.maxRetention.find('P') == std::string::npos)
        return faultBadConfig("Invalid MaximumRetentionTime");

    store_->setConfig(source->recordingToken, c);
    MockSubscriptionManager::getInstance().fireRecordingConfigChanged(
        "RecordingConfiguration",
        "<tt:SimpleItem Name=\"RecordingToken\" Value=\"" + source->recordingToken + "\"/>",
        "<tt:RecordingConfiguration>" + configXml(*source) + "</tt:RecordingConfiguration>");
    return reply(rel, "SetRecordingConfigurationResponse", "<trc:SetRecordingConfigurationResponse/>");
}

// ── Track ─────────────────────────────────────────────────────────────

std::string DvrRecordingService::getTrackConfiguration(const std::string& req, const std::string& rel) {
    std::vector<Source> sources;
    std::string fault;
    if (!loadSources(sources, fault)) return fault;
    const Source* source = findSource(sources, textOf(req, "RecordingToken"));
    if (!source) return faultNoRecording();
    const Stream* stream = source->byTrack(textOf(req, "TrackToken"));
    if (!stream) return faultNoTrack();

    std::string description;
    if (!store_->getTrackDescription(source->recordingToken, stream->trackToken, description))
        description = "Video " + stream->streamType + " stream";
    return reply(rel, "GetTrackConfigurationResponse",
                 "<trc:GetTrackConfigurationResponse><trc:TrackConfiguration><tt:TrackType>Video</tt:TrackType>"
                 "<tt:Description>" + esc(description) + "</tt:Description>"
                 "</trc:TrackConfiguration></trc:GetTrackConfigurationResponse>");
}

std::string DvrRecordingService::setTrackConfiguration(const std::string& req, const std::string& rel) {
    std::lock_guard<std::mutex> lock(mutateMutex_);
    std::vector<Source> sources;
    std::string fault;
    if (!loadSources(sources, fault)) return fault;
    const Source* source = findSource(sources, textOf(req, "RecordingToken"));
    if (!source) return faultNoRecording();
    const Stream* stream = source->byTrack(textOf(req, "TrackToken"));
    if (!stream) return faultNoTrack();

    const std::string scope = blockOf(req, "TrackConfiguration");
    if (scope.empty()) return faultBadConfig("Missing TrackConfiguration");
    const std::string type = textOf(scope, "TrackType");   // track của DVR luôn là video
    if (!type.empty() && type != "Video") return faultBadConfig("Only Video tracks are supported");

    std::string description;
    store_->getTrackDescription(source->recordingToken, stream->trackToken, description);
    if (!blockOf(scope, "Description").empty()) description = unesc(textOf(scope, "Description"));
    if (description.size() > kMaxTextLength) return faultBadConfig("Configuration value too long");

    store_->setTrackDescription(source->recordingToken, stream->trackToken, description);
    MockSubscriptionManager::getInstance().fireRecordingConfigChanged(
        "TrackConfiguration",
        "<tt:SimpleItem Name=\"RecordingToken\" Value=\"" + source->recordingToken + "\"/>"
        "<tt:SimpleItem Name=\"TrackToken\" Value=\"" + stream->trackToken + "\"/>",
        "<tt:TrackConfiguration><tt:TrackType>Video</tt:TrackType><tt:Description>" + esc(description) +
        "</tt:Description></tt:TrackConfiguration>");
    return reply(rel, "SetTrackConfigurationResponse", "<trc:SetTrackConfigurationResponse/>");
}

// ── Options ───────────────────────────────────────────────────────────

std::string DvrRecordingService::getRecordingOptions(const std::string& req, const std::string& rel) {
    std::vector<Source> sources;
    std::string fault;
    if (!loadSources(sources, fault)) return fault;
    const Source* source = findSource(sources, textOf(req, "RecordingToken"));
    if (!source) return faultNoRecording();

    // Mỗi Recording chỉ 1 job (DVR chỉ ghi tay 1 luồng/sensor): đã có job thì hết slot.
    RecordingJobRecord existing;
    const bool hasJob = store_->findJobByRecording(source->recordingToken, existing);
    std::string compatible;
    for (const auto& stream : source->streams)
        compatible += (compatible.empty() ? "" : " ") + stream.profileToken;
    return reply(rel, "GetRecordingOptionsResponse",
                 "<trc:GetRecordingOptionsResponse><trc:Options><trc:Job Spare=\"" +
                 std::string(hasJob ? "0" : "1") + "\" CompatibleSources=\"" + esc(compatible) + "\"/>"
                 "<trc:Track SpareTotal=\"0\" SpareVideo=\"0\" SpareAudio=\"0\" SpareMetadata=\"0\"/>"
                 "</trc:Options></trc:GetRecordingOptionsResponse>");
}
