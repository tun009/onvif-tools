#pragma once
#include <string>

struct soap;

class WsSecurityHandler {
public:
    WsSecurityHandler(const std::string& username,
                      const std::string& password)
        : username_(username), password_(password) {}

    // Xác thực WS-Security từ gSOAP soap context
    bool validate(struct soap* soap) const;

    const std::string& username() const { return username_; }
    const std::string& password() const { return password_; }

private:
    std::string username_;
    std::string password_;
};
