// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "sdk_auth_types.h"
#include "sdk_provider_bundle.h"

namespace editor {
namespace sdk {

class AuthorizationService {
public:
    using AuthzBaseUrlSupplier = std::function<std::string()>;

    struct SessionResult {
        int code = ToCode(SdkStatusCode::Ok);
        std::string message = "ok";
        std::string token;
        std::string session_token;
        std::string connection_id;
        int expires_in = 0;
        std::int64_t created_at = 0;
        std::int64_t last_seen_at = 0;
        AuthContext auth_context;
    };

    explicit AuthorizationService(const ProviderBundle& providers,
                                  AuthzBaseUrlSupplier authz_base_url_supplier = AuthzBaseUrlSupplier());

    SessionResult CreateSession(const std::string& connection_id, const std::string& token);
    SessionResult RefreshSession(const std::string& connection_id);
    SessionResult GetContext(const std::string& connection_id) const;
    SessionResult RequireSession(const std::string& connection_id) const;
    SessionResult RequireSessionToken(const std::string& session_token) const;
    SessionResult RequireCapability(const std::string& connection_id, const std::string& capability) const;
    SessionResult ActivateOffline(const std::string& connection_id, const std::string& auth_code);
    SessionResult ConsumeQuota(const std::string& connection_id,
                               const std::string& capability,
                               const std::string& request_id,
                               int units = 1);
    SessionResult DestroySession(const std::string& connection_id);
    void ClearConnection(const std::string& connection_id);
    std::size_t ActiveSessionCount() const;
    std::vector<SessionResult> SnapshotSessions() const;

private:
    bool HasCapability(const AuthContext& auth_context, const std::string& capability) const;
    std::string AuthzBaseUrl() const;

    ProviderBundle providers_;
    AuthzBaseUrlSupplier authz_base_url_supplier_;
    mutable std::mutex sessions_mu_;
    std::map<std::string, SessionResult> sessions_;
};

} // namespace sdk
} // namespace editor
