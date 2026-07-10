#pragma once
// FaultBuilder — build SOAP 1.2 fault XML với ONVIF subcode chuẩn.
//
// Chuẩn kiến trúc: xem docs/10-refactor-plan-multi-profile.md PHẦN II (QĐ-3).
// CẤM string-concat fault thủ công rải rác — dùng các helper ở đây.

#include <string>

class FaultBuilder {
public:
    // env:Sender fault với subcode 2 tầng.
    // VD: sender("ter:InvalidArgVal", "ter:NoProfile", "Profile not found")
    // Nếu subcode2 rỗng → chỉ 1 tầng subcode.
    static std::string sender(const std::string& subcode1,
                              const std::string& subcode2,
                              const std::string& reason);

    // env:Receiver fault (server-side error).
    static std::string receiver(const std::string& subcode1,
                                const std::string& reason);

    // ── Shortcut các fault hay dùng ──────────────────────────────
    static std::string notAuthorized();
    static std::string actionNotSupported();
    static std::string invalidArgVal(const std::string& reason);
    static std::string noProfile();
};
