#pragma once
// WsSecurityHandler.h
// Validates ONVIF WS-Security UsernameToken (PasswordDigest or PasswordText)
// Uses OpenSSL directly — does NOT depend on wsseapi.cpp.
//
// PasswordDigest spec (ONVIF / WS-Security):
//   digest = Base64( SHA1( Base64Decode(nonce) + created + password ) )

#include <string>
#include <mutex>
#include <map>
#include <ctime>
#include <cmath>

struct soap;  // forward declaration — avoids including soapH.h here

class WsSecurityHandler {
public:
    WsSecurityHandler(const std::string& username,
                      const std::string& password)
        : username_(username), password_(password) {}

    // Call after soap_begin_serve(). Returns true if auth passes.
    bool validate(struct soap* ctx) const;

    // Simple plaintext check (for fallback / testing)
    bool validatePlaintext(const std::string& user,
                           const std::string& pass) const {
        return user == username_ && pass == password_;
    }

    const std::string& username() const { return username_; }
    const std::string& password() const { return password_; }

private:
    // Compute and compare PasswordDigest
    static bool verifyDigest(const std::string& nonceB64,
                             const std::string& created,
                             const std::string& digestB64,
                             const std::string& password);

    // Anti-replay: verify nonce+timestamp not reused within 5-minute window
    bool checkNonce(const std::string& nonce,
                    const std::string& created) const;

    std::string username_;
    std::string password_;

    mutable std::mutex              nonceMutex_;
    mutable std::map<std::string, time_t> usedNonces_;
    static constexpr int            NONCE_WINDOW_SEC = 300;
};
