// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "command_application_service.h"
#include "mock_provider_factory.h"

namespace editor {
namespace sdk {
namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// Preserve the existing mock authorization behavior while supplying metadata
// that a real provider obtains from the verified API Key payload.
class RegionAuthProvider : public ISdkAuthProvider {
public:
    RegionAuthProvider(const std::shared_ptr<ISdkAuthProvider>& delegate, const std::string& region)
        : delegate_(delegate), region_(region) {}

    std::string ProviderName() const override { return "region-auth-contract"; }
    AuthValidateResult ValidateToken(const AuthValidateRequest& request) override {
        return WithRegion(delegate_->ValidateToken(request));
    }
    AuthRefreshResult CreateSession(const AuthValidateRequest& request) override {
        return WithRegion(delegate_->CreateSession(request));
    }
    AuthRefreshResult RefreshSession(const AuthRefreshRequest& request) override {
        return WithRegion(delegate_->RefreshSession(request));
    }
    SessionValidateResult ValidateSession(const SessionValidateRequest& request) override {
        return WithRegion(delegate_->ValidateSession(request));
    }
    AuthContextResult GetAuthContext(const AuthLookupRequest& request) override {
        return WithRegion(delegate_->GetAuthContext(request));
    }
    OfflineActivateResult ActivateOffline(const OfflineActivateRequest& request) override {
        return WithRegion(delegate_->ActivateOffline(request));
    }
    QuotaConsumeResult ConsumeQuota(const QuotaConsumeRequest& request) override {
        return WithRegion(delegate_->ConsumeQuota(request));
    }

private:
    template <typename Result>
    Result WithRegion(Result result) const {
        if (IsOkStatusCode(result.code)) {
            result.auth_context.region = region_;
        }
        return result;
    }

    std::shared_ptr<ISdkAuthProvider> delegate_;
    std::string region_;
};

Json Request(const std::string& method, const Json& params = Json::object()) {
    return Json{{"request_id", method}, {"method", method}, {"params", params}};
}

void RequireResponseRegion(const Json& response, const std::string& region, const std::string& operation) {
    Require(response.value("code", -1) == ToCode(SdkStatusCode::Ok), operation + " must succeed");
    const Json context = response.at("data").at("auth_context");
    Require(context.contains("region") && context.at("region") == region,
            operation + " must preserve the provider API Key region");
}

void RequireAdminRegion(CommandApplicationService& service, const std::string& region) {
    const Json sessions = service.BuildAdminAuthJson().at("sessions");
    Require(sessions.size() == 1, "admin auth must expose exactly the bound session");
    Require(sessions[0].contains("region") && sessions[0].at("region") == region,
            "admin session must expose the provider API Key region");
}

void TestSessionRegion(const std::string& region) {
    ProviderBundle providers = mock::CreateProviderBundle();
    providers.auth_provider.reset(new RegionAuthProvider(providers.auth_provider, region));
    SdkConfig config;
    CommandApplicationService service(config, providers);
    service.SetProviderNames(Json::object());
    const std::string connection_id = "auth-region-contract";

    RequireResponseRegion(service.HandleRequest(connection_id,
                                                Request("auth.create_session", Json{{"token", "mock-token"}})),
                          region, "auth.create_session");
    RequireAdminRegion(service, region);
    RequireResponseRegion(service.HandleRequest(connection_id, Request("auth.get_context")),
                          region, "auth.get_context");
    RequireResponseRegion(service.HandleRequest(connection_id, Request("auth.refresh_session")),
                          region, "auth.refresh_session");
    RequireResponseRegion(service.HandleRequest(connection_id, Request("auth.get_context")),
                          region, "auth.get_context after refresh");
    RequireAdminRegion(service, region);

    service.OnConnectionClosed(connection_id);
    Require(service.BuildAdminAuthJson().at("sessions").empty(), "closed connection must leave no admin session");
}

} // namespace
} // namespace sdk
} // namespace editor

int main() {
    try {
        editor::sdk::TestSessionRegion("cn");
        editor::sdk::TestSessionRegion("global");
        editor::sdk::TestSessionRegion("default");
        // Providers without parsed metadata must not invent an issuance region.
        editor::sdk::TestSessionRegion("");
        std::cout << "sdk_open_auth_region_contract_test passed" << std::endl;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "sdk_open_auth_region_contract_test failed: " << error.what() << std::endl;
        return 1;
    }
}
