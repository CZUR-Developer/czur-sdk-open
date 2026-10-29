// Copyright (c) 2026 CZUR Tech. All rights reserved.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "auth/private_sdk_auth_service.h"
#include "auth/private_sdk_master_secret.h"
#include "auth/private_sdk_support.h"
#include "sdk_entitlement_policy.h"
#include "sdk_status_code.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

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


std::string JoinTempPath(const std::string& leaf) {
#if defined(_WIN32)
    const char separator = '\\';
#else
    const char separator = '/';
#endif
    const std::string dir = editor::sdk::private_impl::GetSdkRuntimeDir();
    if (!dir.empty() && (dir[dir.size() - 1] == '/' || dir[dir.size() - 1] == '\\')) {
        return dir + leaf;
    }
    return dir + separator + leaf;
}

int CheckHttpsTrustStoreResolver() {
    SetEnvValue("SSL_CERT_FILE", NULL);
    SetEnvValue("SSL_CERT_DIR", NULL);

    const std::string ca_file = JoinTempPath("sdk-private-auth-smoke-ca.pem");
    {
        std::ofstream output(ca_file.c_str(), std::ios::out | std::ios::trunc);
        output << "test ca placeholder\n";
    }
    SetEnvValue("SSL_CERT_FILE", ca_file.c_str());
    editor::sdk::private_impl::SdkHttpsTrustStoreConfig config =
        editor::sdk::private_impl::ResolveSdkHttpsTrustStore();
    if (!config.set_ca_cert_path || config.ca_cert_file_path != ca_file ||
        config.ca_cert_dir_path != "" || config.source != "env-file") {
        std::cerr << "https trust store env file priority invalid: source=" << config.source
                  << " file=" << config.ca_cert_file_path << " dir=" << config.ca_cert_dir_path << std::endl;
        SetEnvValue("SSL_CERT_FILE", NULL);
        std::remove(ca_file.c_str());
        return 1;
    }

    SetEnvValue("SSL_CERT_FILE", NULL);
    std::remove(ca_file.c_str());
    config = editor::sdk::private_impl::ResolveSdkHttpsTrustStore();
#if defined(_WIN32)
    if (config.set_ca_cert_path || config.source != "windows-system-store") {
        std::cerr << "windows https trust store default invalid: source=" << config.source << std::endl;
        return 1;
    }
#elif defined(__linux__)
    if (config.source.find("windows") != std::string::npos) {
        std::cerr << "linux https trust store selected windows source: " << config.source << std::endl;
        return 1;
    }
    if (config.set_ca_cert_path && config.ca_cert_file_path.empty() && config.ca_cert_dir_path.empty()) {
        std::cerr << "linux https trust store configured empty path" << std::endl;
        return 1;
    }
#else
    if (config.source.empty()) {
        std::cerr << "https trust store source should not be empty" << std::endl;
        return 1;
    }
#endif

    SetEnvValue("SSL_CERT_FILE", NULL);
    SetEnvValue("SSL_CERT_DIR", NULL);
    return 0;
}

int CheckUtf8SafeJsonSerialization() {
    const std::string chinese_username = "\xE5\xBA\x9E\xE5\x87\xAF\xE5\x87\xAF";
    nlohmann::json valid_payload = nlohmann::json::object();
    valid_payload["username"] = chinese_username;

    bool replaced_invalid_utf8 = true;
    const std::string valid_json = editor::sdk::private_impl::DumpJsonUtf8ReplacingInvalidBytes(
        valid_payload, &replaced_invalid_utf8);
    const nlohmann::json parsed_valid = nlohmann::json::parse(valid_json, NULL, false);
    if (replaced_invalid_utf8 || parsed_valid.is_discarded() ||
        parsed_valid.value("username", std::string()) != chinese_username) {
        std::cerr << "valid UTF-8 username was not preserved" << std::endl;
        return 1;
    }

    std::string windows_ansi_username;
    windows_ansi_username.push_back(static_cast<char>(0xc5));
    windows_ansi_username.push_back(static_cast<char>(0xe2));
    nlohmann::json invalid_payload = nlohmann::json::object();
    invalid_payload["username"] = windows_ansi_username;

    replaced_invalid_utf8 = false;
    const std::string repaired_json = editor::sdk::private_impl::DumpJsonUtf8ReplacingInvalidBytes(
        invalid_payload, &replaced_invalid_utf8);
    const nlohmann::json parsed_repaired = nlohmann::json::parse(repaired_json, NULL, false);
    if (!replaced_invalid_utf8 || parsed_repaired.is_discarded() ||
        !parsed_repaired.contains("username") || !parsed_repaired["username"].is_string()) {
        std::cerr << "invalid Windows ANSI username was not serialized as valid UTF-8" << std::endl;
        return 1;
    }
    return 0;
}

bool HasCapability(const editor::sdk::AuthContext& auth_context, const std::string& capability) {
    return std::find(auth_context.capabilities.begin(), auth_context.capabilities.end(), capability) !=
           auth_context.capabilities.end();
}

const editor::sdk::AuthQuotaBucket* FindQuotaBucket(const editor::sdk::AuthContext& auth_context,
                                                    const std::string& bucket) {
    for (std::vector<editor::sdk::AuthQuotaBucket>::const_iterator it = auth_context.quota_buckets.begin();
         it != auth_context.quota_buckets.end();
         ++it) {
        if (it->bucket == bucket) {
            return &(*it);
        }
    }
    return NULL;
}

bool HasExpectedValidateClientMetadata(const nlohmann::json& request_json) {
    if (!request_json.contains("client") || !request_json["client"].is_object()) {
        return false;
    }
    const nlohmann::json& client_json = request_json["client"];
    if (client_json.value("metadataVersion", std::string()) != "capeditor.open.authz.client.v1") {
        return false;
    }
    if (client_json.contains("machineCode") && !client_json["machineCode"].is_string()) {
        return false;
    }
    if (!client_json.contains("host") || !client_json["host"].is_object()) {
        return false;
    }
    const nlohmann::json& host_json = client_json["host"];
    if (!host_json.contains("hostname") || !host_json["hostname"].is_string()) {
        return false;
    }
    if (!host_json.contains("os") || !host_json["os"].is_object()) {
        return false;
    }
    if (!client_json.contains("devices") || !client_json["devices"].is_array() ||
        client_json["devices"].size() != 1) {
        return false;
    }
    const nlohmann::json& device_json = client_json["devices"][0];
    return device_json.value("deviceId", std::string()) == "device-metadata-smoke" &&
           device_json.value("model", std::string()) == "ET24 Pro" &&
           device_json.value("displayName", std::string()) == "CZUR ET24 Pro" &&
           device_json.value("vid", 0) == 0x1234 &&
           device_json.value("pid", 0) == 0x5678 &&
           device_json.value("status", std::string()) == "online" &&
           device_json.value("supportsVideo", false) &&
           device_json.value("imageTransferProtocol", false) &&
           !device_json.value("czurDevice", true) &&
           device_json.value("thirdParty", false) &&
           !device_json.value("commercialAuthorizationHint", true);
}

bool HasCzurOwnedDeviceAuthorizationHint(const nlohmann::json& request_json) {
    if (!request_json.contains("client") || !request_json["client"].is_object()) {
        return false;
    }
    const nlohmann::json& client_json = request_json["client"];
    if (!client_json.contains("devices") || !client_json["devices"].is_array() ||
        client_json["devices"].empty()) {
        return false;
    }
    const nlohmann::json& device_json = client_json["devices"][0];
    return device_json.value("vid", 0) == 0x04FC &&
           device_json.value("pid", 0) == 0x6333 &&
           device_json.value("czurDevice", false) &&
           !device_json.value("thirdParty", true) &&
           device_json.value("commercialAuthorizationHint", false);
}

bool HasSvipPlusDeviceAuthorizationHint(const nlohmann::json& request_json) {
    if (!HasCzurOwnedDeviceAuthorizationHint(request_json)) {
        return false;
    }
    const nlohmann::json& client_json = request_json["client"];
    const nlohmann::json& device_json = client_json["devices"][0];
    return device_json.value("svipplus", false) &&
           client_json.value("deviceEntitlementTier", std::string()) == "svip_plus";
}

editor::sdk::private_impl::ApiKeySpec BuildOnlineApiKeySpec() {
    editor::sdk::private_impl::ApiKeySpec spec;
    spec.account_type = editor::sdk::SdkAccountType::Svip;
    spec.license_mode = "online_api_key";
    spec.host_auth_mode = "activation_required";
    spec.commercial_authorized = true;
    editor::sdk::SdkDeviceGrant grant;
    grant.vid = 0x1234;
    grant.pid = 0x5678;
    spec.device_scope.push_back(grant);
    spec.issued_at = 1710000000;
    spec.expires_at = 1810000000;
    spec.key_id = "key_online_auth_smoke";
    spec.nonce = "online-auth-smoke";
    return spec;
}

int ExpectOk(int code, const std::string& message, const char* step) {
    if (code != editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok)) {
        std::cerr << step << " failed: code=" << code << " message=" << message << std::endl;
        return 1;
    }
    return 0;
}

struct ServerGuard {
    httplib::Server* server = NULL;
    std::thread* thread = NULL;

    ~ServerGuard() {
        if (server != NULL) {
            server->stop();
        }
        if (thread != NULL && thread->joinable()) {
            thread->join();
        }
    }
};

} // namespace

int main() {
    if (CheckHttpsTrustStoreResolver() != 0) {
        return 1;
    }
    if (CheckUtf8SafeJsonSerialization() != 0) {
        return 1;
    }

    if (editor::sdk::IsEntitlementAtLeast(editor::sdk::SdkAccountType::Trial, editor::sdk::SdkAccountType::Vip) ||
        !editor::sdk::IsEntitlementAtLeast(editor::sdk::SdkAccountType::SvipPlus, editor::sdk::SdkAccountType::Vip)) {
        std::cerr << "entitlement rank ordering is invalid" << std::endl;
        return 1;
    }
    editor::sdk::AuthContext trial_svip_context;
    trial_svip_context.account_type = editor::sdk::SdkAccountType::Trial;
    trial_svip_context.licensed_account_type = editor::sdk::SdkAccountType::Svip;
    trial_svip_context.entitlement_state = "online_trial";
    trial_svip_context.commercial_authorized = false;
    const editor::sdk::EntitlementCheckResult svip_trial_check = editor::sdk::CheckFeatureEntitlement(
        trial_svip_context, "image.enhance.svip", editor::sdk::SdkAccountType::Svip);
    if (svip_trial_check.code != editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok) ||
        !svip_trial_check.requires_trial_quota) {
        std::cerr << "svip commercial trial should be allowed through trial quota" << std::endl;
        return 1;
    }
    editor::sdk::AuthContext trial_svip_plus_context = trial_svip_context;
    trial_svip_plus_context.licensed_account_type = editor::sdk::SdkAccountType::SvipPlus;
    const editor::sdk::EntitlementCheckResult svip_plus_trial_check = editor::sdk::CheckFeatureEntitlement(
        trial_svip_plus_context, "image.enhance.svip_plus", editor::sdk::SdkAccountType::SvipPlus);
    if (svip_plus_trial_check.code != editor::sdk::ToCode(editor::sdk::SdkStatusCode::CapabilityNotAllowed) ||
        svip_plus_trial_check.requires_trial_quota) {
        std::cerr << "svip plus must require commercial authorization before trial quota" << std::endl;
        return 1;
    }

    SetEnvValue("CZUR_SDK_MASTER_SECRET", NULL);
    if (editor::sdk::private_impl::IsPrivateSdkMasterSecretEnvConfigured() ||
        editor::sdk::private_impl::GetPrivateSdkMasterSecretSource() != "default" ||
        editor::sdk::private_impl::GetPrivateSdkMasterSecret().empty()) {
        std::cerr << "default master secret state is invalid" << std::endl;
        return 1;
    }
    editor::sdk::private_impl::PrivateSdkAuthService default_issuing_service;
    std::string default_api_key;
    std::string default_err;
    editor::sdk::private_impl::ApiKeySpec default_spec = BuildOnlineApiKeySpec();
    default_spec.license_mode = "offline_api_key";
    default_spec.host_auth_mode = "none";
    if (!default_issuing_service.GenerateApiKey(default_spec, &default_api_key, &default_err)) {
        std::cerr << "generate default master secret api key failed: " << default_err << std::endl;
        return 1;
    }
    const editor::sdk::private_impl::ParsedApiKey default_validate_result =
        default_issuing_service.ParseApiKey(default_api_key, 1710000001);
    if (default_validate_result.code != editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok) ||
        default_validate_result.auth_context.license_mode != "offline_api_key") {
        std::cerr << "default master secret api key did not validate: code=" << default_validate_result.code
                  << " message=" << default_validate_result.message << std::endl;
        return 1;
    }

    SetEnvValue("CZUR_SDK_MASTER_SECRET", "czur-sdk-auth-service-smoke-secret-env");
    if (!editor::sdk::private_impl::IsPrivateSdkMasterSecretEnvConfigured() ||
        editor::sdk::private_impl::GetPrivateSdkMasterSecretSource() != "env" ||
        editor::sdk::private_impl::GetPrivateSdkMasterSecret() != "czur-sdk-auth-service-smoke-secret-env") {
        std::cerr << "env master secret state is invalid" << std::endl;
        return 1;
    }

    SetEnvValue("CZUR_SDK_MASTER_SECRET", "czur-sdk-auth-service-smoke-secret-a");
    SetEnvValue("CZUR_SDK_AUTHZ_BASE_URL", NULL);
    SetEnvValue("CZUR_SDK_AUTH_CLIENT_DEVICES_JSON",
                R"([{"deviceId":"device-metadata-smoke","model":"ET24 Pro","displayName":"CZUR ET24 Pro","vid":4660,"pid":22136,"status":"online","supportsVideo":true,"imageTransferProtocol":true}])");

    std::atomic<int> validate_calls(0);
    std::atomic<int> consume_calls(0);
    httplib::Server authz_server;
    authz_server.Post("/api/v1/capeditor/open/authz/validate",
                      [&](const httplib::Request& req, httplib::Response& res) {
                          ++validate_calls;
                          const nlohmann::json request_json = nlohmann::json::parse(req.body, NULL, false);
                          if (request_json.is_discarded() ||
                              request_json.value("apiKey", std::string()).empty() ||
                              request_json.value("capability", std::string()) != "image.enhance" ||
                              !HasExpectedValidateClientMetadata(request_json)) {
                              res.status = 400;
                              res.set_content(R"({"code":400,"message":"bad request","data":null})", "application/json");
                              return;
                          }
                          res.set_content(R"({"code":200,"message":"操作成功","data":{"allowed":true,"accountType":"capeditor_open","commercialAuthorized":true,"appId":2058127579597594626,"remainingQuota":100000,"reason":null},"timestamp":1781583293068})",
                                          "application/json");
                      });
    authz_server.Post("/api/v1/capeditor/open/authz/consume-confirm",
                      [&](const httplib::Request& req, httplib::Response& res) {
                          ++consume_calls;
                          const nlohmann::json request_json = nlohmann::json::parse(req.body, NULL, false);
                          if (request_json.is_discarded() ||
                              request_json.value("apiKey", std::string()).empty() ||
                              request_json.value("capability", std::string()) != "doc_crop_enhance" ||
                              request_json.value("idempotencyKey", std::string()) != "request_online_auth_smoke" ||
                              request_json.value("usageAmount", 0) != 1) {
                              res.status = 400;
                              res.set_content(R"({"code":400,"message":"bad request","data":null})", "application/json");
                              return;
                          }
                          res.set_content(R"({"code":200,"message":"ok","data":{"accepted":true,"duplicate":false,"remainingQuota":122,"reason":null}})",
                                          "application/json");
                      });
    const int authz_port = authz_server.bind_to_any_port("127.0.0.1");
    if (authz_port <= 0) {
        std::cerr << "failed to bind mock authz server" << std::endl;
        return 1;
    }
    std::thread authz_thread([&authz_server]() {
        authz_server.listen_after_bind();
    });
    ServerGuard authz_guard;
    authz_guard.server = &authz_server;
    authz_guard.thread = &authz_thread;
    for (int i = 0; i < 50 && !authz_server.is_running(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!authz_server.is_running()) {
        std::cerr << "mock authz server did not start" << std::endl;
        return 1;
    }

    editor::sdk::private_impl::PrivateSdkAuthService issuing_service;
    std::string api_key;
    std::string err;
    if (!issuing_service.GenerateApiKey(BuildOnlineApiKeySpec(), &api_key, &err)) {
        std::cerr << "generate online api key failed: " << err << std::endl;
        return 1;
    }

    editor::sdk::AuthValidateRequest create_request;
    create_request.token = api_key;
    create_request.authz_base_url = "http://127.0.0.1:" + std::to_string(authz_port);
    create_request.now_ts = 1710000001;
    const editor::sdk::AuthRefreshResult session_result = issuing_service.RefreshSession(create_request);
    if (ExpectOk(session_result.code, session_result.message, "create session") != 0) {
        return 1;
    }
    if (session_result.session_token.empty() || !session_result.auth_context.is_valid ||
        session_result.auth_context.license_mode != "online_api_key" ||
        !HasCapability(session_result.auth_context, "image.enhance") ||
        !HasCapability(session_result.auth_context, "device.close")) {
        std::cerr << "online api key session context is invalid" << std::endl;
        return 1;
    }

    editor::sdk::SessionValidateRequest validate_session_request;
    validate_session_request.session_token = session_result.session_token;
    validate_session_request.now_ts = 1710000002;
    const editor::sdk::SessionValidateResult validate_session_result =
        issuing_service.ValidateSession(validate_session_request);
    if (ExpectOk(validate_session_result.code, validate_session_result.message, "validate session") != 0) {
        return 1;
    }
    if (!validate_session_result.auth_context.is_valid ||
        validate_session_result.auth_context.license_mode != "online_api_key" ||
        !HasCapability(validate_session_result.auth_context, "image.enhance") ||
        !HasCapability(validate_session_result.auth_context, "device.close")) {
        std::cerr << "validated online api key session context is invalid" << std::endl;
        return 1;
    }

    editor::sdk::QuotaConsumeRequest quota_request;
    quota_request.token = api_key;
    quota_request.authz_base_url = create_request.authz_base_url;
    quota_request.capability = "doc_crop_enhance";
    quota_request.request_id = "request_online_auth_smoke";
    quota_request.units = 1;
    quota_request.now_ts = 1710000003;
    const editor::sdk::QuotaConsumeResult quota_result = issuing_service.ConsumeQuota(quota_request);
    if (ExpectOk(quota_result.code, quota_result.message, "consume quota") != 0) {
        return 1;
    }
    if (!quota_result.consumed || quota_result.bucket != "image" ||
        quota_result.auth_context.license_mode != "online_api_key" ||
        quota_result.auth_context.account_type != editor::sdk::SdkAccountType::Svip ||
        quota_result.auth_context.entitlement_state != "online_validated" ||
        !quota_result.auth_context.commercial_authorized) {
        std::cerr << "online api key quota result is invalid" << std::endl;
        return 1;
    }
    if (validate_calls.load() != 1 || consume_calls.load() != 1) {
        std::cerr << "unexpected mock authz call count: validate=" << validate_calls.load()
                  << " consume=" << consume_calls.load() << std::endl;
        return 1;
    }

    std::atomic<int> trial_validate_calls(0);
    httplib::Server trial_authz_server;
    trial_authz_server.Post("/api/v1/capeditor/open/authz/validate",
                            [&](const httplib::Request& req, httplib::Response& res) {
                                ++trial_validate_calls;
                                const nlohmann::json request_json = nlohmann::json::parse(req.body, NULL, false);
                                if (request_json.is_discarded() ||
                                    request_json.value("apiKey", std::string()).empty() ||
                                    request_json.value("capability", std::string()) != "image.enhance") {
                                    res.status = 400;
                                    res.set_content(R"({"code":400,"message":"bad request","data":null})", "application/json");
                                    return;
                                }
                                res.set_content(R"({"code":200,"message":"ok","data":{"allowed":true,"accountType":"svip_plus","commercialAuthorized":false,"remainingQuota":100000,"reason":null}})",
                                                "application/json");
                            });
    const int trial_authz_port = trial_authz_server.bind_to_any_port("127.0.0.1");
    if (trial_authz_port <= 0) {
        std::cerr << "failed to bind trial mock authz server" << std::endl;
        return 1;
    }
    std::thread trial_authz_thread([&trial_authz_server]() {
        trial_authz_server.listen_after_bind();
    });
    ServerGuard trial_authz_guard;
    trial_authz_guard.server = &trial_authz_server;
    trial_authz_guard.thread = &trial_authz_thread;
    for (int i = 0; i < 50 && !trial_authz_server.is_running(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!trial_authz_server.is_running()) {
        std::cerr << "trial mock authz server did not start" << std::endl;
        return 1;
    }

    editor::sdk::private_impl::ApiKeySpec trial_spec = BuildOnlineApiKeySpec();
    trial_spec.account_type = editor::sdk::SdkAccountType::SvipPlus;
    trial_spec.key_id = "key_online_commercial_trial_smoke";
    trial_spec.nonce = "online-commercial-trial-smoke";
    std::string trial_api_key;
    if (!issuing_service.GenerateApiKey(trial_spec, &trial_api_key, &err)) {
        std::cerr << "generate trial online api key failed: " << err << std::endl;
        return 1;
    }

    editor::sdk::AuthValidateRequest trial_create_request;
    trial_create_request.token = trial_api_key;
    trial_create_request.authz_base_url = "http://127.0.0.1:" + std::to_string(trial_authz_port);
    trial_create_request.now_ts = 1710000101;
    const editor::sdk::AuthRefreshResult trial_session_result = issuing_service.RefreshSession(trial_create_request);
    if (ExpectOk(trial_session_result.code, trial_session_result.message, "create commercial trial session") != 0) {
        return 1;
    }
    if (trial_session_result.auth_context.account_type != editor::sdk::SdkAccountType::Trial ||
        trial_session_result.auth_context.licensed_account_type != editor::sdk::SdkAccountType::SvipPlus ||
        trial_session_result.auth_context.entitlement_state != "online_trial" ||
        trial_session_result.auth_context.commercial_authorized ||
        !HasCapability(trial_session_result.auth_context, "recognition.barcode_detect") ||
        !HasCapability(trial_session_result.auth_context, "ocr.recognize")) {
        std::cerr << "commercial unauthorized online api key was not downgraded to trial" << std::endl;
        return 1;
    }
    const editor::sdk::AuthQuotaBucket* initial_trial_bucket =
        FindQuotaBucket(trial_session_result.auth_context, "trial_vip");
    if (initial_trial_bucket == NULL || initial_trial_bucket->limit != 200 || initial_trial_bucket->remaining != 200 ||
        initial_trial_bucket->enforcement != "local_trial_quota") {
        std::cerr << "commercial trial quota bucket is invalid" << std::endl;
        return 1;
    }

    editor::sdk::private_impl::PrivateSdkAuthService restored_trial_service;
    editor::sdk::QuotaConsumeRequest restored_trial_quota_request;
    restored_trial_quota_request.token = trial_api_key;
    restored_trial_quota_request.session_token = trial_session_result.session_token;
    restored_trial_quota_request.authz_base_url = trial_create_request.authz_base_url;
    restored_trial_quota_request.capability = "ocr.recognize";
    restored_trial_quota_request.request_id = "request_commercial_trial_restored_session";
    restored_trial_quota_request.units = 1;
    restored_trial_quota_request.now_ts = 1710000199;
    const editor::sdk::QuotaConsumeResult restored_trial_quota_result =
        restored_trial_service.ConsumeQuota(restored_trial_quota_request);
    if (ExpectOk(restored_trial_quota_result.code,
                 restored_trial_quota_result.message,
                 "consume commercial trial quota from restored session") != 0) {
        return 1;
    }
    if (restored_trial_quota_result.bucket != "trial_vip" || restored_trial_quota_result.remaining != 199 ||
        restored_trial_quota_result.auth_context.account_type != editor::sdk::SdkAccountType::Trial ||
        restored_trial_quota_result.auth_context.entitlement_state != "online_trial") {
        std::cerr << "restored commercial trial quota consume result is invalid" << std::endl;
        return 1;
    }

    editor::sdk::QuotaConsumeRequest drain_trial_quota_request;
    drain_trial_quota_request.token = trial_api_key;
    drain_trial_quota_request.authz_base_url = trial_create_request.authz_base_url;
    drain_trial_quota_request.capability = "doc_crop_enhance";
    drain_trial_quota_request.request_id = "request_commercial_trial_drain";
    drain_trial_quota_request.units = 199;
    drain_trial_quota_request.now_ts = 1710000200;
    const editor::sdk::QuotaConsumeResult drain_trial_quota_result =
        issuing_service.ConsumeQuota(drain_trial_quota_request);
    if (ExpectOk(drain_trial_quota_result.code,
                 drain_trial_quota_result.message,
                 "drain commercial trial quota") != 0) {
        return 1;
    }
    if (drain_trial_quota_result.bucket != "trial_vip" || drain_trial_quota_result.remaining != 0 ||
        drain_trial_quota_result.auth_context.account_type != editor::sdk::SdkAccountType::Trial) {
        std::cerr << "commercial trial quota drain result is invalid" << std::endl;
        return 1;
    }
    const editor::sdk::QuotaConsumeResult duplicate_drain_trial_quota_result =
        issuing_service.ConsumeQuota(drain_trial_quota_request);
    if (ExpectOk(duplicate_drain_trial_quota_result.code,
                 duplicate_drain_trial_quota_result.message,
                 "repeat commercial trial quota drain") != 0 ||
        duplicate_drain_trial_quota_result.remaining != 0) {
        std::cerr << "commercial trial quota duplicate consume result is invalid" << std::endl;
        return 1;
    }
    editor::sdk::QuotaConsumeRequest exhausted_trial_quota_request;
    exhausted_trial_quota_request.token = trial_api_key;
    exhausted_trial_quota_request.authz_base_url = trial_create_request.authz_base_url;
    exhausted_trial_quota_request.capability = "file.convert.vip";
    exhausted_trial_quota_request.request_id = "request_commercial_trial_exhausted";
    exhausted_trial_quota_request.units = 1;
    exhausted_trial_quota_request.now_ts = 1710000300;
    const editor::sdk::QuotaConsumeResult exhausted_trial_quota_result =
        issuing_service.ConsumeQuota(exhausted_trial_quota_request);
    if (exhausted_trial_quota_result.code != editor::sdk::ToCode(editor::sdk::SdkStatusCode::UsageLimitExceeded) ||
        exhausted_trial_quota_result.bucket != "trial_vip" || exhausted_trial_quota_result.remaining != 0) {
        std::cerr << "expected commercial trial quota to be exhausted, got code="
                  << exhausted_trial_quota_result.code << " bucket=" << exhausted_trial_quota_result.bucket
                  << " remaining=" << exhausted_trial_quota_result.remaining << std::endl;
        return 1;
    }
    if (trial_validate_calls.load() != 1) {
        std::cerr << "unexpected trial mock authz validate count: " << trial_validate_calls.load() << std::endl;
        return 1;
    }

    SetEnvValue("CZUR_SDK_AUTH_CLIENT_DEVICES_JSON",
                R"([{"deviceId":"czur-owned-device-smoke","model":"ET24 Pro","displayName":"CZUR ET24 Pro","vid":1276,"pid":25395,"status":"online","supportsVideo":true,"imageTransferProtocol":true}])");
    std::atomic<int> device_override_validate_calls(0);
    httplib::Server device_override_authz_server;
    device_override_authz_server.Post("/api/v1/capeditor/open/authz/validate",
                                      [&](const httplib::Request& req, httplib::Response& res) {
                                          ++device_override_validate_calls;
                                          const nlohmann::json request_json = nlohmann::json::parse(req.body, NULL, false);
                                          if (request_json.is_discarded() ||
                                              request_json.value("apiKey", std::string()).empty() ||
                                              request_json.value("capability", std::string()) != "image.enhance" ||
                                              !HasCzurOwnedDeviceAuthorizationHint(request_json)) {
                                              res.status = 400;
                                              res.set_content(R"({"code":400,"message":"bad request","data":null})", "application/json");
                                              return;
                                          }
                                          res.set_content(R"({"code":200,"message":"ok","data":{"allowed":true,"accountType":"svip","commercialAuthorized":false,"remainingQuota":100000,"reason":null}})",
                                                          "application/json");
                                      });
    const int device_override_authz_port = device_override_authz_server.bind_to_any_port("127.0.0.1");
    if (device_override_authz_port <= 0) {
        std::cerr << "failed to bind device override mock authz server" << std::endl;
        return 1;
    }
    std::thread device_override_authz_thread([&device_override_authz_server]() {
        device_override_authz_server.listen_after_bind();
    });
    ServerGuard device_override_authz_guard;
    device_override_authz_guard.server = &device_override_authz_server;
    device_override_authz_guard.thread = &device_override_authz_thread;
    for (int i = 0; i < 50 && !device_override_authz_server.is_running(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!device_override_authz_server.is_running()) {
        std::cerr << "device override mock authz server did not start" << std::endl;
        return 1;
    }

    editor::sdk::private_impl::ApiKeySpec device_override_spec = BuildOnlineApiKeySpec();
    device_override_spec.account_type = editor::sdk::SdkAccountType::Svip;
    device_override_spec.key_id = "key_online_czur_device_override_smoke";
    device_override_spec.nonce = "online-czur-device-override-smoke";
    std::string device_override_api_key;
    if (!issuing_service.GenerateApiKey(device_override_spec, &device_override_api_key, &err)) {
        std::cerr << "generate device override online api key failed: " << err << std::endl;
        return 1;
    }

    editor::sdk::AuthValidateRequest device_override_request;
    device_override_request.token = device_override_api_key;
    device_override_request.authz_base_url = "http://127.0.0.1:" + std::to_string(device_override_authz_port);
    device_override_request.now_ts = 1710000401;
    const editor::sdk::AuthRefreshResult device_override_session_result =
        issuing_service.RefreshSession(device_override_request);
    if (ExpectOk(device_override_session_result.code,
                 device_override_session_result.message,
                 "create czur device commercial override session") != 0) {
        return 1;
    }
    if (device_override_session_result.auth_context.account_type != editor::sdk::SdkAccountType::Svip ||
        device_override_session_result.auth_context.licensed_account_type != editor::sdk::SdkAccountType::Svip ||
        device_override_session_result.auth_context.entitlement_state != "online_validated" ||
        device_override_session_result.auth_context.commercial_authorized ||
        !HasCapability(device_override_session_result.auth_context, "ocr.recognize")) {
        std::cerr << "czur owned device did not locally provide formal svip entitlement" << std::endl;
        return 1;
    }
    if (FindQuotaBucket(device_override_session_result.auth_context, "trial_vip") != NULL) {
        std::cerr << "czur owned device entitlement should not expose trial_vip bucket" << std::endl;
        return 1;
    }
    if (device_override_validate_calls.load() != 1) {
        std::cerr << "unexpected device override mock authz validate count: "
                  << device_override_validate_calls.load() << std::endl;
        return 1;
    }


    SetEnvValue("CZUR_SDK_AUTH_CLIENT_DEVICES_JSON",
                R"([{"deviceId":"czur-svip-plus-device-smoke","model":"ET38","displayName":"CZUR ET38","vid":1276,"pid":25395,"status":"online","supportsVideo":true,"imageTransferProtocol":true,"commercialAuthorizationHint":true,"czurDevice":true,"thirdParty":false,"svipplus":true}])");
    std::atomic<int> svip_plus_device_validate_calls(0);
    httplib::Server svip_plus_device_authz_server;
    svip_plus_device_authz_server.Post("/api/v1/capeditor/open/authz/validate",
                                       [&](const httplib::Request& req, httplib::Response& res) {
                                           ++svip_plus_device_validate_calls;
                                           const nlohmann::json request_json = nlohmann::json::parse(req.body, NULL, false);
                                           if (request_json.is_discarded() ||
                                               request_json.value("apiKey", std::string()).empty() ||
                                               request_json.value("capability", std::string()) != "image.enhance" ||
                                               !HasSvipPlusDeviceAuthorizationHint(request_json)) {
                                               res.status = 400;
                                               res.set_content(R"({"code":400,"message":"bad request","data":null})", "application/json");
                                               return;
                                           }
                                           res.set_content(R"({"code":200,"message":"ok","data":{"allowed":true,"accountType":"svip_plus","commercialAuthorized":false,"remainingQuota":100000,"reason":null}})",
                                                           "application/json");
                                       });
    const int svip_plus_device_authz_port = svip_plus_device_authz_server.bind_to_any_port("127.0.0.1");
    if (svip_plus_device_authz_port <= 0) {
        std::cerr << "failed to bind svip plus device mock authz server" << std::endl;
        return 1;
    }
    std::thread svip_plus_device_authz_thread([&svip_plus_device_authz_server]() {
        svip_plus_device_authz_server.listen_after_bind();
    });
    ServerGuard svip_plus_device_authz_guard;
    svip_plus_device_authz_guard.server = &svip_plus_device_authz_server;
    svip_plus_device_authz_guard.thread = &svip_plus_device_authz_thread;
    for (int i = 0; i < 50 && !svip_plus_device_authz_server.is_running(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!svip_plus_device_authz_server.is_running()) {
        std::cerr << "svip plus device mock authz server did not start" << std::endl;
        return 1;
    }

    editor::sdk::private_impl::ApiKeySpec svip_plus_device_spec = BuildOnlineApiKeySpec();
    svip_plus_device_spec.account_type = editor::sdk::SdkAccountType::SvipPlus;
    svip_plus_device_spec.key_id = "key_online_czur_svip_plus_device_auto_authorize_smoke";
    svip_plus_device_spec.nonce = "online-czur-svip-plus-device-auto-authorize-smoke";
    std::string svip_plus_device_api_key;
    if (!issuing_service.GenerateApiKey(svip_plus_device_spec, &svip_plus_device_api_key, &err)) {
        std::cerr << "generate svip plus device online api key failed: " << err << std::endl;
        return 1;
    }

    editor::sdk::AuthValidateRequest svip_plus_device_request;
    svip_plus_device_request.token = svip_plus_device_api_key;
    svip_plus_device_request.authz_base_url = "http://127.0.0.1:" + std::to_string(svip_plus_device_authz_port);
    svip_plus_device_request.now_ts = 1710000451;
    const editor::sdk::AuthRefreshResult svip_plus_device_session_result =
        issuing_service.RefreshSession(svip_plus_device_request);
    if (ExpectOk(svip_plus_device_session_result.code,
                 svip_plus_device_session_result.message,
                 "create svip plus czur device automatic formal session") != 0) {
        return 1;
    }
    if (svip_plus_device_session_result.auth_context.account_type != editor::sdk::SdkAccountType::SvipPlus ||
        svip_plus_device_session_result.auth_context.licensed_account_type != editor::sdk::SdkAccountType::SvipPlus ||
        svip_plus_device_session_result.auth_context.entitlement_state != "online_validated" ||
        svip_plus_device_session_result.auth_context.commercial_authorized) {
        std::cerr << "svip plus czur device did not locally provide formal entitlement" << std::endl;
        return 1;
    }
    if (svip_plus_device_validate_calls.load() != 1) {
        std::cerr << "unexpected svip plus device mock authz validate count: "
                  << svip_plus_device_validate_calls.load() << std::endl;
        return 1;
    }

    SetEnvValue("CZUR_SDK_AUTH_CLIENT_DEVICES_JSON",
                R"([{"deviceId":"third-party-device-smoke","model":"3rdDevice","displayName":"Third Party Camera","vid":1276,"pid":0,"status":"online","supportsVideo":true,"imageTransferProtocol":true}])");
    std::atomic<int> third_party_validate_calls(0);
    httplib::Server third_party_authz_server;
    third_party_authz_server.Post("/api/v1/capeditor/open/authz/validate",
                                  [&](const httplib::Request& req, httplib::Response& res) {
                                      ++third_party_validate_calls;
                                      const nlohmann::json request_json = nlohmann::json::parse(req.body, NULL, false);
                                      if (request_json.is_discarded() ||
                                          request_json.value("apiKey", std::string()).empty() ||
                                          request_json.value("capability", std::string()) != "image.enhance" ||
                                          HasCzurOwnedDeviceAuthorizationHint(request_json)) {
                                          res.status = 400;
                                          res.set_content(R"({"code":400,"message":"bad request","data":null})", "application/json");
                                          return;
                                      }
                                      res.set_content(R"({"code":200,"message":"ok","data":{"allowed":true,"accountType":"svip_plus","commercialAuthorized":false,"remainingQuota":100000,"reason":null}})",
                                                      "application/json");
                                  });
    const int third_party_authz_port = third_party_authz_server.bind_to_any_port("127.0.0.1");
    if (third_party_authz_port <= 0) {
        std::cerr << "failed to bind third party mock authz server" << std::endl;
        return 1;
    }
    std::thread third_party_authz_thread([&third_party_authz_server]() {
        third_party_authz_server.listen_after_bind();
    });
    ServerGuard third_party_authz_guard;
    third_party_authz_guard.server = &third_party_authz_server;
    third_party_authz_guard.thread = &third_party_authz_thread;
    for (int i = 0; i < 50 && !third_party_authz_server.is_running(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!third_party_authz_server.is_running()) {
        std::cerr << "third party mock authz server did not start" << std::endl;
        return 1;
    }

    editor::sdk::private_impl::ApiKeySpec third_party_spec = BuildOnlineApiKeySpec();
    third_party_spec.account_type = editor::sdk::SdkAccountType::SvipPlus;
    third_party_spec.key_id = "key_online_third_party_no_override_smoke";
    third_party_spec.nonce = "online-third-party-no-override-smoke";
    std::string third_party_api_key;
    if (!issuing_service.GenerateApiKey(third_party_spec, &third_party_api_key, &err)) {
        std::cerr << "generate third party online api key failed: " << err << std::endl;
        return 1;
    }

    editor::sdk::AuthValidateRequest third_party_request;
    third_party_request.token = third_party_api_key;
    third_party_request.authz_base_url = "http://127.0.0.1:" + std::to_string(third_party_authz_port);
    third_party_request.now_ts = 1710000501;
    const editor::sdk::AuthRefreshResult third_party_session_result =
        issuing_service.RefreshSession(third_party_request);
    if (ExpectOk(third_party_session_result.code,
                 third_party_session_result.message,
                 "create third party commercial trial session") != 0) {
        return 1;
    }
    if (third_party_session_result.auth_context.account_type != editor::sdk::SdkAccountType::Trial ||
        third_party_session_result.auth_context.licensed_account_type != editor::sdk::SdkAccountType::SvipPlus ||
        third_party_session_result.auth_context.entitlement_state != "online_trial" ||
        third_party_session_result.auth_context.commercial_authorized) {
        std::cerr << "third party device should not locally override commercial authorization" << std::endl;
        return 1;
    }
    const editor::sdk::AuthQuotaBucket* third_party_trial_bucket =
        FindQuotaBucket(third_party_session_result.auth_context, "trial_vip");
    if (third_party_trial_bucket == NULL || third_party_trial_bucket->limit != 200 ||
        third_party_trial_bucket->remaining != 200) {
        std::cerr << "third party commercial trial quota bucket is invalid" << std::endl;
        return 1;
    }
    if (third_party_validate_calls.load() != 1) {
        std::cerr << "unexpected third party mock authz validate count: "
                  << third_party_validate_calls.load() << std::endl;
        return 1;
    }

    SetEnvValue("CZUR_SDK_MASTER_SECRET", "czur-sdk-auth-service-smoke-secret-b");
    editor::sdk::private_impl::PrivateSdkAuthService mismatched_service;
    const editor::sdk::AuthValidateResult mismatched_result = mismatched_service.ValidateApiKey(create_request);
    if (mismatched_result.code != editor::sdk::ToCode(editor::sdk::SdkStatusCode::TokenInvalid)) {
        std::cerr << "expected mismatched master secret to fail with TOKEN_INVALID, got code="
                  << mismatched_result.code << " message=" << mismatched_result.message << std::endl;
        return 1;
    }

    return 0;
}
