// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "authorization_service.h"

#include <ctime>
#include <utility>

namespace editor {
namespace sdk {

AuthorizationService::AuthorizationService(const ProviderBundle& providers,
                                           AuthzBaseUrlSupplier authz_base_url_supplier)
    : providers_(providers),
      authz_base_url_supplier_(std::move(authz_base_url_supplier)) {}

AuthorizationService::SessionResult AuthorizationService::CreateSession(const std::string& connection_id,
                                                                        const std::string& token) {
    SessionResult result;
    const std::int64_t now_ts = static_cast<std::int64_t>(std::time(NULL));
    if (!providers_.auth_provider) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "provider not ready";
        return result;
    }
    if (token.empty()) {
        result.code = ToCode(SdkStatusCode::AuthRequired);
        result.message = "token required";
        return result;
    }

    AuthValidateRequest request;
    request.token = token;
    request.authz_base_url = AuthzBaseUrl();
    request.now_ts = now_ts;
    const AuthRefreshResult provider_result = providers_.auth_provider->CreateSession(request);
    result.code = provider_result.code;
    result.message = provider_result.message;
    result.token = token;
    result.session_token = provider_result.session_token;
    result.connection_id = connection_id;
    result.expires_in = provider_result.expires_in;
    result.created_at = now_ts;
    result.last_seen_at = now_ts;
    result.auth_context = provider_result.auth_context;

    if (IsOkStatusCode(result.code)) {
        std::lock_guard<std::mutex> lock(sessions_mu_);
        sessions_[connection_id] = result;
    }
    return result;
}

AuthorizationService::SessionResult AuthorizationService::RefreshSession(const std::string& connection_id) {
    SessionResult bound_session = RequireSession(connection_id);
    if (!IsOkStatusCode(bound_session.code)) {
        return bound_session;
    }

    SessionResult result;
    AuthRefreshResult provider_result;
    const std::int64_t now_ts = static_cast<std::int64_t>(std::time(NULL));
    if ((bound_session.auth_context.license_mode == "online_api_key" ||
         bound_session.auth_context.license_mode == "offline_api_key") &&
        !bound_session.token.empty()) {
        AuthValidateRequest validate_request;
        validate_request.token = bound_session.token;
        validate_request.authz_base_url = AuthzBaseUrl();
        validate_request.now_ts = now_ts;
        provider_result = providers_.auth_provider->CreateSession(validate_request);
    } else {
        AuthRefreshRequest request;
        request.session_token = bound_session.session_token;
        request.now_ts = now_ts;
        provider_result = providers_.auth_provider->RefreshSession(request);
    }
    result.code = provider_result.code;
    result.message = provider_result.message;
    result.token = bound_session.token;
    result.session_token = provider_result.session_token;
    result.connection_id = connection_id;
    result.expires_in = provider_result.expires_in;
    result.created_at = bound_session.created_at;
    result.last_seen_at = now_ts;
    result.auth_context = provider_result.auth_context;

    if (IsOkStatusCode(result.code)) {
        std::lock_guard<std::mutex> lock(sessions_mu_);
        sessions_[connection_id] = result;
    }
    return result;
}

AuthorizationService::SessionResult AuthorizationService::GetContext(const std::string& connection_id) const {
    return RequireSession(connection_id);
}

AuthorizationService::SessionResult AuthorizationService::RequireSession(const std::string& connection_id) const {
    SessionResult result;
    std::lock_guard<std::mutex> lock(sessions_mu_);
    const std::map<std::string, SessionResult>::const_iterator it = sessions_.find(connection_id);
    if (it == sessions_.end()) {
        result.code = ToCode(SdkStatusCode::AuthRequired);
        result.message = "session required";
        return result;
    }
    return it->second;
}

AuthorizationService::SessionResult AuthorizationService::RequireSessionToken(const std::string& session_token) const {
    SessionResult result;
    if (session_token.empty()) {
        result.code = ToCode(SdkStatusCode::AuthRequired);
        result.message = "session token required";
        return result;
    }
    std::lock_guard<std::mutex> lock(sessions_mu_);
    for (std::map<std::string, SessionResult>::const_iterator it = sessions_.begin();
         it != sessions_.end();
         ++it) {
        if (it->second.session_token == session_token) {
            result = it->second;
            if (result.connection_id.empty()) {
                result.connection_id = it->first;
            }
            return result;
        }
    }
    result.code = ToCode(SdkStatusCode::AuthRequired);
    result.message = "session token invalid";
    return result;
}

AuthorizationService::SessionResult AuthorizationService::RequireCapability(const std::string& connection_id,
                                                                            const std::string& capability) const {
    SessionResult result = RequireSession(connection_id);
    if (!IsOkStatusCode(result.code)) {
        return result;
    }
    if (!HasCapability(result.auth_context, capability)) {
        result.code = ToCode(SdkStatusCode::CapabilityNotAllowed);
        result.message = "capability not allowed";
    }
    return result;
}

AuthorizationService::SessionResult AuthorizationService::DestroySession(const std::string& connection_id) {
    SessionResult result;
    result.message = "ok";
    {
        std::lock_guard<std::mutex> lock(sessions_mu_);
        sessions_.erase(connection_id);
    }
    return result;
}

AuthorizationService::SessionResult AuthorizationService::ActivateOffline(const std::string& connection_id,
                                                                          const std::string& auth_code) {
    SessionResult bound_session = RequireSession(connection_id);
    if (!IsOkStatusCode(bound_session.code)) {
        return bound_session;
    }
    if (!providers_.auth_provider) {
        bound_session.code = ToCode(SdkStatusCode::ProviderNotReady);
        bound_session.message = "provider not ready";
        return bound_session;
    }

    OfflineActivateRequest request;
    request.token = bound_session.token;
    request.session_token = bound_session.session_token;
    request.auth_code = auth_code;
    request.now_ts = static_cast<std::int64_t>(std::time(NULL));
    const OfflineActivateResult provider_result = providers_.auth_provider->ActivateOffline(request);

    SessionResult result;
    result.code = provider_result.code;
    result.message = provider_result.message;
    result.token = bound_session.token;
    result.session_token = provider_result.session_token;
    result.connection_id = connection_id;
    result.expires_in = provider_result.expires_in;
    result.created_at = bound_session.created_at;
    result.last_seen_at = request.now_ts;
    result.auth_context = provider_result.auth_context;
    if (IsOkStatusCode(result.code)) {
        std::lock_guard<std::mutex> lock(sessions_mu_);
        sessions_[connection_id] = result;
    }
    return result;
}

AuthorizationService::SessionResult AuthorizationService::ConsumeQuota(const std::string& connection_id,
                                                                       const std::string& capability,
                                                                       const std::string& request_id,
                                                                       int units) {
    SessionResult bound_session = RequireSession(connection_id);
    if (!IsOkStatusCode(bound_session.code)) {
        return bound_session;
    }
    if (!providers_.auth_provider) {
        bound_session.code = ToCode(SdkStatusCode::ProviderNotReady);
        bound_session.message = "provider not ready";
        return bound_session;
    }

    QuotaConsumeRequest request;
    request.token = bound_session.token;
    request.session_token = bound_session.session_token;
    request.authz_base_url = AuthzBaseUrl();
    request.capability = capability;
    request.request_id = request_id;
    request.units = units;
    request.now_ts = static_cast<std::int64_t>(std::time(NULL));
    const QuotaConsumeResult provider_result = providers_.auth_provider->ConsumeQuota(request);

    SessionResult result = bound_session;
    result.code = provider_result.code;
    result.message = provider_result.message;
    result.auth_context = provider_result.auth_context;
    result.last_seen_at = request.now_ts;
    if (IsOkStatusCode(result.code)) {
        std::lock_guard<std::mutex> lock(sessions_mu_);
        std::map<std::string, SessionResult>::iterator it = sessions_.find(connection_id);
        if (it != sessions_.end()) {
            it->second.auth_context = provider_result.auth_context;
            it->second.last_seen_at = request.now_ts;
            result = it->second;
            result.code = provider_result.code;
            result.message = provider_result.message;
        }
    }
    return result;
}

void AuthorizationService::ClearConnection(const std::string& connection_id) {
    std::lock_guard<std::mutex> lock(sessions_mu_);
    sessions_.erase(connection_id);
}

std::size_t AuthorizationService::ActiveSessionCount() const {
    std::lock_guard<std::mutex> lock(sessions_mu_);
    return sessions_.size();
}

std::vector<AuthorizationService::SessionResult> AuthorizationService::SnapshotSessions() const {
    std::vector<SessionResult> sessions;
    std::lock_guard<std::mutex> lock(sessions_mu_);
    for (std::map<std::string, SessionResult>::const_iterator it = sessions_.begin();
         it != sessions_.end();
         ++it) {
        SessionResult snapshot = it->second;
        if (snapshot.connection_id.empty()) {
            snapshot.connection_id = it->first;
        }
        sessions.push_back(snapshot);
    }
    return sessions;
}

std::string AuthorizationService::AuthzBaseUrl() const {
    return authz_base_url_supplier_ ? authz_base_url_supplier_() : std::string();
}

bool AuthorizationService::HasCapability(const AuthContext& auth_context, const std::string& capability) const {
    for (std::vector<std::string>::const_iterator it = auth_context.capabilities.begin();
         it != auth_context.capabilities.end();
         ++it) {
        if (*it == capability) {
            return true;
        }
    }
    return false;
}

} // namespace sdk
} // namespace editor
