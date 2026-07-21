#pragma once
// MediaLegacyHandler — implement Media Service ver10 (Profile S / G / Q).
// Trả XML thủ công vì Profile T dùng Media2 (ver20); Media1 chỉ dùng để claim
// Profile S. Tất cả ops Media1 map ra state Media2 (cùng 3 profiles fixed).

#include <string>

class MediaLegacyHandler {
public:
    // Nhận diện request Media ver10 trong body; trả response XML nếu match,
    // "" nếu không phải op ver10 (fallback cho Media2 dispatcher).
    static std::string dispatch(const std::string& rawRequest);
    // Đặt deviceIp + httpPort để build URL trong response (GetStreamUri...).
    static void setEndpoint(const std::string& ip, int port);

private:
    // Discovery + info
    static std::string handleGetServiceCapabilities();
    static std::string handleGetVideoSources();
    static std::string handleGetAudioSources();

    // Profile CRUD
    static std::string handleGetProfiles();
    static std::string handleGetProfile(const std::string& req);
    static std::string handleCreateProfile(const std::string& req);
    static std::string handleDeleteProfile(const std::string& req);

    // Video Source Config
    static std::string handleGetVideoSourceConfigurations();
    static std::string handleGetVideoSourceConfiguration(const std::string& req);
    static std::string handleGetVideoSourceConfigurationOptions();
    static std::string handleGetCompatibleVideoSourceConfigurations();
    static std::string handleAddVideoSourceConfiguration(const std::string& req);
    static std::string handleRemoveVideoSourceConfiguration(const std::string& req);
    static std::string handleSetVideoSourceConfiguration(const std::string& req);

    // Video Encoder Config
    static std::string handleGetVideoEncoderConfigurations();
    static std::string handleGetVideoEncoderConfiguration(const std::string& req);
    static std::string handleGetVideoEncoderConfigurationOptions(const std::string& req);
    static std::string handleGetCompatibleVideoEncoderConfigurations();
    static std::string handleAddVideoEncoderConfiguration(const std::string& req);
    static std::string handleRemoveVideoEncoderConfiguration(const std::string& req);
    static std::string handleSetVideoEncoderConfiguration(const std::string& req);
    static std::string handleGetGuaranteedNumberOfVideoEncoderInstances(const std::string& req);

    // Streaming
    static std::string handleGetStreamUri(const std::string& req);
    static std::string handleGetSnapshotUri(const std::string& req);
    static std::string handleSetSynchronizationPoint(const std::string& req);

    // Metadata (Media1 optional but tool RTSS-4-1-3 / MEDIA-5-1-3 gọi)
    static std::string handleGetMetadataConfigurations();

    // Helpers
    static std::string extractMessageId(const std::string& xml);
    static std::string extractInnerTag(const std::string& xml, const std::string& localName);
    static std::string wrap(const std::string& action,
                            const std::string& relatesTo,
                            const std::string& bodyXml);
    // wrapperElem = "Profiles" (list, plural) hoặc "Profile" (single).
    // fixed=true cho profile default backend, false cho dyn CreateProfile.
    // includeVSC/includeVEC: kèm configuration blocks hay không.
    static std::string profileXml(const char* wrapperElem,
                                  const char* token, const char* name,
                                  bool fixed, bool includeVSC, bool includeVEC);
};
