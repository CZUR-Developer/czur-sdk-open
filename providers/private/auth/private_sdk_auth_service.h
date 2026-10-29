// Copyright (c) 2026 CZUR Tech. All rights reserved.

#pragma once

#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "private_sdk_key_codec.h"
#include "private_sdk_session_service.h"

namespace editor {
namespace sdk {
namespace private_impl {

struct AuthCodeGenerateResult {
    int code = 0;
    std::string message = "ok";
    std::string auth_code;
};

class PrivateSdkAuthService {
public:
    AuthValidateResult ValidateApiKey(const AuthValidateRequest& request) const;
    void ValidateApiKey(const AuthValidateRequest& request, AuthValidateResult* result) const;
    AuthRefreshResult RefreshSession(const AuthValidateRequest& request) const;
    void RefreshSession(const AuthValidateRequest& request, AuthRefreshResult* result) const;
    AuthRefreshResult RefreshSession(const AuthRefreshRequest& request) const;
    void RefreshSession(const AuthRefreshRequest& request, AuthRefreshResult* result) const;
    SessionValidateResult ValidateSession(const SessionValidateRequest& request) const;
    AuthContextResult GetAuthContext(const AuthLookupRequest& request) const;
    OfflineActivateResult ActivateOffline(const OfflineActivateRequest& request) const;
    QuotaConsumeResult ConsumeQuota(const QuotaConsumeRequest& request) const;
    AuthCodeGenerateResult GenerateAuthCode(const std::string& machine_code,
                                            const std::string& expires_date,
                                            int auth_type) const;
    bool GenerateApiKey(const ApiKeySpec& spec, std::string* api_key, std::string* err = NULL) const;
    ParsedApiKey ParseApiKey(const std::string& api_key, std::int64_t now_ts) const;
    void ParseApiKey(const std::string& api_key, std::int64_t now_ts, ParsedApiKey* parsed) const;

private:
    AuthContext FinalizeAuthContext(const AuthContext& auth_context) const;
    std::vector<std::string> BuildCapabilities(SdkAccountType account_type) const;

    PrivateSdkKeyCodec key_codec_;
    PrivateSdkSessionService session_service_;
    mutable std::mutex state_mu_;
    mutable std::map<std::string, AuthContext> online_auth_contexts_;
};

} // namespace private_impl
} // namespace sdk
} // namespace editor
