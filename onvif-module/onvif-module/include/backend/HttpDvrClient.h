#pragma once

#include "backend/IDvrClient.h"

class HttpDvrClient final : public IDvrClient {
public:
    explicit HttpDvrClient(DvrClientConfig config);

    std::vector<StreamProfile> getProfiles() override;
    StreamUri   getStreamUri(const std::string& profileToken,
                             StreamProtocol protocol) override;
    SnapshotUri getSnapshotUri(const std::string& profileToken) override;

private:
    struct HttpResponse { int status = 0; std::string body; };
    HttpResponse request(const std::string& method, const std::string& path,
                         const std::string& body = "") const;

    DvrClientConfig config_;
};
