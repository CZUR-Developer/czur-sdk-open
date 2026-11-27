// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include <iostream>
#include <memory>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "image_enhance_quota.h"

namespace editor {
namespace sdk {
namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class CountingAuthProvider : public ISdkAuthProvider {
public:
    std::string ProviderName() const override { return "quota-test-auth"; }
    AuthValidateResult ValidateToken(const AuthValidateRequest&) override { return AuthValidateResult(); }
    AuthRefreshResult CreateSession(const AuthValidateRequest&) override { return AuthRefreshResult(); }
    AuthRefreshResult RefreshSession(const AuthRefreshRequest&) override { return AuthRefreshResult(); }
    SessionValidateResult ValidateSession(const SessionValidateRequest&) override { return SessionValidateResult(); }
    AuthContextResult GetAuthContext(const AuthLookupRequest&) override { return AuthContextResult(); }
    OfflineActivateResult ActivateOffline(const OfflineActivateRequest&) override { return OfflineActivateResult(); }

    QuotaConsumeResult ConsumeQuota(const QuotaConsumeRequest& request) override {
        requests.push_back(request);
        QuotaConsumeResult result;
        if (fail) {
            result.code = ToCode(SdkStatusCode::UsageLimitExceeded);
            result.message = "quota denied";
        } else {
            result.consumed = true;
        }
        return result;
    }

    bool fail = false;
    std::vector<QuotaConsumeRequest> requests;
};

void TestAliasesAndOnlineDetection() {
    Require(NormalizeOnlineEnhanceCapability("document_rectify_enhance") == "doc_crop_enhance",
            "document rectify alias should normalize to quota capability");
    Require(NormalizeOnlineEnhanceCapability("remove_background_texture") == "doc_repair",
            "background texture alias should normalize to quota capability");
    Require(IsOnlineEnhanceCapability("document_rectify_enhance"), "alias should be online");
    Require(IsOnlineEnhanceCapability("remove_handwriting"), "remove handwriting should be online");
    Require(!IsOnlineEnhanceCapability("offline_color"), "offline capability should not be online");
}

void TestConfirmCountsAndCredentials() {
    std::shared_ptr<CountingAuthProvider> auth(new CountingAuthProvider);
    ProviderBundle providers;
    providers.auth_provider = auth;
    OnlineEnhanceQuotaCredentials credentials;
    credentials.online_api_key = "api-key";
    credentials.online_session_token = "session-token";
    credentials.authz_base_url = "https://authz.example.test";
    std::map<std::string, int> usage;
    usage["doc_crop_enhance"] = 2;
    usage["remove_handwriting"] = 0;

    const QuotaConsumeResult result = ConfirmOnlineEnhanceQuota(providers, credentials, "task-1", usage);
    Require(IsOkStatusCode(result.code), "quota confirmation should succeed");
    Require(auth->requests.size() == 2, "quota confirmation should consume each capability");
    Require(auth->requests[0].token == credentials.online_api_key &&
                auth->requests[0].session_token == credentials.online_session_token &&
                auth->requests[0].authz_base_url == credentials.authz_base_url,
            "quota confirmation should forward credentials");
    Require(auth->requests[0].request_id == "image.enhance:task-1:doc_crop_enhance",
            "quota request id should preserve image enhance prefix");
    Require(auth->requests[0].units == 2 && auth->requests[1].units == 1,
            "quota units should use provided counts with a minimum of one");
}

void TestConfirmMissingProviderAndAuthFailure() {
    ProviderBundle missing;
    OnlineEnhanceQuotaCredentials credentials;
    std::map<std::string, int> usage;
    usage["doc_crop_enhance"] = 1;
    const QuotaConsumeResult missing_result = ConfirmOnlineEnhanceQuota(missing, credentials, "task-2", usage);
    Require(missing_result.code == ToCode(SdkStatusCode::ProviderNotReady),
            "missing auth provider should fail non-empty quota confirmation");

    std::shared_ptr<CountingAuthProvider> auth(new CountingAuthProvider);
    auth->fail = true;
    ProviderBundle providers;
    providers.auth_provider = auth;
    const QuotaConsumeResult failure = ConfirmOnlineEnhanceQuota(providers, credentials, "task-3", usage);
    Require(failure.code == ToCode(SdkStatusCode::UsageLimitExceeded),
            "auth failure should be returned to caller");
}

} // namespace
} // namespace sdk
} // namespace editor

int main() {
    try {
        editor::sdk::TestAliasesAndOnlineDetection();
        editor::sdk::TestConfirmCountsAndCredentials();
        editor::sdk::TestConfirmMissingProviderAndAuthFailure();
        std::cout << "sdk_open_image_enhance_quota_test passed" << std::endl;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "sdk_open_image_enhance_quota_test failed: " << error.what() << std::endl;
        return 1;
    }
}
