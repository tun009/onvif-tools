// WsSecurityHandler.cpp
// Validates ONVIF WS-Security UsernameToken without wsseapi.cpp.
// Parses the wsse:Security SOAP header from the gSOAP-generated struct
// and verifies PasswordDigest using OpenSSL SHA1 + Base64.

#include "auth/WsSecurityHandler.h"

// gSOAP generated headers — must be generated first (make gen)
#include "soapH.h"

#include <openssl/sha.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <cstring>
#include <ctime>
#include <vector>
#include <algorithm>
#include <stdexcept>

// ── Base64 helpers ────────────────────────────────────────────────────────

static std::vector<uint8_t> b64Decode(const std::string& in) {
    BIO* b64  = BIO_new(BIO_f_base64());
    BIO* bmem = BIO_new_mem_buf(in.data(), static_cast<int>(in.size()));
    bmem = BIO_push(b64, bmem);
    BIO_set_flags(bmem, BIO_FLAGS_BASE64_NO_NL);

    std::vector<uint8_t> out(in.size());
    int len = BIO_read(bmem, out.data(), static_cast<int>(out.size()));
    BIO_free_all(bmem);

    if (len <= 0) return {};
    out.resize(static_cast<size_t>(len));
    return out;
}

static std::string b64Encode(const uint8_t* data, size_t len) {
    BIO* b64  = BIO_new(BIO_f_base64());
    BIO* bout = BIO_new(BIO_s_mem());
    bout = BIO_push(b64, bout);
    BIO_set_flags(bout, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bout, data, static_cast<int>(len));
    BIO_flush(bout);

    BUF_MEM* bptr;
    BIO_get_mem_ptr(bout, &bptr);
    std::string result(bptr->data, bptr->length);
    BIO_free_all(bout);
    return result;
}

// ── PasswordDigest verification ───────────────────────────────────────────

bool WsSecurityHandler::verifyDigest(const std::string& nonceB64,
                                      const std::string& created,
                                      const std::string& digestB64,
                                      const std::string& password) {
    auto nonce = b64Decode(nonceB64);
    if (nonce.empty()) return false;

    // Input = nonce_bytes || created_string || password_string
    std::vector<uint8_t> input;
    input.insert(input.end(), nonce.begin(), nonce.end());
    input.insert(input.end(), created.begin(), created.end());
    input.insert(input.end(), password.begin(), password.end());

    uint8_t sha1[SHA_DIGEST_LENGTH];
    SHA1(input.data(), input.size(), sha1);

    std::string computed = b64Encode(sha1, SHA_DIGEST_LENGTH);
    return computed == digestB64;
}

// ── Anti-replay nonce check ───────────────────────────────────────────────

bool WsSecurityHandler::checkNonce(const std::string& nonce,
                                    const std::string& created) const {
    std::lock_guard<std::mutex> lock(nonceMutex_);

    // Parse ISO8601 timestamp
    struct tm tm{};
    strptime(created.c_str(), "%Y-%m-%dT%H:%M:%SZ", &tm);
    time_t createdTime = timegm(&tm);
    time_t now         = time(nullptr);

    if (std::abs(static_cast<long>(now - createdTime)) > NONCE_WINDOW_SEC) {
        return false;  // timestamp out of ±5min window
    }

    if (usedNonces_.count(nonce)) {
        return false;  // replay detected
    }

    usedNonces_[nonce] = now;

    // Evict expired nonces
    for (auto it = usedNonces_.begin(); it != usedNonces_.end(); ) {
        if (now - it->second > NONCE_WINDOW_SEC)
            it = usedNonces_.erase(it);
        else
            ++it;
    }

    return true;
}

// ── Main validate ─────────────────────────────────────────────────────────

bool WsSecurityHandler::validate(struct soap* ctx) const {
    if (!ctx || !ctx->header) return false;

    // gSOAP places parsed WS-Security header in ctx->header->wsse__Security
    // The struct name depends on what soapcpp2 generated from the WSDL.
    // Try the standard generated field name:
    auto* security = ctx->header->wsse__Security;
    if (!security) return false;

    // UsernameToken
    if (!security->UsernameToken || !security->UsernameToken->Username)
        return false;

    const std::string& user = security->UsernameToken->Username->__item;
    if (user != username_) return false;

    auto* pwdField = security->UsernameToken->Password;
    if (!pwdField) return false;

    std::string pwType = pwdField->Type ? pwdField->Type : "";
    std::string pwValue = pwdField->__item ? pwdField->__item : "";

    // ── PasswordDigest ────────────────────────────────────────────
    if (pwType.find("PasswordDigest") != std::string::npos) {
        if (!security->UsernameToken->Nonce ||
            !security->UsernameToken->Created) return false;

        const std::string nonce   =
            security->UsernameToken->Nonce->__item
            ? security->UsernameToken->Nonce->__item : "";
        const std::string created =
            security->UsernameToken->Created->__item
            ? security->UsernameToken->Created->__item : "";

        if (!checkNonce(nonce, created)) return false;
        return verifyDigest(nonce, created, pwValue, password_);
    }

    // ── PasswordText (fallback) ───────────────────────────────────
    if (pwType.find("PasswordText") != std::string::npos || pwType.empty()) {
        return pwValue == password_;
    }

    return false;
}
