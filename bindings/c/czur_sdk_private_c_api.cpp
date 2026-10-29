// Copyright (c) 2026 CZUR Tech. All rights reserved.

#include "bindings/c/czur_sdk_private_c_api.h"

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "private_sdk_auth_service.h"
#include "sdk_status_code.h"

namespace {

using editor::sdk::AccountTypeCode;
using editor::sdk::AccountTypeFromString;
using editor::sdk::AuthValidateRequest;
using editor::sdk::AuthValidateResult;
using editor::sdk::AuthRefreshResult;
using editor::sdk::OfflineActivateRequest;
using editor::sdk::OfflineActivateResult;
using editor::sdk::SdkAccountType;
using editor::sdk::SdkDeviceGrant;
using editor::sdk::SdkStatusCode;
using editor::sdk::QuotaConsumeRequest;
using editor::sdk::QuotaConsumeResult;
using editor::sdk::ToAccountTypeString;
using editor::sdk::ToCode;
using editor::sdk::private_impl::ApiKeySpec;
using editor::sdk::private_impl::AuthCodeGenerateResult;
using editor::sdk::private_impl::ParsedApiKey;
using editor::sdk::private_impl::PrivateSdkAuthService;

const char kOfflineApiKeyMode[] = "offline_api_key";
const char kOnlineApiKeyMode[] = "online_api_key";
const char kHostAuthModeNone[] = "none";
const char kHostAuthModeActivationRequired[] = "activation_required";
const char kOfflineEntitlementTrial[] = "offline_trial";

const char* DuplicateString(const std::string& value) {
    char* output = static_cast<char*>(std::malloc(value.size() + 1));
    if (output == NULL) {
        return NULL;
    }
    std::memcpy(output, value.c_str(), value.size() + 1);
    return output;
}

const char* JsonResponse(const nlohmann::json& response) {
    return DuplicateString(response.dump());
}

nlohmann::json BuildResponse(int code, const std::string& message) {
    return nlohmann::json{
        {"code", code},
        {"message", message},
    };
}

const char* ErrorResponse(int code, const std::string& message) {
    return JsonResponse(BuildResponse(code, message));
}

bool ParseRequestJson(const char* request_json, nlohmann::json* request, const char** message) {
    if (request == NULL || message == NULL) {
        return false;
    }
    if (request_json == NULL || request_json[0] == '\0') {
        *message = "request json required";
        return false;
    }
    *request = nlohmann::json::parse(request_json, NULL, false);
    if (request->is_discarded() || !request->is_object()) {
        *message = "request json invalid";
        return false;
    }
    return true;
}

SdkAccountType ParseTier(const std::string& tier) {
    if (tier == "sr") {
        return SdkAccountType::Vip;
    }
    if (tier == "sq") {
        return SdkAccountType::Svip;
    }
    if (tier == "sp") {
        return SdkAccountType::SvipPlus;
    }
    return AccountTypeFromString(tier);
}

std::string NormalizeLicenseMode(const std::string& value) {
    if (value == "offline") {
        return kOfflineApiKeyMode;
    }
    if (value == "online") {
        return kOnlineApiKeyMode;
    }
    return value;
}

std::string NormalizeHostAuthMode(const std::string& value) {
    if (value == "activation-required") {
        return kHostAuthModeActivationRequired;
    }
    if (value.empty()) {
        return kHostAuthModeNone;
    }
    return value;
}

bool IsSupportedLicenseMode(const std::string& license_mode) {
    return license_mode == kOfflineApiKeyMode || license_mode == kOnlineApiKeyMode;
}

bool IsSupportedHostAuthMode(const std::string& host_auth_mode) {
    return host_auth_mode == kHostAuthModeNone || host_auth_mode == kHostAuthModeActivationRequired;
}

bool IsIssuableTier(SdkAccountType account_type) {
    return account_type == SdkAccountType::Vip || account_type == SdkAccountType::Svip ||
           account_type == SdkAccountType::SvipPlus;
}

bool ParseDeviceScope(const nlohmann::json& value, std::vector<SdkDeviceGrant>* device_scope) {
    if (device_scope == NULL || !value.is_array()) {
        return false;
    }
    device_scope->clear();
    for (nlohmann::json::const_iterator it = value.begin(); it != value.end(); ++it) {
        if (!it->is_object() || !it->contains("vid") || !(*it)["vid"].is_number_integer() ||
            !it->contains("pid") || !(*it)["pid"].is_number_integer()) {
            return false;
        }
        SdkDeviceGrant grant;
        grant.vid = (*it)["vid"].get<int>();
        grant.pid = (*it)["pid"].get<int>();
        if (grant.vid <= 0 || grant.pid <= 0) {
            return false;
        }
        device_scope->push_back(grant);
    }
    return true;
}

nlohmann::json DeviceScopeToJson(const std::vector<SdkDeviceGrant>& device_scope) {
    nlohmann::json output = nlohmann::json::array();
    try {
        const std::size_t count = device_scope.size();
        if (count > 256) {
            return output;
        }
        for (std::size_t i = 0; i < count; ++i) {
            output.push_back(nlohmann::json{
                {"vid", device_scope[i].vid},
                {"pid", device_scope[i].pid},
            });
        }
    } catch (const std::exception&) {
        return nlohmann::json::array();
    }
    return output;
}

bool BuildApiKeySpec(const nlohmann::json& request, ApiKeySpec* spec, std::string* message) {
    if (spec == NULL || message == NULL) {
        return false;
    }
    if (!request.contains("tier") || !request["tier"].is_string()) {
        *message = "tier required";
        return false;
    }
    spec->account_type = ParseTier(request["tier"].get<std::string>());
    if (!IsIssuableTier(spec->account_type)) {
        *message = "tier invalid";
        return false;
    }

    const std::string requested_license_mode =
        request.value("license_mode", std::string(kOfflineApiKeyMode));
    spec->license_mode = NormalizeLicenseMode(requested_license_mode);
    if (!IsSupportedLicenseMode(spec->license_mode)) {
        *message = "license_mode invalid";
        return false;
    }
    spec->host_auth_mode =
        NormalizeHostAuthMode(request.value("host_auth_mode", std::string(kHostAuthModeNone)));
    if (!IsSupportedHostAuthMode(spec->host_auth_mode)) {
        *message = "host_auth_mode invalid";
        return false;
    }

    if (!request.contains("device_scope") ||
        !ParseDeviceScope(request["device_scope"], &spec->device_scope)) {
        *message = "device_scope invalid";
        return false;
    }

    if (request.contains("issued_at") && !request["issued_at"].is_number_integer()) {
        *message = "issued_at invalid";
        return false;
    }
    if (request.contains("expires_at") && !request["expires_at"].is_number_integer()) {
        *message = "expires_at invalid";
        return false;
    }
    if (request.contains("key_id") && !request["key_id"].is_string()) {
        *message = "key_id invalid";
        return false;
    }
    if (request.contains("nonce") && !request["nonce"].is_string()) {
        *message = "nonce invalid";
        return false;
    }
    if (request.contains("commercial_authorized") && !request["commercial_authorized"].is_boolean()) {
        *message = "commercial_authorized invalid";
        return false;
    }
    if (request.contains("commercialAuthorized") && !request["commercialAuthorized"].is_boolean()) {
        *message = "commercialAuthorized invalid";
        return false;
    }

    spec->issued_at = request.value("issued_at", static_cast<std::int64_t>(0));
    spec->expires_at = request.value("expires_at", static_cast<std::int64_t>(0));
    spec->key_id = request.value("key_id", std::string());
    spec->nonce = request.value("nonce", std::string());
    spec->commercial_authorized = request.value("commercial_authorized",
                                                request.value("commercialAuthorized", false));
    return true;
}

std::int64_t RequestNowTs(const nlohmann::json& request) {
    if (request.contains("now_ts") && request["now_ts"].is_number_integer()) {
        return request["now_ts"].get<std::int64_t>();
    }
    return static_cast<std::int64_t>(std::time(NULL));
}

void AddAuthContextToResponse(nlohmann::json* response, const editor::sdk::AuthContext& auth_context) {
    if (response == NULL) {
        return;
    }
    (*response)["tier"] = ToAccountTypeString(auth_context.account_type);
    (*response)["tier_code"] = auth_context.account_type_code;
    (*response)["licensed_account_type"] = ToAccountTypeString(auth_context.licensed_account_type);
    (*response)["licensed_account_type_code"] = auth_context.licensed_account_type_code;
    (*response)["license_mode"] = auth_context.license_mode;
    (*response)["host_auth_mode"] = auth_context.host_auth_mode.empty() ? kHostAuthModeNone : auth_context.host_auth_mode;
    (*response)["entitlement_state"] = auth_context.entitlement_state;
    (*response)["commercial_authorized"] = auth_context.commercial_authorized;
    (*response)["expires_at"] = auth_context.expires_at;
    (*response)["device_scope"] = DeviceScopeToJson(auth_context.device_scope);
}

nlohmann::json AuthContextToJson(const editor::sdk::AuthContext& auth_context) {
    nlohmann::json capabilities = nlohmann::json::array();
    for (std::vector<std::string>::const_iterator it = auth_context.capabilities.begin();
         it != auth_context.capabilities.end();
         ++it) {
        capabilities.push_back(*it);
    }

    nlohmann::json quota_buckets = nlohmann::json::array();
    for (std::vector<editor::sdk::AuthQuotaBucket>::const_iterator it = auth_context.quota_buckets.begin();
         it != auth_context.quota_buckets.end();
         ++it) {
        nlohmann::json methods = nlohmann::json::array();
        for (std::vector<std::string>::const_iterator method_it = it->methods.begin();
             method_it != it->methods.end();
             ++method_it) {
            methods.push_back(*method_it);
        }
        quota_buckets.push_back(nlohmann::json{
            {"bucket", it->bucket},
            {"methods", methods},
            {"limit", it->limit},
            {"remaining", it->remaining},
            {"enforcement", it->enforcement},
        });
    }

    return nlohmann::json{
        {"is_valid", auth_context.is_valid},
        {"account_type", ToAccountTypeString(auth_context.account_type)},
        {"account_type_code", auth_context.account_type_code},
        {"licensed_account_type", ToAccountTypeString(auth_context.licensed_account_type)},
        {"licensed_account_type_code", auth_context.licensed_account_type_code},
        {"auth_scene", auth_context.auth_scene},
        {"license_mode", auth_context.license_mode},
        {"host_auth_mode", auth_context.host_auth_mode},
        {"entitlement_state", auth_context.entitlement_state},
        {"commercial_authorized", auth_context.commercial_authorized},
        {"machine_code", auth_context.machine_code},
        {"device_scope", DeviceScopeToJson(auth_context.device_scope)},
        {"expires_at", auth_context.expires_at},
        {"capabilities", capabilities},
        {"quota_buckets", quota_buckets},
    };
}

editor::sdk::AuthContext BuildGenerateResponseContext(const ParsedApiKey& parsed) {
    editor::sdk::AuthContext auth_context;
    auth_context.is_valid = parsed.auth_context.is_valid;
    auth_context.account_type = parsed.auth_context.account_type;
    auth_context.account_type_code = parsed.auth_context.account_type_code;
    auth_context.licensed_account_type = parsed.auth_context.licensed_account_type;
    auth_context.licensed_account_type_code = parsed.auth_context.licensed_account_type_code;
    auth_context.auth_scene = parsed.auth_context.auth_scene;
    auth_context.license_mode = parsed.auth_context.license_mode;
    auth_context.host_auth_mode = parsed.auth_context.host_auth_mode;
    auth_context.entitlement_state = parsed.auth_context.entitlement_state;
    auth_context.commercial_authorized = parsed.auth_context.commercial_authorized;
    auth_context.machine_code = parsed.auth_context.machine_code;
    auth_context.expires_at = parsed.auth_context.expires_at;
    auth_context.capabilities = parsed.auth_context.capabilities;
    auth_context.quota_buckets = parsed.auth_context.quota_buckets;
    auth_context.device_scope.clear();
    try {
        const std::size_t device_scope_count = parsed.auth_context.device_scope.size();
        if (device_scope_count <= 256) {
            for (std::size_t i = 0; i < device_scope_count; ++i) {
                auth_context.device_scope.push_back(parsed.auth_context.device_scope[i]);
            }
        }
    } catch (const std::exception&) {
        auth_context.device_scope.clear();
    }
    if (auth_context.host_auth_mode == kHostAuthModeActivationRequired &&
        auth_context.license_mode == kOfflineApiKeyMode) {
        auth_context.account_type = SdkAccountType::Trial;
        auth_context.account_type_code = AccountTypeCode(SdkAccountType::Trial);
        auth_context.entitlement_state = kOfflineEntitlementTrial;
    }
    return auth_context;
}

} // namespace

extern "C" {

const char* czur_sdk_private_generate_api_key_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseRequestJson(request_json, &request, &parse_message)) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }

        ApiKeySpec spec;
        std::string validation_message;
        if (!BuildApiKeySpec(request, &spec, &validation_message)) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidParams), validation_message);
        }

        PrivateSdkAuthService auth_service;
        std::string api_key;
        std::string error;
        if (!auth_service.GenerateApiKey(spec, &api_key, &error)) {
            return ErrorResponse(ToCode(SdkStatusCode::InternalError),
                                 error.empty() ? "generate api key failed" : error);
        }

        ParsedApiKey parsed;
        auth_service.ParseApiKey(api_key, spec.issued_at > 0 ? spec.issued_at : 0, &parsed);
        nlohmann::json response = BuildResponse(parsed.code, parsed.message);
        response["api_key"] = api_key;
        response["key_id"] = parsed.key_id;
        response["issued_at"] = parsed.issued_at;
        AddAuthContextToResponse(&response, BuildGenerateResponseContext(parsed));
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

const char* czur_sdk_private_parse_api_key_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseRequestJson(request_json, &request, &parse_message)) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }
        if (!request.contains("api_key") || !request["api_key"].is_string()) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidParams), "api_key required");
        }
        if (request.contains("now_ts") && !request["now_ts"].is_number_integer()) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidParams), "now_ts invalid");
        }

        PrivateSdkAuthService auth_service;
        ParsedApiKey parsed;
        auth_service.ParseApiKey(request["api_key"].get<std::string>(), RequestNowTs(request), &parsed);
        AuthValidateRequest validate_request;
        validate_request.token = request["api_key"].get<std::string>();
        validate_request.now_ts = RequestNowTs(request);
        AuthValidateResult validate_result;
        if (parsed.code == 0) {
            auth_service.ValidateApiKey(validate_request, &validate_result);
        }
        nlohmann::json response = BuildResponse(parsed.code, parsed.message);
        response["is_valid"] = parsed.auth_context.is_valid;
        response["key_id"] = parsed.key_id;
        response["nonce"] = parsed.nonce;
        response["token_digest"] = parsed.token_digest;
        response["issued_at"] = parsed.issued_at;
        if (parsed.code == 0 && validate_result.code == 0) {
            AddAuthContextToResponse(&response, validate_result.auth_context);
        } else {
            AddAuthContextToResponse(&response, parsed.auth_context);
        }
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

const char* czur_sdk_private_generate_auth_code_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseRequestJson(request_json, &request, &parse_message)) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }
        if (!request.contains("machine_code") || !request["machine_code"].is_string()) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidParams), "machine_code required");
        }
        if (!request.contains("expires_date") || !request["expires_date"].is_string()) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidParams), "expires_date required");
        }
        if (!request.contains("auth_type") || !request["auth_type"].is_number_integer()) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidParams), "auth_type required");
        }

        const std::string machine_code = request["machine_code"].get<std::string>();
        const std::string expires_date = request["expires_date"].get<std::string>();
        const int auth_type = request["auth_type"].get<int>();

        PrivateSdkAuthService auth_service;
        const AuthCodeGenerateResult result = auth_service.GenerateAuthCode(machine_code, expires_date, auth_type);
        if (result.code != 0) {
            return ErrorResponse(result.code, result.message);
        }

        nlohmann::json response = BuildResponse(result.code, result.message);
        response["auth_code"] = result.auth_code;
        response["machine_code"] = machine_code;
        response["expires_date"] = expires_date;
        response["auth_type"] = auth_type;
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

const char* czur_sdk_private_create_session_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseRequestJson(request_json, &request, &parse_message)) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }
        if (!request.contains("token") || !request["token"].is_string()) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidParams), "token required");
        }
        if (request.contains("now_ts") && !request["now_ts"].is_number_integer()) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidParams), "now_ts invalid");
        }
        if (request.contains("authz_base_url") && !request["authz_base_url"].is_string()) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidParams), "authz_base_url invalid");
        }

        AuthValidateRequest auth_request;
        auth_request.token = request["token"].get<std::string>();
        auth_request.authz_base_url = request.value("authz_base_url", std::string());
        auth_request.now_ts = RequestNowTs(request);

        PrivateSdkAuthService auth_service;
        AuthRefreshResult result;
        auth_service.RefreshSession(auth_request, &result);
        nlohmann::json response = BuildResponse(result.code, result.message);
        response["session_token"] = result.session_token;
        response["expires_in"] = result.expires_in;
        response["auth_context"] = AuthContextToJson(result.auth_context);
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

const char* czur_sdk_private_activate_offline_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseRequestJson(request_json, &request, &parse_message)) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }
        if (!request.contains("token") || !request["token"].is_string()) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidParams), "token required");
        }
        if (request.contains("session_token") && !request["session_token"].is_string()) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidParams), "session_token invalid");
        }
        if (!request.contains("auth_code") || !request["auth_code"].is_string()) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidParams), "auth_code required");
        }
        if (request.contains("now_ts") && !request["now_ts"].is_number_integer()) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidParams), "now_ts invalid");
        }

        OfflineActivateRequest activate_request;
        activate_request.token = request["token"].get<std::string>();
        activate_request.session_token = request.value("session_token", std::string());
        activate_request.auth_code = request["auth_code"].get<std::string>();
        activate_request.now_ts = RequestNowTs(request);

        PrivateSdkAuthService auth_service;
        const OfflineActivateResult result = auth_service.ActivateOffline(activate_request);
        nlohmann::json response = BuildResponse(result.code, result.message);
        response["activated"] = result.activated;
        response["session_token"] = result.session_token;
        response["expires_in"] = result.expires_in;
        response["auth_context"] = AuthContextToJson(result.auth_context);
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

const char* czur_sdk_private_consume_quota_json(const char* request_json) {
    try {
        nlohmann::json request;
        const char* parse_message = "";
        if (!ParseRequestJson(request_json, &request, &parse_message)) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidRequest), parse_message);
        }
        if (!request.contains("token") || !request["token"].is_string()) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidParams), "token required");
        }
        if (!request.contains("session_token") || !request["session_token"].is_string()) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidParams), "session_token required");
        }
        if (!request.contains("capability") || !request["capability"].is_string()) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidParams), "capability required");
        }
        if (!request.contains("request_id") || !request["request_id"].is_string()) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidParams), "request_id required");
        }
        if (request.contains("units") && !request["units"].is_number_integer()) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidParams), "units invalid");
        }
        if (request.contains("now_ts") && !request["now_ts"].is_number_integer()) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidParams), "now_ts invalid");
        }
        if (request.contains("authz_base_url") && !request["authz_base_url"].is_string()) {
            return ErrorResponse(ToCode(SdkStatusCode::InvalidParams), "authz_base_url invalid");
        }

        QuotaConsumeRequest quota_request;
        quota_request.token = request["token"].get<std::string>();
        quota_request.session_token = request["session_token"].get<std::string>();
        quota_request.authz_base_url = request.value("authz_base_url", std::string());
        quota_request.capability = request["capability"].get<std::string>();
        quota_request.request_id = request["request_id"].get<std::string>();
        quota_request.units = request.value("units", 1);
        quota_request.now_ts = RequestNowTs(request);

        PrivateSdkAuthService auth_service;
        const QuotaConsumeResult result = auth_service.ConsumeQuota(quota_request);
        nlohmann::json response = BuildResponse(result.code, result.message);
        response["consumed"] = result.consumed;
        response["bucket"] = result.bucket;
        response["limit"] = result.limit;
        response["remaining"] = result.remaining;
        response["auth_context"] = AuthContextToJson(result.auth_context);
        return JsonResponse(response);
    } catch (const std::exception& e) {
        return ErrorResponse(ToCode(SdkStatusCode::InternalError), e.what());
    } catch (...) {
        return ErrorResponse(ToCode(SdkStatusCode::InternalError), "unknown native error");
    }
}

const char* czur_sdk_private_version_json(void) {
    nlohmann::json response = BuildResponse(ToCode(SdkStatusCode::Ok), "ok");
    response["name"] = "czur-sdk-private-c-api";
    response["abi_version"] = 3;
    response["api_key_version"] = "v1";
    return JsonResponse(response);
}

void czur_sdk_private_free_string(const char* value) {
    std::free(const_cast<char*>(value));
}

} // extern "C"
