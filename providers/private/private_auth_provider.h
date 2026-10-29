// Copyright (c) 2026 CZUR Tech. All rights reserved.

#pragma once

#include "i_sdk_auth_provider.h"
#include "private_sdk_auth_service.h"

namespace editor {
namespace sdk {
namespace private_impl {

class PrivateAuthProvider : public ISdkAuthProvider {
public:
    std::string ProviderName() const override;
    AuthValidateResult ValidateToken(const AuthValidateRequest& request) override;
    AuthRefreshResult CreateSession(const AuthValidateRequest& request) override;
    AuthRefreshResult RefreshSession(const AuthRefreshRequest& request) override;
    SessionValidateResult ValidateSession(const SessionValidateRequest& request) override;
    AuthContextResult GetAuthContext(const AuthLookupRequest& request) override;
    OfflineActivateResult ActivateOffline(const OfflineActivateRequest& request) override;
    QuotaConsumeResult ConsumeQuota(const QuotaConsumeRequest& request) override;

private:
    PrivateSdkAuthService auth_service_;
};

} // namespace private_impl
} // namespace sdk
} // namespace editor
