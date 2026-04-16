// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

#include "sdk_auth_types.h"

namespace editor {
namespace sdk {

class ISdkAuthProvider {
public:
    virtual ~ISdkAuthProvider() = default;

    virtual std::string ProviderName() const = 0;
    virtual AuthValidateResult ValidateToken(const AuthValidateRequest& request) = 0;
    virtual AuthRefreshResult CreateSession(const AuthValidateRequest& request) = 0;
    virtual AuthRefreshResult RefreshSession(const AuthRefreshRequest& request) = 0;
    virtual SessionValidateResult ValidateSession(const SessionValidateRequest& request) = 0;
    virtual AuthContextResult GetAuthContext(const AuthLookupRequest& request) = 0;
    virtual OfflineActivateResult ActivateOffline(const OfflineActivateRequest& request) = 0;
    virtual QuotaConsumeResult ConsumeQuota(const QuotaConsumeRequest& request) = 0;
};

} // namespace sdk
} // namespace editor
