// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "authorization_service.h"

#include <ctime>
#include <utility>

#include "sdk_entitlement_policy.h"

#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
#include <windows.h>

#include "sdk_json_utils.h"
#endif

namespace editor {
namespace sdk {

namespace {

#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)

typedef const char* (*PrivateAuthJsonFn)(const char*);
typedef void (*PrivateAuthFreeStringFn)(const char*);

struct PrivateAuthCApi {
    HMODULE module = NULL;
    PrivateAuthJsonFn create_session = NULL;
    PrivateAuthJsonFn activate_offline = NULL;
    PrivateAuthJsonFn consume_quota = NULL;
    PrivateAuthFreeStringFn free_string = NULL;
};

PrivateAuthCApi& GetPrivateAuthCApi() {
    static PrivateAuthCApi api;
    static bool loaded = false;
    if (loaded) {
        return api;
    }
    loaded = true;
    api.module = ::LoadLibraryA("sdk_private_auth_c_api.dll");
    if (api.module == NULL) {
        return api;
    }
    api.create_session = reinterpret_cast<PrivateAuthJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_create_session_json"));
    api.activate_offline = reinterpret_cast<PrivateAuthJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_activate_offline_json"));
    api.consume_quota = reinterpret_cast<PrivateAuthJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_consume_quota_json"));
    api.free_string = reinterpret_cast<PrivateAuthFreeStringFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_free_string"));
    return api;
}

std::string StringField(const Json& object, const char* key) {
    if (!object.is_object()) {
        return "";
    }
    Json::const_iterator it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

int IntField(const Json& object, const char* key, int fallback = 0) {
    if (!object.is_object()) {
        return fallback;
    }
    Json::const_iterator it = object.find(key);
    return it != object.end() && it->is_number_integer() ? it->get<int>() : fallback;
}

std::int64_t Int64Field(const Json& object, const char* key, std::int64_t fallback = 0) {
    if (!object.is_object()) {
        return fallback;
    }
    Json::const_iterator it = object.find(key);
    return it != object.end() && it->is_number_integer() ? it->get<std::int64_t>() : fallback;
}

bool BoolField(const Json& object, const char* key, bool fallback = false) {
    if (!object.is_object()) {
        return fallback;
    }
    Json::const_iterator it = object.find(key);
    return it != object.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

void CopyAuthContextFields(const AuthContext& source, AuthContext* target) {
    if (target == NULL) {
        return;
    }
    target->is_valid = source.is_valid;
    target->account_type = source.account_type;
    target->account_type_code = source.account_type_code;
    target->licensed_account_type = source.licensed_account_type;
    target->licensed_account_type_code = source.licensed_account_type_code;
    target->auth_scene.assign(source.auth_scene.c_str(), source.auth_scene.size());
    target->license_mode.assign(source.license_mode.c_str(), source.license_mode.size());
    target->host_auth_mode.assign(source.host_auth_mode.c_str(), source.host_auth_mode.size());
    target->entitlement_state.assign(source.entitlement_state.c_str(), source.entitlement_state.size());
    target->commercial_authorized = source.commercial_authorized;
    target->machine_code.assign(source.machine_code.c_str(), source.machine_code.size());
    target->expires_at = source.expires_at;

    target->device_scope.clear();
    target->device_scope.reserve(source.device_scope.size());
    for (std::size_t i = 0; i < source.device_scope.size(); ++i) {
        SdkDeviceGrant grant;
        grant.vid = source.device_scope[i].vid;
        grant.pid = source.device_scope[i].pid;
        target->device_scope.push_back(grant);
    }

    target->capabilities.clear();
    target->capabilities.reserve(source.capabilities.size());
    for (std::size_t i = 0; i < source.capabilities.size(); ++i) {
        target->capabilities.push_back(std::string(source.capabilities[i].c_str(), source.capabilities[i].size()));
    }

    target->quota_buckets.clear();
    target->quota_buckets.reserve(source.quota_buckets.size());
    for (std::size_t i = 0; i < source.quota_buckets.size(); ++i) {
        AuthQuotaBucket bucket;
        bucket.bucket.assign(source.quota_buckets[i].bucket.c_str(), source.quota_buckets[i].bucket.size());
        bucket.limit = source.quota_buckets[i].limit;
        bucket.remaining = source.quota_buckets[i].remaining;
        bucket.enforcement.assign(source.quota_buckets[i].enforcement.c_str(),
                                  source.quota_buckets[i].enforcement.size());
        bucket.methods.reserve(source.quota_buckets[i].methods.size());
        for (std::size_t j = 0; j < source.quota_buckets[i].methods.size(); ++j) {
            bucket.methods.push_back(std::string(source.quota_buckets[i].methods[j].c_str(),
                                                source.quota_buckets[i].methods[j].size()));
        }
        target->quota_buckets.push_back(bucket);
    }
}

AuthorizationService::SessionResult CopySessionResultForStorage(const AuthorizationService::SessionResult& source) {
    AuthorizationService::SessionResult target;
    target.code = source.code;
    target.message.assign(source.message.c_str(), source.message.size());
    target.token.assign(source.token.c_str(), source.token.size());
    target.session_token.assign(source.session_token.c_str(), source.session_token.size());
    target.connection_id.assign(source.connection_id.c_str(), source.connection_id.size());
    target.expires_in = source.expires_in;
    target.created_at = source.created_at;
    target.last_seen_at = source.last_seen_at;
    CopyAuthContextFields(source.auth_context, &target.auth_context);
    return target;
}

AuthorizationService::SessionResult CopySessionResultForReturn(const AuthorizationService::SessionResult& source) {
    return CopySessionResultForStorage(source);
}

void RebuildLocalCapabilities(AuthContext* auth_context) {
    if (auth_context == NULL) {
        return;
    }
    auth_context->capabilities = BuildEntitledCapabilities(auth_context->account_type);
}

Json AuthContextDataToJson(const AuthContext& auth_context) {
    return Json{
        {"is_valid", auth_context.is_valid},
        {"account_type", ToAccountTypeString(auth_context.account_type)},
        {"account_type_code", auth_context.account_type_code},
        {"licensed_account_type", ToAccountTypeString(auth_context.licensed_account_type)},
        {"licensed_account_type_code", auth_context.licensed_account_type_code},
        {"auth_scene", "plugin"},
        {"license_mode", "offline_api_key"},
        {"host_auth_mode", "activation_required"},
        {"entitlement_state", "offline_trial"},
        {"commercial_authorized", auth_context.commercial_authorized},
        {"machine_code", ""},
        {"device_scope", Json::array()},
        {"expires_at", auth_context.expires_at},
        {"capability_count", auth_context.capabilities.size()},
        {"quota_bucket_count", auth_context.quota_buckets.size()},
    };
}

Json SessionContextDataToJson(const AuthorizationService::SessionResult& session) {
    return Json{
        {"session_token", session.session_token},
        {"expires_in", session.expires_in},
        {"auth_context", AuthContextDataToJson(session.auth_context)},
    };
}

AuthContext AuthContextFromJson(const Json& value) {
    AuthContext context;
    if (!value.is_object()) {
        return context;
    }

    context.is_valid = BoolField(value, "is_valid");
    context.account_type = AccountTypeFromString(StringField(value, "account_type"));
    context.account_type_code = IntField(value, "account_type_code", AccountTypeCode(context.account_type));
    context.licensed_account_type = AccountTypeFromString(StringField(value, "licensed_account_type"));
    context.licensed_account_type_code =
        IntField(value, "licensed_account_type_code", AccountTypeCode(context.licensed_account_type));
    context.auth_scene = StringField(value, "auth_scene");
    context.license_mode = StringField(value, "license_mode");
    context.host_auth_mode = StringField(value, "host_auth_mode");
    context.entitlement_state = StringField(value, "entitlement_state");
    context.commercial_authorized = BoolField(value, "commercial_authorized", true);
    context.machine_code = StringField(value, "machine_code");
    context.expires_at = Int64Field(value, "expires_at");

    Json::const_iterator device_scope_it = value.find("device_scope");
    if (device_scope_it != value.end() && device_scope_it->is_array()) {
        for (Json::const_iterator it = device_scope_it->begin(); it != device_scope_it->end(); ++it) {
            if (!it->is_object()) {
                continue;
            }
            SdkDeviceGrant grant;
            grant.vid = IntField(*it, "vid");
            grant.pid = IntField(*it, "pid");
            context.device_scope.push_back(grant);
        }
    }

    Json::const_iterator capabilities_it = value.find("capabilities");
    if (capabilities_it != value.end() && capabilities_it->is_array()) {
        for (Json::const_iterator it = capabilities_it->begin(); it != capabilities_it->end(); ++it) {
            if (it->is_string()) {
                const std::string capability = it->get<std::string>();
                context.capabilities.push_back(std::string(capability.c_str(), capability.size()));
            }
        }
    }

    Json::const_iterator buckets_it = value.find("quota_buckets");
    if (buckets_it != value.end() && buckets_it->is_array()) {
        try {
            const std::size_t bucket_count = buckets_it->size();
            const std::size_t capped_bucket_count = bucket_count > 64 ? 64 : bucket_count;
            for (std::size_t bucket_index = 0; bucket_index < capped_bucket_count; ++bucket_index) {
                const Json& bucket_json = (*buckets_it)[bucket_index];
                if (!bucket_json.is_object()) {
                    continue;
                }
                AuthQuotaBucket bucket;
                bucket.bucket = StringField(bucket_json, "bucket");
                bucket.limit = IntField(bucket_json, "limit");
                bucket.remaining = IntField(bucket_json, "remaining");
                bucket.enforcement = StringField(bucket_json, "enforcement");
                Json::const_iterator methods_it = bucket_json.find("methods");
                if (methods_it != bucket_json.end() && methods_it->is_array()) {
                    const std::size_t method_count = methods_it->size();
                    const std::size_t capped_method_count = method_count > 128 ? 128 : method_count;
                    for (std::size_t method_index = 0; method_index < capped_method_count; ++method_index) {
                        const Json& method_json = (*methods_it)[method_index];
                        if (method_json.is_string()) {
                            bucket.methods.push_back(method_json.get<std::string>());
                        }
                    }
                }
                context.quota_buckets.push_back(bucket);
            }
        } catch (const std::exception& e) {
            context.quota_buckets.clear();
        } catch (...) {
            context.quota_buckets.clear();
        }
    }

    return context;
}

AuthorizationService::SessionResult CreatePrivateSessionWithCApi(const std::string& connection_id,
                                                                 const std::string& token,
                                                                 const std::string& authz_base_url,
                                                                 std::int64_t now_ts) {
    AuthorizationService::SessionResult result;
    PrivateAuthCApi& api = GetPrivateAuthCApi();
    if (api.create_session == NULL || api.free_string == NULL) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "private auth c api not ready";
        return result;
    }

    const Json request = Json{{"token", token}, {"authz_base_url", authz_base_url}, {"now_ts", now_ts}};
    const char* response_ptr = api.create_session(request.dump().c_str());
    if (response_ptr == NULL) {
        result.code = ToCode(SdkStatusCode::InternalError);
        result.message = "private auth c api returned null";
        return result;
    }
    const std::string response_text(response_ptr);
    api.free_string(response_ptr);

    Json response;
    std::string parse_error;
    if (!TryParseJson(response_text, &response, &parse_error) || !response.is_object()) {
        result.code = ToCode(SdkStatusCode::InternalError);
        result.message = "private auth c api returned invalid json";
        return result;
    }

    result.code = IntField(response, "code");
    result.message = StringField(response, "message");
    if (result.message.empty()) {
        result.message = IsOkStatusCode(result.code) ? "ok" : "private auth failed";
    }
    result.token = token;
    result.session_token = StringField(response, "session_token");
    result.connection_id = connection_id;
    result.expires_in = IntField(response, "expires_in");
    result.created_at = now_ts;
    result.last_seen_at = now_ts;
    Json::const_iterator auth_context_it = response.find("auth_context");
    if (auth_context_it != response.end()) {
        result.auth_context = AuthContextFromJson(*auth_context_it);
    }
    return result;
}

bool InvokePrivateAuthCApi(PrivateAuthJsonFn fn,
                           const Json& request,
                           Json* response,
                           std::string* message) {
    PrivateAuthCApi& api = GetPrivateAuthCApi();
    if (fn == NULL || api.free_string == NULL) {
        if (message != NULL) {
            *message = "private auth c api not ready";
        }
        return false;
    }

    const char* response_ptr = fn(request.dump().c_str());
    if (response_ptr == NULL) {
        if (message != NULL) {
            *message = "private auth c api returned null";
        }
        return false;
    }
    const std::string response_text(response_ptr);
    api.free_string(response_ptr);

    std::string parse_error;
    if (!TryParseJson(response_text, response, &parse_error) || response == NULL || !response->is_object()) {
        if (message != NULL) {
            *message = "private auth c api returned invalid json";
        }
        return false;
    }
    return true;
}

AuthorizationService::SessionResult ConsumeQuotaWithCApi(const AuthorizationService::SessionResult& bound_session,
                                                         const std::string& authz_base_url,
                                                         const std::string& capability,
                                                         const std::string& request_id,
                                                         int units,
                                                         std::int64_t now_ts) {
    AuthorizationService::SessionResult result = bound_session;
    PrivateAuthCApi& api = GetPrivateAuthCApi();
    Json response;
    std::string error;
    const Json request = Json{{"token", bound_session.token},
                              {"session_token", bound_session.session_token},
                              {"authz_base_url", authz_base_url},
                              {"capability", capability},
                              {"request_id", request_id},
                              {"units", units},
                              {"now_ts", now_ts}};
    if (!InvokePrivateAuthCApi(api.consume_quota, request, &response, &error)) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = error;
        return result;
    }

    result.code = IntField(response, "code");
    result.message = StringField(response, "message");
    if (result.message.empty()) {
        result.message = IsOkStatusCode(result.code) ? "ok" : "private auth failed";
    }
    Json::const_iterator auth_context_it = response.find("auth_context");
    if (auth_context_it != response.end()) {
        result.auth_context = AuthContextFromJson(*auth_context_it);
    }
    result.last_seen_at = now_ts;
    return result;
}

AuthorizationService::SessionResult ActivateOfflineWithCApi(const AuthorizationService::SessionResult& bound_session,
                                                            const std::string& connection_id,
                                                            const std::string& auth_code,
                                                            std::int64_t now_ts) {
    AuthorizationService::SessionResult result;
    PrivateAuthCApi& api = GetPrivateAuthCApi();
    Json response;
    std::string error;
    const Json request = Json{{"token", bound_session.token},
                              {"session_token", bound_session.session_token},
                              {"auth_code", auth_code},
                              {"now_ts", now_ts}};
    if (!InvokePrivateAuthCApi(api.activate_offline, request, &response, &error)) {
        result = bound_session;
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = error;
        return result;
    }

    result.code = IntField(response, "code");
    result.message = StringField(response, "message");
    if (result.message.empty()) {
        result.message = IsOkStatusCode(result.code) ? "ok" : "private auth failed";
    }
    result.token = bound_session.token;
    result.session_token = StringField(response, "session_token");
    result.connection_id = connection_id;
    result.expires_in = IntField(response, "expires_in");
    result.created_at = bound_session.created_at;
    result.last_seen_at = now_ts;
    Json::const_iterator auth_context_it = response.find("auth_context");
    if (auth_context_it != response.end()) {
        result.auth_context = AuthContextFromJson(*auth_context_it);
    }
    return result;
}

#endif

} // namespace

AuthorizationService::AuthorizationService(const ProviderBundle& providers,
                                           AuthzBaseUrlSupplier authz_base_url_supplier)
    : providers_(providers),
      authz_base_url_supplier_(std::move(authz_base_url_supplier)) {}

AuthorizationService::SessionResult AuthorizationService::CreateSession(const std::string& connection_id,
                                                                        const std::string& token) {
    SessionResult result;
    const std::int64_t now_ts = static_cast<std::int64_t>(std::time(NULL));
#if !defined(_WIN32) || !defined(SDK_USE_PRIVATE_PROVIDER)
    if (!providers_.auth_provider) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "provider not ready";
        return result;
    }
#endif
    if (token.empty()) {
        result.code = ToCode(SdkStatusCode::AuthRequired);
        result.message = "token required";
        return result;
    }

    AuthValidateRequest request;
    request.token = token;
    request.authz_base_url = AuthzBaseUrl();
    request.now_ts = now_ts;
#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
    result = CreatePrivateSessionWithCApi(connection_id, token, request.authz_base_url, now_ts);
    if (IsOkStatusCode(result.code)) {
        RebuildLocalCapabilities(&result.auth_context);
        std::lock_guard<std::mutex> lock(sessions_mu_);
        sessions_[connection_id] = CopySessionResultForStorage(result);
    }
    return result;
#else
    const AuthRefreshResult provider_result = providers_.auth_provider->CreateSession(request);
    result.code = provider_result.code;
    result.message = provider_result.message;
    result.token = token;
    result.session_token = provider_result.session_token;
    result.connection_id = connection_id;
    result.expires_in = provider_result.expires_in;
    result.created_at = now_ts;
    result.last_seen_at = now_ts;
    result.auth_context = provider_result.auth_context;

    if (IsOkStatusCode(result.code)) {
        std::lock_guard<std::mutex> lock(sessions_mu_);
        sessions_[connection_id] = result;
    }
    return result;
#endif
}

AuthorizationService::SessionResult AuthorizationService::RefreshSession(const std::string& connection_id) {
    SessionResult bound_session = RequireSession(connection_id);
    if (!IsOkStatusCode(bound_session.code)) {
        return bound_session;
    }

    SessionResult result;
    const std::int64_t now_ts = static_cast<std::int64_t>(std::time(NULL));
    if ((bound_session.auth_context.license_mode == "online_api_key" ||
         bound_session.auth_context.license_mode == "offline_api_key") &&
        !bound_session.token.empty()) {
#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
        result = CreatePrivateSessionWithCApi(connection_id, bound_session.token, AuthzBaseUrl(), now_ts);
        result.created_at = bound_session.created_at;
        if (IsOkStatusCode(result.code)) {
            std::lock_guard<std::mutex> lock(sessions_mu_);
            sessions_[connection_id] = result;
        }
        return result;
#else
        AuthRefreshResult provider_result;
        AuthValidateRequest validate_request;
        validate_request.token = bound_session.token;
        validate_request.authz_base_url = AuthzBaseUrl();
        validate_request.now_ts = now_ts;
        provider_result = providers_.auth_provider->CreateSession(validate_request);
        result.code = provider_result.code;
        result.message = provider_result.message;
        result.token = bound_session.token;
        result.session_token = provider_result.session_token;
        result.connection_id = connection_id;
        result.expires_in = provider_result.expires_in;
        result.created_at = bound_session.created_at;
        result.last_seen_at = now_ts;
        result.auth_context = provider_result.auth_context;
#endif
    } else {
        AuthRefreshResult provider_result;
        AuthRefreshRequest request;
        request.session_token = bound_session.session_token;
        request.now_ts = now_ts;
        provider_result = providers_.auth_provider->RefreshSession(request);
        result.code = provider_result.code;
        result.message = provider_result.message;
        result.token = bound_session.token;
        result.session_token = provider_result.session_token;
        result.connection_id = connection_id;
        result.expires_in = provider_result.expires_in;
        result.created_at = bound_session.created_at;
        result.last_seen_at = now_ts;
        result.auth_context = provider_result.auth_context;
    }

    if (IsOkStatusCode(result.code)) {
        std::lock_guard<std::mutex> lock(sessions_mu_);
        sessions_[connection_id] = result;
    }
    return result;
}

AuthorizationService::SessionResult AuthorizationService::GetContext(const std::string& connection_id) const {
    return RequireSession(connection_id);
}

AuthorizationService::ContextDataResult AuthorizationService::GetContextData(const std::string& connection_id) const {
    ContextDataResult result;
    std::lock_guard<std::mutex> lock(sessions_mu_);
    const std::map<std::string, SessionResult>::const_iterator it = sessions_.find(connection_id);
    if (it == sessions_.end()) {
        result.code = ToCode(SdkStatusCode::AuthRequired);
        result.message = "session required";
        result.data = Json::object();
        return result;
    }
    result.data = SessionContextDataToJson(it->second);
    return result;
}

AuthorizationService::SessionResult AuthorizationService::RequireSession(const std::string& connection_id) const {
    SessionResult result;
    std::lock_guard<std::mutex> lock(sessions_mu_);
    const std::map<std::string, SessionResult>::const_iterator it = sessions_.find(connection_id);
    if (it == sessions_.end()) {
        result.code = ToCode(SdkStatusCode::AuthRequired);
        result.message = "session required";
        return result;
    }
    return CopySessionResultForReturn(it->second);
}

AuthorizationService::SessionResult AuthorizationService::RequireSessionToken(const std::string& session_token) const {
    SessionResult result;
    if (session_token.empty()) {
        result.code = ToCode(SdkStatusCode::AuthRequired);
        result.message = "session token required";
        return result;
    }
    std::lock_guard<std::mutex> lock(sessions_mu_);
    for (std::map<std::string, SessionResult>::const_iterator it = sessions_.begin();
         it != sessions_.end();
         ++it) {
        if (it->second.session_token == session_token) {
            result = CopySessionResultForReturn(it->second);
            if (result.connection_id.empty()) {
                result.connection_id = it->first;
            }
            return result;
        }
    }
    result.code = ToCode(SdkStatusCode::AuthRequired);
    result.message = "session token invalid";
    return result;
}

AuthorizationService::SessionResult AuthorizationService::RequireCapability(const std::string& connection_id,
                                                                            const std::string& capability) const {
    SessionResult result = RequireSession(connection_id);
    if (!IsOkStatusCode(result.code)) {
        return result;
    }
    if (!HasCapability(result.auth_context, capability)) {
        result.code = ToCode(SdkStatusCode::CapabilityNotAllowed);
        result.message = "capability not allowed";
    }
    return result;
}

AuthorizationService::SessionResult AuthorizationService::DestroySession(const std::string& connection_id) {
    SessionResult result;
    result.message = "ok";
    {
        std::lock_guard<std::mutex> lock(sessions_mu_);
        sessions_.erase(connection_id);
    }
    return result;
}

AuthorizationService::SessionResult AuthorizationService::ActivateOffline(const std::string& connection_id,
                                                                          const std::string& auth_code) {
    SessionResult bound_session = RequireSession(connection_id);
    if (!IsOkStatusCode(bound_session.code)) {
        return bound_session;
    }
#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
    const std::int64_t now_ts = static_cast<std::int64_t>(std::time(NULL));
    SessionResult result = ActivateOfflineWithCApi(bound_session, connection_id, auth_code, now_ts);
    if (IsOkStatusCode(result.code)) {
        RebuildLocalCapabilities(&result.auth_context);
        std::lock_guard<std::mutex> lock(sessions_mu_);
        sessions_[connection_id] = CopySessionResultForStorage(result);
    }
    return result;
#else
    if (!providers_.auth_provider) {
        bound_session.code = ToCode(SdkStatusCode::ProviderNotReady);
        bound_session.message = "provider not ready";
        return bound_session;
    }

    OfflineActivateRequest request;
    request.token = bound_session.token;
    request.session_token = bound_session.session_token;
    request.auth_code = auth_code;
    request.now_ts = static_cast<std::int64_t>(std::time(NULL));
    const OfflineActivateResult provider_result = providers_.auth_provider->ActivateOffline(request);

    SessionResult result;
    result.code = provider_result.code;
    result.message = provider_result.message;
    result.token = bound_session.token;
    result.session_token = provider_result.session_token;
    result.connection_id = connection_id;
    result.expires_in = provider_result.expires_in;
    result.created_at = bound_session.created_at;
    result.last_seen_at = request.now_ts;
    result.auth_context = provider_result.auth_context;
    if (IsOkStatusCode(result.code)) {
        std::lock_guard<std::mutex> lock(sessions_mu_);
        sessions_[connection_id] = result;
    }
    return result;
#endif
}

AuthorizationService::SessionResult AuthorizationService::ConsumeQuota(const std::string& connection_id,
                                                                       const std::string& capability,
                                                                       const std::string& request_id,
                                                                       int units) {
    SessionResult bound_session = RequireSession(connection_id);
    if (!IsOkStatusCode(bound_session.code)) {
        return bound_session;
    }
#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
    const std::int64_t now_ts = static_cast<std::int64_t>(std::time(NULL));
    SessionResult result = ConsumeQuotaWithCApi(bound_session, AuthzBaseUrl(), capability, request_id, units, now_ts);
    if (IsOkStatusCode(result.code)) {
        std::lock_guard<std::mutex> lock(sessions_mu_);
        std::map<std::string, SessionResult>::iterator it = sessions_.find(connection_id);
        if (it != sessions_.end()) {
            it->second.auth_context = result.auth_context;
            it->second.last_seen_at = now_ts;
            result = it->second;
            result.code = ToCode(SdkStatusCode::Ok);
            result.message = "ok";
        }
    }
    return result;
#else
    if (!providers_.auth_provider) {
        bound_session.code = ToCode(SdkStatusCode::ProviderNotReady);
        bound_session.message = "provider not ready";
        return bound_session;
    }

    QuotaConsumeRequest request;
    request.token = bound_session.token;
    request.session_token = bound_session.session_token;
    request.authz_base_url = AuthzBaseUrl();
    request.capability = capability;
    request.request_id = request_id;
    request.units = units;
    request.now_ts = static_cast<std::int64_t>(std::time(NULL));
    const QuotaConsumeResult provider_result = providers_.auth_provider->ConsumeQuota(request);

    SessionResult result = bound_session;
    result.code = provider_result.code;
    result.message = provider_result.message;
    result.auth_context = provider_result.auth_context;
    result.last_seen_at = request.now_ts;
    if (IsOkStatusCode(result.code)) {
        std::lock_guard<std::mutex> lock(sessions_mu_);
        std::map<std::string, SessionResult>::iterator it = sessions_.find(connection_id);
        if (it != sessions_.end()) {
            it->second.auth_context = provider_result.auth_context;
            it->second.last_seen_at = request.now_ts;
            result = it->second;
            result.code = provider_result.code;
            result.message = provider_result.message;
        }
    }
    return result;
#endif
}

void AuthorizationService::ClearConnection(const std::string& connection_id) {
    std::lock_guard<std::mutex> lock(sessions_mu_);
    sessions_.erase(connection_id);
}

std::size_t AuthorizationService::ActiveSessionCount() const {
    std::lock_guard<std::mutex> lock(sessions_mu_);
    return sessions_.size();
}

std::vector<AuthorizationService::SessionResult> AuthorizationService::SnapshotSessions() const {
    std::vector<SessionResult> sessions;
    std::lock_guard<std::mutex> lock(sessions_mu_);
    for (std::map<std::string, SessionResult>::const_iterator it = sessions_.begin();
         it != sessions_.end();
         ++it) {
        SessionResult snapshot = it->second;
        if (snapshot.connection_id.empty()) {
            snapshot.connection_id = it->first;
        }
        sessions.push_back(snapshot);
    }
    return sessions;
}

std::string AuthorizationService::AuthzBaseUrl() const {
    return authz_base_url_supplier_ ? authz_base_url_supplier_() : std::string();
}

bool AuthorizationService::HasCapability(const AuthContext& auth_context, const std::string& capability) const {
    for (std::vector<std::string>::const_iterator it = auth_context.capabilities.begin();
         it != auth_context.capabilities.end();
         ++it) {
        if (*it == capability) {
            return true;
        }
    }
    return false;
}

} // namespace sdk
} // namespace editor
