#include "auth/WsSecurityHandler.h"
#include "soapH.h"
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <cstring>
#include <iostream>

// Helper decode Base64
static std::string base64Decode(const std::string& in) {
    if (in.empty()) return "";
    BIO *bio, *b64;
    char* buffer = (char*)malloc(in.size());
    if (!buffer) return "";
    memset(buffer, 0, in.size());
    bio = BIO_new_mem_buf(in.data(), in.size());
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    int decoded_len = BIO_read(bio, buffer, in.size());
    BIO_free_all(bio);
    std::string out(buffer, decoded_len);
    free(buffer);
    return out;
}

// Helper encode Base64
static std::string base64Encode(const unsigned char* buffer, size_t length) {
    BIO *bio, *b64;
    BUF_MEM *bufferPtr;
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, buffer, length);
    (void)BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);
    std::string out(bufferPtr->data, bufferPtr->length);
    BIO_free_all(bio);
    return out;
}

bool WsSecurityHandler::validate(struct soap* soap) const {
    if (!soap || !soap->header) {
        std::cerr << "[WsSecurity] Auth failed: No SOAP header" << std::endl;
        return false;
    }

    // Lấy wsse:Security
    auto security = soap->header->wsse__Security;
    if (!security || !security->UsernameToken) {
        std::cerr << "[WsSecurity] Auth failed: No UsernameToken in Security header" << std::endl;
        return false;
    }

    auto token = security->UsernameToken;
    if (!token->Username) {
        std::cerr << "[WsSecurity] Auth failed: Username is missing" << std::endl;
        return false;
    }

    std::string clientUser = token->Username;
    if (clientUser != username_) {
        std::cerr << "[WsSecurity] Auth failed: Username mismatch. Expected: " 
                  << username_ << ", Got: " << clientUser << std::endl;
        return false;
    }

    if (!token->Password) {
        std::cerr << "[WsSecurity] Auth failed: Password node is missing" << std::endl;
        return false;
    }

    std::string passwordType = token->Password->Type ? *(token->Password->Type) : "";
    std::string clientPassword = token->Password->__item ? *(token->Password->__item) : "";

    // 1. Plaintext Password
    if (passwordType == "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#PasswordText" || passwordType.empty()) {
        if (clientPassword == password_) {
            return true;
        }
        std::cerr << "[WsSecurity] Plaintext password validation failed" << std::endl;
        return false;
    }

    // 2. PasswordDigest
    if (passwordType == "http://docs.oasis-open.org/wss/2004/01/oasis-200401-wss-username-token-profile-1.0#PasswordDigest") {
        if (!token->Nonce || !token->wsu__Created) {
            std::cerr << "[WsSecurity] Digest auth failed: Missing Nonce or Created timestamp" << std::endl;
            return false;
        }

        std::string rawNonce = base64Decode(token->Nonce->__item);
        std::string createdTime = *(token->wsu__Created);

        // Công thức: PasswordDigest = Base64 ( SHA-1 ( Nonce + Created + Password ) )
        SHA_CTX sha;
        unsigned char hash[SHA_DIGEST_LENGTH];
        SHA1_Init(&sha);
        SHA1_Update(&sha, rawNonce.data(), rawNonce.size());
        SHA1_Update(&sha, createdTime.data(), createdTime.size());
        SHA1_Update(&sha, password_.data(), password_.size());
        SHA1_Final(hash, &sha);

        std::string calculatedDigest = base64Encode(hash, SHA_DIGEST_LENGTH);
        if (calculatedDigest == clientPassword) {
            return true;
        }

        // Fallback: Một số client gửi Nonce dạng chuỗi thô thay vì mã hóa base64 trước đó
        std::string rawNonceStr = token->Nonce->__item;
        SHA1_Init(&sha);
        SHA1_Update(&sha, rawNonceStr.data(), rawNonceStr.size());
        SHA1_Update(&sha, createdTime.data(), createdTime.size());
        SHA1_Update(&sha, password_.data(), password_.size());
        SHA1_Final(hash, &sha);
        
        calculatedDigest = base64Encode(hash, SHA_DIGEST_LENGTH);
        if (calculatedDigest == clientPassword) {
            return true;
        }

        std::cerr << "[WsSecurity] Digest verification failed" << std::endl;
        return false;
    }

    std::cerr << "[WsSecurity] Unsupported password type: " << passwordType << std::endl;
    return false;
}
