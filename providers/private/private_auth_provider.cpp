// Copyright (c) 2026 CZUR Tech. All rights reserved.

#include "private_auth_provider.h"

#include "auth/private_sdk_support.h"

#include "support/private_sdk_logger.h"


namespace {

std::string ShortDigestForLog(const std::string& value) {
    if (value.empty()) {
        return "empty";
    }
    const std::string digest = editor::sdk::private_impl::Sha256HexString(value);
    return digest.size() > 12 ? digest.substr(0, 12) : digest;
}

} // namespace

namespace editor {
namespace sdk {
namespace private_impl {

std::string PrivateAuthProvider::ProviderName() const {
    return "czur-sdk-auth-provider";
}

AuthValidateResult PrivateAuthProvider::ValidateToken(const AuthValidateRequest& request) {
    SDK_PRIVATE_LOG_INFO("auth.validate_token begin now_ts={} token_present={} token_digest={}",
                         request.now_ts,
                         !request.token.empty(),
                         ShortDigestForLog(request.token));
    AuthValidateResult result;
    auth_service_.ValidateApiKey(request, &result);
    SDK_PRIVATE_LOG_INFO("auth.validate_token end code={} message={} valid={} account_type={} entitlement={}",
                         result.code,
                         result.message,
                         result.auth_context.is_valid,
                         result.auth_context.account_type_code,
                         result.auth_context.entitlement_state);
    return result;
}

AuthRefreshResult PrivateAuthProvider::CreateSession(const AuthValidateRequest& request) {
    SDK_PRIVATE_LOG_INFO("auth.create_session begin now_ts={} token_present={} token_digest={}",
                         request.now_ts,
                         !request.token.empty(),
                         ShortDigestForLog(request.token));
    AuthRefreshResult result;
    auth_service_.RefreshSession(request, &result);
    SDK_PRIVATE_LOG_INFO("auth.create_session end code={} message={} expires_in={} valid={} entitlement={}",
                         result.code,
                         result.message,
                         result.expires_in,
                         result.auth_context.is_valid,
                         result.auth_context.entitlement_state);
    return result;
}

AuthRefreshResult PrivateAuthProvider::RefreshSession(const AuthRefreshRequest& request) {
    SDK_PRIVATE_LOG_INFO("auth.refresh_session begin now_ts={} session_present={}",
                         request.now_ts,
                         !request.session_token.empty());
    AuthRefreshResult result;
    auth_service_.RefreshSession(request, &result);
    SDK_PRIVATE_LOG_INFO("auth.refresh_session end code={} expires_in={} valid={} entitlement={}",
                         result.code,
                         result.expires_in,
                         result.auth_context.is_valid,
                         result.auth_context.entitlement_state);
    return result;
}

SessionValidateResult PrivateAuthProvider::ValidateSession(const SessionValidateRequest& request) {
    SDK_PRIVATE_LOG_INFO("auth.validate_session begin now_ts={} session_present={}",
                         request.now_ts,
                         !request.session_token.empty());
    SessionValidateResult result = auth_service_.ValidateSession(request);
    SDK_PRIVATE_LOG_INFO("auth.validate_session end code={} valid={} entitlement={}",
                         result.code,
                         result.auth_context.is_valid,
                         result.auth_context.entitlement_state);
    return result;
}

AuthContextResult PrivateAuthProvider::GetAuthContext(const AuthLookupRequest& request) {
    SDK_PRIVATE_LOG_DEBUG("auth.context begin now_ts={} session_present={}",
                          request.now_ts,
                          !request.session_token.empty());
    AuthContextResult result = auth_service_.GetAuthContext(request);
    SDK_PRIVATE_LOG_DEBUG("auth.context end code={} via_session={} valid={}",
                          result.code,
                          result.via_session,
                          result.auth_context.is_valid);
    return result;
}

OfflineActivateResult PrivateAuthProvider::ActivateOffline(const OfflineActivateRequest& request) {
    SDK_PRIVATE_LOG_INFO("auth.activate_offline begin now_ts={} token_present={} session_present={} auth_code_present={}",
                         request.now_ts,
                         !request.token.empty(),
                         !request.session_token.empty(),
                         !request.auth_code.empty());
    OfflineActivateResult result = auth_service_.ActivateOffline(request);
    SDK_PRIVATE_LOG_INFO("auth.activate_offline end code={} activated={} expires_in={} entitlement={}",
                         result.code,
                         result.activated,
                         result.expires_in,
                         result.auth_context.entitlement_state);
    return result;
}

QuotaConsumeResult PrivateAuthProvider::ConsumeQuota(const QuotaConsumeRequest& request) {
    SDK_PRIVATE_LOG_INFO("auth.consume_quota begin capability={} units={} request_id_present={} token_present={} session_present={}",
                         request.capability,
                         request.units,
                         !request.request_id.empty(),
                         !request.token.empty(),
                         !request.session_token.empty());
    QuotaConsumeResult result = auth_service_.ConsumeQuota(request);
    SDK_PRIVATE_LOG_INFO("auth.consume_quota end code={} consumed={} bucket={} remaining={}",
                         result.code,
                         result.consumed,
                         result.bucket,
                         result.remaining);
    return result;
}

} // namespace private_impl
} // namespace sdk
} // namespace editor
