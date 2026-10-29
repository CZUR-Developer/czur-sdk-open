// Copyright (c) 2026 CZUR Tech. All rights reserved.

#include <cstdlib>
#include <iostream>
#include <string>

#include "auth/private_sdk_auth_service.h"
#include "auth/private_sdk_master_secret.h"
#include "sdk_status_code.h"

namespace {

void SetEnvValue(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value == NULL ? "" : value);
#else
    if (value == NULL) {
        unsetenv(name);
    } else {
        setenv(name, value, 1);
    }
#endif
}

editor::sdk::private_impl::ApiKeySpec BuildApiKeySpec() {
    editor::sdk::private_impl::ApiKeySpec spec;
    spec.account_type = editor::sdk::SdkAccountType::Svip;
    spec.license_mode = "offline_api_key";
    spec.host_auth_mode = "none";
    spec.commercial_authorized = true;
    editor::sdk::SdkDeviceGrant grant;
    grant.vid = 0x1234;
    grant.pid = 0x5678;
    spec.device_scope.push_back(grant);
    spec.issued_at = 1710000000;
    spec.expires_at = 0;
    spec.key_id = "key_master_secret_smoke";
    spec.nonce = "master-secret-smoke";
    return spec;
}

int ExpectParsedOk(const editor::sdk::private_impl::ParsedApiKey& parsed, const char* step) {
    if (parsed.code != editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok) ||
        parsed.auth_context.license_mode != "offline_api_key") {
        std::cerr << step << " failed: code=" << parsed.code << " message=" << parsed.message << std::endl;
        return 1;
    }
    return 0;
}

} // namespace

int main() {
    SetEnvValue("CZUR_SDK_MASTER_SECRET", NULL);
    if (editor::sdk::private_impl::IsPrivateSdkMasterSecretEnvConfigured() ||
        editor::sdk::private_impl::GetPrivateSdkMasterSecretSource() != "default" ||
        editor::sdk::private_impl::GetPrivateSdkMasterSecret() != "czur-sdk-private-master-secret-v1") {
        std::cerr << "default master secret state is invalid" << std::endl;
        return 1;
    }

    editor::sdk::private_impl::PrivateSdkAuthService default_service;
    std::string default_api_key;
    std::string err;
    if (!default_service.GenerateApiKey(BuildApiKeySpec(), &default_api_key, &err)) {
        std::cerr << "generate default api key failed: " << err << std::endl;
        return 1;
    }
    if (ExpectParsedOk(default_service.ParseApiKey(default_api_key, 1710000001), "parse default api key") != 0) {
        return 1;
    }

    SetEnvValue("CZUR_SDK_MASTER_SECRET", "czur-sdk-master-secret-smoke-env");
    if (!editor::sdk::private_impl::IsPrivateSdkMasterSecretEnvConfigured() ||
        editor::sdk::private_impl::GetPrivateSdkMasterSecretSource() != "env" ||
        editor::sdk::private_impl::GetPrivateSdkMasterSecret() != "czur-sdk-master-secret-smoke-env") {
        std::cerr << "env master secret state is invalid" << std::endl;
        return 1;
    }

    editor::sdk::private_impl::PrivateSdkAuthService env_service;
    std::string env_api_key;
    if (!env_service.GenerateApiKey(BuildApiKeySpec(), &env_api_key, &err)) {
        std::cerr << "generate env api key failed: " << err << std::endl;
        return 1;
    }
    if (ExpectParsedOk(env_service.ParseApiKey(env_api_key, 1710000001), "parse env api key") != 0) {
        return 1;
    }

    const editor::sdk::private_impl::ParsedApiKey mismatched =
        env_service.ParseApiKey(default_api_key, 1710000001);
    if (mismatched.code != editor::sdk::ToCode(editor::sdk::SdkStatusCode::TokenInvalid)) {
        std::cerr << "expected default api key to fail under env secret, got code=" << mismatched.code
                  << " message=" << mismatched.message << std::endl;
        return 1;
    }

    return 0;
}
