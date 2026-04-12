// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <map>
#include <mutex>
#include <string>

#include "sdk_auth_types.h"
#include "sdk_provider_bundle.h"

namespace editor {
namespace sdk {

class AuthorizationService {
public:
    struct SessionResult {
        int code = ToCode(SdkStatusCode::Ok);
        std::string message = "ok";
        std::string session_token;
        int expires_in = 0;
        AuthContext auth_context;
    };

    explicit AuthorizationService(const ProviderBundle& providers);

    SessionResult CreateSession(const std::string& connection_id, const std::string& token);
    SessionResult RefreshSession(const std::string& connection_id);
    SessionResult GetContext(const std::string& connection_id) const;
    SessionResult RequireSession(const std::string& connection_id) const;
    SessionResult RequireCapability(const std::string& connection_id, const std::string& capability) const;
    SessionResult DestroySession(const std::string& connection_id);
    void ClearConnection(const std::string& connection_id);
    std::size_t ActiveSessionCount() const;

private:
    bool HasCapability(const AuthContext& auth_context, const std::string& capability) const;

    ProviderBundle providers_;
    mutable std::mutex sessions_mu_;
    std::map<std::string, SessionResult> sessions_;
};

} // namespace sdk
} // namespace editor
