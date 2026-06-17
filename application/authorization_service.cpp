// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "authorization_service.h"

#include <ctime>
#include <utility>

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
                context.capabilities.push_back(it->get<std::string>());
            }
        }
    }

    Json::const_iterator buckets_it = value.find("quota_buckets");
    if (buckets_it != value.end() && buckets_it->is_array()) {
        for (Json::const_iterator it = buckets_it->begin(); it != buckets_it->end(); ++it) {
            if (!it->is_object()) {
                continue;
            }
            AuthQuotaBucket bucket;
            bucket.bucket = StringField(*it, "bucket");
            bucket.limit = IntField(*it, "limit");
            bucket.remaining = IntField(*it, "remaining");
            bucket.enforcement = StringField(*it, "enforcement");
            Json::const_iterator methods_it = it->find("methods");
            if (methods_it != it->end() && methods_it->is_array()) {
                for (Json::const_iterator method_it = methods_it->begin(); method_it != methods_it->end(); ++method_it) {
                    if (method_it->is_string()) {
                        bucket.methods.push_back(method_it->get<std::string>());
                    }
                }
            }
            context.quota_buckets.push_back(bucket);
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
        std::lock_guard<std::mutex> lock(sessions_mu_);
        sessions_[connection_id] = result;
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

AuthorizationService::SessionResult AuthorizationService::RequireSession(const std::string& connection_id) const {
    SessionResult result;
    std::lock_guard<std::mutex> lock(sessions_mu_);
    const std::map<std::string, SessionResult>::const_iterator it = sessions_.find(connection_id);
    if (it == sessions_.end()) {
        result.code = ToCode(SdkStatusCode::AuthRequired);
        result.message = "session required";
        return result;
    }
    return it->second;
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
            result = it->second;
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
