#pragma once
#include "backend/IMgmtClient.h"
#include <memory>
#include <string>
#include <map>
#include <utility>

class DigestAuthHandler {
public:
    DigestAuthHandler(const std::string& username,
                      const std::string& password,
                      const std::string& realm = "onvif")
        : username_(username), password_(password), realm_(realm) {}
    explicit DigestAuthHandler(std::shared_ptr<IMgmtClient> mgmtClient,
                               const std::string& realm = "onvif")
        : realm_(realm), mgmtClient_(std::move(mgmtClient)) {}

    // Xác thực request dựa trên raw HTTP headers từ gSOAP
    bool validate(const std::string& rawHeaders, const std::string& method) const;

    // Tạo chuỗi WWW-Authenticate challenge header
    std::string generateChallenge() const;

private:
    // Helper parse các tham số của Digest header (ví dụ: username="admin", nonce="...")
    static std::map<std::string, std::string> parseDigestParams(const std::string& authHeader);

    // Tính toán MD5 hash của một chuỗi
    static std::string calculateMD5(const std::string& input);

    std::string username_;
    std::string password_;
    std::string realm_;
    std::shared_ptr<IMgmtClient> mgmtClient_;
};
