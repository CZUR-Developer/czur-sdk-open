// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "authorization_service.h"

#include <ctime>

namespace editor {
namespace sdk {

AuthorizationService::AuthorizationService(const ProviderBundle& providers)
    : providers_(providers) {}

AuthorizationService::SessionResult AuthorizationService::CreateSession(const std::string& connection_id,
                                                                        const std::string& token) {
    SessionResult result;
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
    request.now_ts = static_cast<std::int64_t>(std::time(NULL));
    const AuthRefreshResult provider_result = providers_.auth_provider->CreateSession(request);
    result.code = provider_result.code;
    result.message = provider_result.message;
    result.session_token = provider_result.session_token;
    result.expires_in = provider_result.expires_in;
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

    AuthRefreshRequest request;
    request.session_token = bound_session.session_token;
    request.now_ts = static_cast<std::int64_t>(std::time(NULL));
    const AuthRefreshResult provider_result = providers_.auth_provider->RefreshSession(request);
    SessionResult result;
    result.code = provider_result.code;
    result.message = provider_result.message;
    result.session_token = provider_result.session_token;
    result.expires_in = provider_result.expires_in;
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

void AuthorizationService::ClearConnection(const std::string& connection_id) {
    std::lock_guard<std::mutex> lock(sessions_mu_);
    sessions_.erase(connection_id);
}

std::size_t AuthorizationService::ActiveSessionCount() const {
    std::lock_guard<std::mutex> lock(sessions_mu_);
    return sessions_.size();
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
