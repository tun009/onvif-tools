#include "auth/DigestAuthHandler.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace {
using Clock = std::chrono::steady_clock;
constexpr auto kNonceTtl = std::chrono::minutes(5);
constexpr std::size_t kMaxActiveNonces = 1024;

struct NonceRecord {
    Clock::time_point expiresAt;
    std::unordered_map<std::string, std::uint32_t> highestNonceCount;
};

std::mutex g_nonceMutex;
std::unordered_map<std::string, NonceRecord> g_nonces;

bool isHex(const std::string& value, std::size_t requiredSize) {
    return value.size() == requiredSize &&
           std::all_of(value.begin(), value.end(), [](unsigned char ch) {
               return std::isxdigit(ch) != 0;
           });
}

bool parseNonceCount(const std::string& value, std::uint32_t& output) {
    if (!isHex(value, 8)) return false;
    try {
        output = static_cast<std::uint32_t>(std::stoul(value, nullptr, 16));
        return output != 0;
    } catch (...) {
        return false;
    }
}

void removeExpiredNoncesLocked(Clock::time_point now) {
    for (auto item = g_nonces.begin(); item != g_nonces.end();) {
        if (item->second.expiresAt <= now)
            item = g_nonces.erase(item);
        else
            ++item;
    }
}

bool nonceCanBeUsed(const std::string& nonce, const std::string& useKey,
                    std::uint32_t nonceCount) {
    std::lock_guard<std::mutex> lock(g_nonceMutex);
    removeExpiredNoncesLocked(Clock::now());
    const auto nonceItem = g_nonces.find(nonce);
    if (nonceItem == g_nonces.end()) return false;
    const auto countItem = nonceItem->second.highestNonceCount.find(useKey);
    return countItem == nonceItem->second.highestNonceCount.end() ||
           nonceCount > countItem->second;
}

bool commitNonceUse(const std::string& nonce, const std::string& useKey,
                    std::uint32_t nonceCount) {
    std::lock_guard<std::mutex> lock(g_nonceMutex);
    removeExpiredNoncesLocked(Clock::now());
    const auto nonceItem = g_nonces.find(nonce);
    if (nonceItem == g_nonces.end()) return false;
    auto& highest = nonceItem->second.highestNonceCount[useKey];
    if (nonceCount <= highest) return false;
    highest = nonceCount;
    return true;
}

std::string requestTarget(const std::string& rawHeaders,
                          const std::string& expectedMethod) {
    const auto end = rawHeaders.find("\r\n");
    std::istringstream stream(rawHeaders.substr(0, end));
    std::string method;
    std::string target;
    std::string version;
    stream >> method >> target >> version;
    if (method != expectedMethod || target.empty() || version.rfind("HTTP/", 0) != 0)
        return {};
    return target;
}
}

// Helper to trim whitespaces and quotes
static std::string trimQuote(std::string str) {
    const auto first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = str.find_last_not_of(" \t\r\n");
    str = str.substr(first, last - first + 1);
    // Trim quotes
    if (str.size() >= 2 && str.front() == '"' && str.back() == '"') {
        str = str.substr(1, str.size() - 2);
    }
    return str;
}

std::string DigestAuthHandler::calculateMD5(const std::string& input) {
    std::array<unsigned char, 16> digest{};
    unsigned int digestSize = 0;
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (!context) return {};
    const bool ok = EVP_DigestInit_ex(context, EVP_md5(), nullptr) == 1 &&
                    EVP_DigestUpdate(context, input.data(), input.size()) == 1 &&
                    EVP_DigestFinal_ex(context, digest.data(), &digestSize) == 1 &&
                    digestSize == digest.size();
    EVP_MD_CTX_free(context);
    if (!ok) return {};

    std::ostringstream oss;
    for (unsigned char value : digest)
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(value);
    return oss.str();
}

std::map<std::string, std::string> DigestAuthHandler::parseDigestParams(const std::string& authHeader) {
    std::map<std::string, std::string> params;
    
    // Bỏ qua prefix "Digest "
    std::string prefix = "Digest ";
    size_t startPos = authHeader.find(prefix);
    if (startPos == std::string::npos) return params;
    
    std::string listStr = authHeader.substr(startPos + prefix.size());
    std::stringstream ss(listStr);
    std::string item;
    
    while (std::getline(ss, item, ',')) {
        size_t eq = item.find('=');
        if (eq != std::string::npos) {
            std::string key = trimQuote(item.substr(0, eq));
            std::string val = trimQuote(item.substr(eq + 1));
            params[key] = val;
        }
    }
    return params;
}

std::string DigestAuthHandler::generateChallenge() const {
    std::array<unsigned char, 16> randomBytes{};
    std::string nonce;
    if (RAND_bytes(randomBytes.data(), static_cast<int>(randomBytes.size())) == 1) {
        std::ostringstream output;
        for (unsigned char value : randomBytes)
            output << std::hex << std::setw(2) << std::setfill('0')
                   << static_cast<int>(value);
        nonce = output.str();
    } else {
        nonce = calculateMD5(std::to_string(Clock::now().time_since_epoch().count()));
    }

    {
        std::lock_guard<std::mutex> lock(g_nonceMutex);
        removeExpiredNoncesLocked(Clock::now());
        if (g_nonces.size() >= kMaxActiveNonces) g_nonces.erase(g_nonces.begin());
        g_nonces[nonce] = NonceRecord{Clock::now() + kNonceTtl, {}};
    }

    const std::string opaque = calculateMD5(realm_ + "opaque_salt");
    std::ostringstream challenge;
    challenge << "WWW-Authenticate: Digest "
              << "realm=\"" << realm_ << "\", "
              << "qop=\"auth\", "
              << "algorithm=MD5, "
              << "nonce=\"" << nonce << "\", "
              << "opaque=\"" << opaque << "\"";
    return challenge.str();
}

bool DigestAuthHandler::validate(const std::string& rawHeaders, const std::string& method) const {
    std::stringstream ss(rawHeaders);
    std::string line;
    std::string authHeader;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("Authorization:", 0) == 0) {
            authHeader = line;
            break;
        }
    }
    if (authHeader.empty()) return false;

    const auto params = parseDigestParams(authHeader);
    const auto required = {"username", "realm", "nonce", "uri", "response",
                           "qop", "nc", "cnonce"};
    for (const char* name : required)
        if (params.find(name) == params.end()) return false;

    const std::string algorithm = params.count("algorithm") ? params.at("algorithm") : "MD5";
    const std::string actualTarget = requestTarget(rawHeaders, method);
    if (params.at("realm") != realm_ || params.at("qop") != "auth" ||
        algorithm != "MD5" || !isHex(params.at("response"), 32) ||
        actualTarget.empty() || params.at("uri") != actualTarget)
        return false;

    std::uint32_t nonceCount = 0;
    if (!parseNonceCount(params.at("nc"), nonceCount)) return false;
    const std::string useKey = params.at("username") + ':' + params.at("cnonce");
    if (!nonceCanBeUsed(params.at("nonce"), useKey, nonceCount)) {
        std::cerr << "[DigestAuth] Unknown, expired or replayed nonce" << std::endl;
        return false;
    }

    bool authenticated = false;
    if (mgmtClient_) {
        try {
            authenticated = mgmtClient_->verifyHttpDigest(HttpDigestCredential{
                params.at("username"), params.at("realm"), method,
                params.at("uri"), params.at("nonce"), params.at("qop"),
                params.at("nc"), params.at("cnonce"), algorithm,
                params.at("response")}).authenticated;
        } catch (const std::exception& error) {
            std::cerr << "[DigestAuth] MGMT authentication unavailable: "
                      << error.what() << std::endl;
            return false;
        }
    } else {
        if (params.at("username") != username_) return false;
        const std::string ha1 = calculateMD5(username_ + ':' + realm_ + ':' + password_);
        const std::string ha2 = calculateMD5(method + ':' + params.at("uri"));
        const std::string expected = calculateMD5(
            ha1 + ':' + params.at("nonce") + ':' + params.at("nc") + ':' +
            params.at("cnonce") + ':' + params.at("qop") + ':' + ha2);
        std::string supplied = params.at("response");
        std::transform(supplied.begin(), supplied.end(), supplied.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        authenticated = expected == supplied;
    }

    return authenticated && commitNonceUse(params.at("nonce"), useKey, nonceCount);
}
