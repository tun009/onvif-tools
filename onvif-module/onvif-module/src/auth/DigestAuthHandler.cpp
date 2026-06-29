#include "auth/DigestAuthHandler.h"
#include <openssl/md5.h>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <algorithm>

// Helper to trim whitespaces and quotes
static std::string trimQuote(std::string str) {
    // Trim spaces
    str.erase(0, str.find_first_not_of(" \t\r\n"));
    str.erase(str.find_last_not_of(" \t\r\n") + 1);
    // Trim quotes
    if (str.size() >= 2 && str.front() == '"' && str.back() == '"') {
        str = str.substr(1, str.size() - 2);
    }
    return str;
}

std::string DigestAuthHandler::calculateMD5(const std::string& input) {
    unsigned char digest[MD5_DIGEST_LENGTH];
    MD5((const unsigned char*)input.c_str(), input.size(), digest);
    
    std::ostringstream oss;
    for (int i = 0; i < MD5_DIGEST_LENGTH; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)digest[i];
    }
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
    // Tạo một nonce ngẫu nhiên đơn giản
    static std::random_device rd;
    static std::mt19937 generator(rd());
    std::uniform_int_distribution<uint64_t> distribution;
    uint64_t randomVal = distribution(generator);
    
    std::string nonce = calculateMD5(std::to_string(randomVal) + std::to_string(time(nullptr)));
    std::string opaque = calculateMD5(realm_ + "opaque_salt");
    
    std::ostringstream challenge;
    challenge << "WWW-Authenticate: Digest "
              << "realm=\"" << realm_ << "\", "
              << "qop=\"auth\", "
              << "nonce=\"" << nonce << "\", "
              << "opaque=\"" << opaque << "\"";
              
    return challenge.str();
}

bool DigestAuthHandler::validate(const std::string& rawHeaders, const std::string& method) const {
    // 1. Tìm header Authorization
    std::stringstream ss(rawHeaders);
    std::string line;
    std::string authHeader = "";
    
    while (std::getline(ss, line)) {
        // Cắt bỏ kí tự xuống dòng ở cuối line
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("Authorization:", 0) == 0) {
            authHeader = line;
            break;
        }
    }
    
    if (authHeader.empty()) {
        std::cerr << "[DigestAuth] Missing Authorization header" << std::endl;
        return false;
    }
    
    // 2. Parse các tham số trong Digest Auth header
    auto params = parseDigestParams(authHeader);
    if (params.find("username") == params.end() || params.find("nonce") == params.end() ||
        params.find("uri") == params.end() || params.find("response") == params.end()) {
        std::cerr << "[DigestAuth] Invalid Digest parameters" << std::endl;
        return false;
    }
    
    std::string clientUser = params["username"];
    std::string clientUri  = params["uri"];
    std::string clientResp = params["response"];
    std::string nonce      = params["nonce"];
    
    if (clientUser != username_) {
        std::cerr << "[DigestAuth] Username mismatch: expected " << username_ << " but got " << clientUser << std::endl;
        return false;
    }
    
    // 3. Tính toán và so khớp hash MD5
    // HA1 = MD5(username:realm:password)
    std::string ha1 = calculateMD5(username_ + ":" + realm_ + ":" + password_);
    
    // HA2 = MD5(method:uri)
    std::string ha2 = calculateMD5(method + ":" + clientUri);
    
    std::string calculatedResp;
    if (params.find("qop") != params.end() && params["qop"] == "auth") {
        if (params.find("nc") == params.end() || params.find("cnonce") == params.end()) {
            std::cerr << "[DigestAuth] Missing nc or cnonce for qop=auth" << std::endl;
            return false;
        }
        std::string nc = params["nc"];
        std::string cnonce = params["cnonce"];
        std::string qop = params["qop"];
        
        calculatedResp = calculateMD5(ha1 + ":" + nonce + ":" + nc + ":" + cnonce + ":" + qop + ":" + ha2);
    } else {
        calculatedResp = calculateMD5(ha1 + ":" + nonce + ":" + ha2);
    }
    
    if (calculatedResp == clientResp) {
        return true;
    }
    
    std::cerr << "[DigestAuth] Hash mismatch! Client: " << clientResp << ", Expected: " << calculatedResp << std::endl;
    return false;
}
