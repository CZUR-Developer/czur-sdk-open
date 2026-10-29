// Copyright (c) 2026 CZUR Tech. All rights reserved.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sdk_auth_types.h"

namespace editor {
namespace sdk {
namespace private_impl {

struct SessionIssueResult {
    int code;
    std::string message;
    std::string session_token;
    int expires_in;
    AuthContext auth_context;

    SessionIssueResult() : code(0), expires_in(0) {}
};

struct SessionParseResult {
    int code;
    std::string message;
    AuthContext auth_context;
    std::string key_id;

    SessionParseResult() : code(0) {}
};

class PrivateSdkSessionService {
public:
    PrivateSdkSessionService();

    SessionIssueResult IssueSession(const AuthContext& auth_context, std::int64_t now_ts) const;
    void IssueSession(const AuthContext& auth_context, std::int64_t now_ts, SessionIssueResult* result) const;
    SessionParseResult ValidateSession(const std::string& session_token, std::int64_t now_ts) const;

private:
    std::vector<unsigned char> enc_key_;
    std::vector<unsigned char> sig_key_;
};

} // namespace private_impl
} // namespace sdk
} // namespace editor
