// Copyright (c) 2026 CZUR Tech. All rights reserved.

#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#endif

#include "auth/private_sdk_auth_service.h"
#include "private_sdk_support.h"
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

editor::sdk::private_impl::ApiKeySpec BuildOfflineApiKeySpec(bool commercial_authorized = true) {
    editor::sdk::private_impl::ApiKeySpec spec;
    spec.account_type = editor::sdk::SdkAccountType::Svip;
    spec.license_mode = "offline_api_key";
    spec.host_auth_mode = "activation_required";
    spec.commercial_authorized = commercial_authorized;
    editor::sdk::SdkDeviceGrant grant;
    grant.vid = 0x1234;
    grant.pid = 0x5678;
    spec.device_scope.push_back(grant);
    spec.issued_at = 1710000000;
    spec.expires_at = 0;
    spec.key_id = "key_offline_activation_smoke";
    spec.nonce = "offline-activation-smoke";
    return spec;
}

bool FileExists(const std::string& path) {
    std::ifstream input(path.c_str(), std::ios::binary);
    return input.good();
}

const editor::sdk::AuthQuotaBucket* FindQuotaBucket(const editor::sdk::AuthContext& auth_context,
                                                     const std::string& bucket_name) {
    for (std::vector<editor::sdk::AuthQuotaBucket>::const_iterator it = auth_context.quota_buckets.begin();
         it != auth_context.quota_buckets.end();
         ++it) {
        if (it->bucket == bucket_name) {
            return &(*it);
        }
    }
    return NULL;
}

} // namespace

int main() {
    const std::string runtime_root =
        std::string("/tmp/czur-sdk-offline-activation-smoke-") +
        std::to_string(static_cast<long long>(std::time(NULL))) +
#if defined(_WIN32)
        "-win";
#else
        "-" + std::to_string(static_cast<long long>(getpid()));
#endif
    const std::string home_dir = runtime_root + "/home";
    const std::string runtime_dir = runtime_root + "/runtime";
    const std::string auth_license_dir = runtime_dir + "/auth_license";

    SetEnvValue("CZUR_SDK_MASTER_SECRET", "czur-sdk-offline-activation-smoke-secret");
    SetEnvValue("CZUR_SDK_RUNTIME_DIR", runtime_dir.c_str());
    SetEnvValue("CZUR_AUTH_LICENSE_DIR", auth_license_dir.c_str());
    SetEnvValue("HOME", home_dir.c_str());

    std::string init_error;
    if (!editor::sdk::private_impl::EnsureDirectoryRecursive(home_dir) ||
        !editor::sdk::private_impl::EnsureDirectoryRecursive(runtime_dir) ||
        !editor::sdk::private_impl::EnsureDirectoryRecursive(auth_license_dir)) {
        std::cerr << "failed to create smoke runtime directories" << std::endl;
        return 1;
    }

    editor::sdk::private_impl::PrivateSdkAuthService auth_service;
    std::string api_key;
    std::string err;
    if (!auth_service.GenerateApiKey(BuildOfflineApiKeySpec(), &api_key, &err)) {
        std::cerr << "generate offline api key failed: " << err << std::endl;
        return 1;
    }

    editor::sdk::AuthValidateRequest validate_request;
    validate_request.token = api_key;
    validate_request.now_ts = static_cast<std::int64_t>(std::time(NULL));
    const editor::sdk::AuthValidateResult validate_result = auth_service.ValidateApiKey(validate_request);
    if (validate_result.code != editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok) ||
        validate_result.auth_context.machine_code.empty() ||
        validate_result.auth_context.entitlement_state != "offline_trial") {
        std::cerr << "validate offline api key failed: code=" << validate_result.code
                  << " message=" << validate_result.message
                  << " machine_code=" << validate_result.auth_context.machine_code
                  << " entitlement=" << validate_result.auth_context.entitlement_state << std::endl;
        return 1;
    }

    editor::sdk::private_impl::ApiKeySpec no_commercial_spec = BuildOfflineApiKeySpec(false);
    no_commercial_spec.key_id = "key_offline_no_commercial_host_activation_smoke";
    no_commercial_spec.nonce = "offline-no-commercial-host-activation-smoke";
    std::string no_commercial_api_key;
    if (!auth_service.GenerateApiKey(no_commercial_spec, &no_commercial_api_key, &err)) {
        std::cerr << "generate no-commercial offline api key failed: " << err << std::endl;
        return 1;
    }
    editor::sdk::AuthValidateRequest no_commercial_validate_request;
    no_commercial_validate_request.token = no_commercial_api_key;
    no_commercial_validate_request.now_ts = static_cast<std::int64_t>(std::time(NULL));
    editor::sdk::AuthValidateResult no_commercial_validate_result =
        auth_service.ValidateApiKey(no_commercial_validate_request);
    if (no_commercial_validate_result.code != editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok) ||
        no_commercial_validate_result.auth_context.account_type != editor::sdk::SdkAccountType::Trial ||
        no_commercial_validate_result.auth_context.commercial_authorized ||
        no_commercial_validate_result.auth_context.entitlement_state != "offline_trial") {
        std::cerr << "no-commercial offline key should stay trial before host activation: code="
                  << no_commercial_validate_result.code
                  << " message=" << no_commercial_validate_result.message
                  << " entitlement=" << no_commercial_validate_result.auth_context.entitlement_state << std::endl;
        return 1;
    }

    static const char* kTrialQuotaBuckets[] = {"capture", "image", "file", "trial_vip"};
    for (std::size_t index = 0; index < sizeof(kTrialQuotaBuckets) / sizeof(kTrialQuotaBuckets[0]); ++index) {
        const editor::sdk::AuthQuotaBucket* bucket =
            FindQuotaBucket(no_commercial_validate_result.auth_context, kTrialQuotaBuckets[index]);
        if (bucket == NULL || bucket->limit != 200 || bucket->remaining != 200) {
            std::cerr << "offline trial quota bucket is invalid: " << kTrialQuotaBuckets[index] << std::endl;
            return 1;
        }
    }

    // Older releases persisted only `used`. Seed that historical representation
    // directly so the upgraded service must derive the new 200-use limit from it.
    const editor::sdk::private_impl::ParsedApiKey parsed_no_commercial_key =
        auth_service.ParseApiKey(no_commercial_api_key, no_commercial_validate_request.now_ts);
    const std::string offline_store_path = runtime_dir + "/offline_entitlements.json";
    nlohmann::json offline_store_json;
    if (parsed_no_commercial_key.code != editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok) ||
        parsed_no_commercial_key.token_digest.empty() ||
        !editor::sdk::private_impl::LoadJsonFile(offline_store_path, &offline_store_json) ||
        !offline_store_json.is_object()) {
        std::cerr << "failed to prepare historical offline trial quota record" << std::endl;
        return 1;
    }
    nlohmann::json legacy_request_ids = nlohmann::json::array();
    legacy_request_ids.push_back("legacy-offline-trial-capture-usage");
    offline_store_json["records"][parsed_no_commercial_key.token_digest]["quotas"]["capture"] =
        nlohmann::json{{"used", 5}, {"request_ids", legacy_request_ids}};
    if (!editor::sdk::private_impl::SaveJsonFile(offline_store_path, offline_store_json)) {
        std::cerr << "failed to persist historical offline trial quota record" << std::endl;
        return 1;
    }

    editor::sdk::private_impl::PrivateSdkAuthService restored_trial_service;
    const editor::sdk::AuthValidateResult restored_trial_validate_result =
        restored_trial_service.ValidateApiKey(no_commercial_validate_request);
    const editor::sdk::AuthQuotaBucket* restored_capture_bucket =
        FindQuotaBucket(restored_trial_validate_result.auth_context, "capture");
    if (restored_trial_validate_result.code != editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok) ||
        restored_trial_validate_result.auth_context.entitlement_state != "offline_trial" ||
        restored_capture_bucket == NULL || restored_capture_bucket->limit != 200 ||
        restored_capture_bucket->remaining != 195) {
        std::cerr << "offline trial persisted quota was not upgraded to the 200-use default" << std::endl;
        return 1;
    }

    editor::sdk::private_impl::AuthCodeGenerateResult auth_code_result =
        auth_service.GenerateAuthCode(validate_result.auth_context.machine_code, "20261231", 5);
    if (auth_code_result.code != editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok)) {
        std::cerr << "generate auth code failed: code=" << auth_code_result.code
                  << " message=" << auth_code_result.message << std::endl;
        return 1;
    }

    editor::sdk::OfflineActivateRequest activate_request;
    activate_request.token = api_key;
    activate_request.auth_code = auth_code_result.auth_code;
    activate_request.now_ts = static_cast<std::int64_t>(std::time(NULL));
    const editor::sdk::OfflineActivateResult activate_result =
        auth_service.ActivateOffline(activate_request);
    if (activate_result.code != editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok) ||
        !activate_result.activated ||
        activate_result.auth_context.entitlement_state != "offline_unlocked") {
        std::cerr << "activate offline failed: code=" << activate_result.code
                  << " message=" << activate_result.message
                  << " activated=" << activate_result.activated
                  << " entitlement=" << activate_result.auth_context.entitlement_state << std::endl;
        return 1;
    }
    if (!FileExists(auth_license_dir + "/.libaY3p1cg==.so") ||
        !FileExists(auth_license_dir + "/ab2NyX3BhcnQx.data")) {
        std::cerr << "activation did not persist auth license files under " << auth_license_dir << std::endl;
        return 1;
    }

    editor::sdk::OfflineActivateRequest no_commercial_activate_request;
    no_commercial_activate_request.token = no_commercial_api_key;
    no_commercial_activate_request.auth_code = auth_code_result.auth_code;
    no_commercial_activate_request.now_ts = static_cast<std::int64_t>(std::time(NULL));
    const editor::sdk::OfflineActivateResult no_commercial_activate_result =
        auth_service.ActivateOffline(no_commercial_activate_request);
    if (no_commercial_activate_result.code != editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok) ||
        !no_commercial_activate_result.activated ||
        no_commercial_activate_result.auth_context.account_type != editor::sdk::SdkAccountType::Svip ||
        no_commercial_activate_result.auth_context.entitlement_state != "offline_unlocked" ||
        no_commercial_activate_result.auth_context.commercial_authorized) {
        std::cerr << "no-commercial offline activation should follow host activation: code="
                  << no_commercial_activate_result.code
                  << " message=" << no_commercial_activate_result.message
                  << " account=" << static_cast<int>(no_commercial_activate_result.auth_context.account_type)
                  << " entitlement=" << no_commercial_activate_result.auth_context.entitlement_state << std::endl;
        return 1;
    }
    no_commercial_validate_result = auth_service.ValidateApiKey(no_commercial_validate_request);
    if (no_commercial_validate_result.code != editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok) ||
        no_commercial_validate_result.auth_context.account_type != editor::sdk::SdkAccountType::Svip ||
        no_commercial_validate_result.auth_context.commercial_authorized ||
        no_commercial_validate_result.auth_context.entitlement_state != "offline_unlocked") {
        std::cerr << "no-commercial offline key should stay unlocked after its host activation: code="
                  << no_commercial_validate_result.code
                  << " message=" << no_commercial_validate_result.message
                  << " account=" << static_cast<int>(no_commercial_validate_result.auth_context.account_type)
                  << " entitlement=" << no_commercial_validate_result.auth_context.entitlement_state << std::endl;
        return 1;
    }

    const editor::sdk::AuthValidateResult post_activate_validate_result =
        auth_service.ValidateApiKey(validate_request);
    if (post_activate_validate_result.code != editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok) ||
        post_activate_validate_result.auth_context.entitlement_state != "offline_unlocked") {
        std::cerr << "post-activation validate did not load persisted offline entitlement: code="
                  << post_activate_validate_result.code
                  << " message=" << post_activate_validate_result.message
                  << " entitlement=" << post_activate_validate_result.auth_context.entitlement_state << std::endl;
        return 1;
    }

    // A connected CZUR device makes a compatible offline key formal without
    // requiring a Host activation code. This must not manufacture a commercial
    // authorization fact, and must not be constrained by the key's device scope.
    SetEnvValue("CZUR_SDK_AUTH_CLIENT_DEVICES_JSON",
                R"([{"deviceId":"czur-owned-device-smoke","vid":1276,"pid":25395,"commercialAuthorizationHint":true,
                    "czurDevice":true,"thirdParty":false}])");
    editor::sdk::private_impl::ApiKeySpec device_auto_authorized_spec = BuildOfflineApiKeySpec(false);
    device_auto_authorized_spec.key_id = "key_offline_czur_device_auto_authorize_smoke";
    device_auto_authorized_spec.nonce = "offline-czur-device-auto-authorize-smoke";
    std::string device_auto_authorized_api_key;
    if (!auth_service.GenerateApiKey(device_auto_authorized_spec, &device_auto_authorized_api_key, &err)) {
        std::cerr << "generate CZUR automatic offline api key failed: " << err << std::endl;
        return 1;
    }
    editor::sdk::AuthValidateRequest device_auto_authorized_validate_request;
    device_auto_authorized_validate_request.token = device_auto_authorized_api_key;
    device_auto_authorized_validate_request.now_ts = static_cast<std::int64_t>(std::time(NULL));
    const editor::sdk::AuthValidateResult device_auto_authorized_validate_result =
        auth_service.ValidateApiKey(device_auto_authorized_validate_request);
    if (device_auto_authorized_validate_result.code != editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok) ||
        device_auto_authorized_validate_result.auth_context.account_type != editor::sdk::SdkAccountType::Svip ||
        device_auto_authorized_validate_result.auth_context.licensed_account_type != editor::sdk::SdkAccountType::Svip ||
        device_auto_authorized_validate_result.auth_context.commercial_authorized ||
        device_auto_authorized_validate_result.auth_context.entitlement_state != "offline_unlocked") {
        std::cerr << "CZUR device did not automatically unlock compatible offline key" << std::endl;
        return 1;
    }
    editor::sdk::QuotaConsumeRequest device_auto_authorized_quota_request;
    device_auto_authorized_quota_request.token = device_auto_authorized_api_key;
    device_auto_authorized_quota_request.capability = "doc_crop_enhance";
    device_auto_authorized_quota_request.request_id = "offline-czur-device-formal-quota";
    device_auto_authorized_quota_request.units = 1;
    device_auto_authorized_quota_request.now_ts = static_cast<std::int64_t>(std::time(NULL));
    const editor::sdk::QuotaConsumeResult device_auto_authorized_quota_result =
        auth_service.ConsumeQuota(device_auto_authorized_quota_request);
    if (device_auto_authorized_quota_result.code != editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok) ||
        device_auto_authorized_quota_result.bucket != "image" ||
        device_auto_authorized_quota_result.limit != 100 ||
        device_auto_authorized_quota_result.auth_context.entitlement_state != "offline_unlocked") {
        std::cerr << "CZUR device automatic offline entitlement still used trial quota" << std::endl;
        return 1;
    }

    editor::sdk::private_impl::ApiKeySpec ordinary_svip_plus_spec = BuildOfflineApiKeySpec(false);
    ordinary_svip_plus_spec.account_type = editor::sdk::SdkAccountType::SvipPlus;
    ordinary_svip_plus_spec.key_id = "key_offline_czur_device_svip_plus_trial_smoke";
    ordinary_svip_plus_spec.nonce = "offline-czur-device-svip-plus-trial-smoke";
    std::string ordinary_svip_plus_api_key;
    if (!auth_service.GenerateApiKey(ordinary_svip_plus_spec, &ordinary_svip_plus_api_key, &err)) {
        std::cerr << "generate ordinary CZUR svip plus offline api key failed: " << err << std::endl;
        return 1;
    }
    editor::sdk::AuthValidateRequest ordinary_svip_plus_validate_request;
    ordinary_svip_plus_validate_request.token = ordinary_svip_plus_api_key;
    ordinary_svip_plus_validate_request.now_ts = static_cast<std::int64_t>(std::time(NULL));
    const editor::sdk::AuthValidateResult ordinary_svip_plus_validate_result =
        auth_service.ValidateApiKey(ordinary_svip_plus_validate_request);
    if (ordinary_svip_plus_validate_result.code != editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok) ||
        ordinary_svip_plus_validate_result.auth_context.account_type != editor::sdk::SdkAccountType::Trial ||
        ordinary_svip_plus_validate_result.auth_context.entitlement_state != "offline_trial") {
        std::cerr << "ordinary CZUR device incorrectly unlocked svip plus offline key" << std::endl;
        return 1;
    }

    SetEnvValue("CZUR_SDK_AUTH_CLIENT_DEVICES_JSON",
                R"([{"deviceId":"czur-svip-plus-device-smoke","vid":1276,"pid":25395,"commercialAuthorizationHint":true,"czurDevice":true,"thirdParty":false,"svipplus":true}])");
    editor::sdk::private_impl::ApiKeySpec svip_plus_device_auto_authorized_spec = BuildOfflineApiKeySpec(false);
    svip_plus_device_auto_authorized_spec.account_type = editor::sdk::SdkAccountType::SvipPlus;
    svip_plus_device_auto_authorized_spec.key_id = "key_offline_czur_svip_plus_device_auto_authorize_smoke";
    svip_plus_device_auto_authorized_spec.nonce = "offline-czur-svip-plus-device-auto-authorize-smoke";
    std::string svip_plus_device_auto_authorized_api_key;
    if (!auth_service.GenerateApiKey(
            svip_plus_device_auto_authorized_spec, &svip_plus_device_auto_authorized_api_key, &err)) {
        std::cerr << "generate special CZUR svip plus offline api key failed: " << err << std::endl;
        return 1;
    }
    editor::sdk::AuthValidateRequest svip_plus_device_auto_authorized_validate_request;
    svip_plus_device_auto_authorized_validate_request.token = svip_plus_device_auto_authorized_api_key;
    svip_plus_device_auto_authorized_validate_request.now_ts = static_cast<std::int64_t>(std::time(NULL));
    const editor::sdk::AuthValidateResult svip_plus_device_auto_authorized_validate_result =
        auth_service.ValidateApiKey(svip_plus_device_auto_authorized_validate_request);
    if (svip_plus_device_auto_authorized_validate_result.code != editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok) ||
        svip_plus_device_auto_authorized_validate_result.auth_context.account_type != editor::sdk::SdkAccountType::SvipPlus ||
        svip_plus_device_auto_authorized_validate_result.auth_context.commercial_authorized ||
        svip_plus_device_auto_authorized_validate_result.auth_context.entitlement_state != "offline_unlocked") {
        std::cerr << "special CZUR device did not automatically unlock svip plus offline key" << std::endl;
        return 1;
    }
    SetEnvValue("CZUR_SDK_AUTH_CLIENT_DEVICES_JSON", NULL);

    return 0;
}
