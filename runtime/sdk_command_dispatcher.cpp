// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_command_dispatcher.h"

#include <ctime>
#include <utility>

namespace editor {
namespace sdk {

namespace {

std::string GetOptionalStringField(const Json& obj, const char* key) {
    const auto it = obj.find(key);
    if (it != obj.end() && it->is_string()) {
        return it->get<std::string>();
    }
    return "";
}

Json GetOptionalObjectField(const Json& obj, const char* key) {
    const auto it = obj.find(key);
    if (it != obj.end() && it->is_object()) {
        return *it;
    }
    return Json::object();
}

std::string GetSessionCredential(const Json& auth) {
    const std::string session_key = GetOptionalStringField(auth, "session_key");
    if (!session_key.empty()) {
        return session_key;
    }
    return GetOptionalStringField(auth, "session_token");
}

const char* ToAuthScopeString(SdkAuthScope auth_scope) {
    switch (auth_scope) {
        case SdkAuthScope::Authenticated:
            return "authenticated";
        case SdkAuthScope::Entitled:
            return "entitled";
        case SdkAuthScope::Anonymous:
        default:
            return "anonymous";
    }
}

const char* ToDeviceScopeString(SdkDeviceScopePolicy device_scope) {
    switch (device_scope) {
        case SdkDeviceScopePolicy::TargetDevice:
            return "target_device";
        case SdkDeviceScopePolicy::AllBoundDevices:
            return "all_bound_devices";
        case SdkDeviceScopePolicy::None:
        default:
            return "none";
    }
}

} // namespace

SdkCommandDispatcher::SdkCommandDispatcher(const SdkConfig& config, const ProviderBundle& providers)
    : config_(config),
      providers_(providers) {
    method_descriptors_.push_back({"system.ping", SdkAuthScope::Anonymous, SdkDeviceScopePolicy::None, "ga",
                                   "SDK 存活探测", {"ping"}});
    method_descriptors_.push_back({"system.info", SdkAuthScope::Anonymous, SdkDeviceScopePolicy::None, "ga",
                                   "返回运行状态、端口和 provider 信息", {"getStatus"}});
    method_descriptors_.push_back({"system.capabilities", SdkAuthScope::Anonymous, SdkDeviceScopePolicy::None, "ga",
                                   "返回当前开放 method 清单", {"listCapabilities"}});
    method_descriptors_.push_back({"auth.validate", SdkAuthScope::Authenticated, SdkDeviceScopePolicy::None, "ga",
                                   "校验 ApiKey 并返回授权上下文", {}});
    method_descriptors_.push_back({"auth.refresh", SdkAuthScope::Authenticated, SdkDeviceScopePolicy::None, "beta",
                                   "校验 ApiKey 并签发短期 session key", {}});
    method_descriptors_.push_back({"auth.get_context", SdkAuthScope::Authenticated, SdkDeviceScopePolicy::None, "beta",
                                   "使用 session key 或 ApiKey 查询授权上下文", {}});
}

void SdkCommandDispatcher::SetStatusSupplier(StatusSupplier supplier) {
    status_supplier_ = std::move(supplier);
}

Json SdkCommandDispatcher::Dispatch(const Json& request_json) {
    if (!request_json.is_object()) {
        return BuildWsResponse("", SdkStatusCode::InvalidRequest, "invalid request");
    }

    SdkCommandRequest request;
    request.request_id = GetOptionalStringField(request_json, "request_id");
    if (request.request_id.empty()) {
        request.request_id = GetOptionalStringField(request_json, "id");
    }
    request.original_method = GetOptionalStringField(request_json, "method");
    request.method = ResolveMethod(request.original_method);
    request.params = GetOptionalObjectField(request_json, "params");
    request.auth = GetOptionalObjectField(request_json, "auth");
    request.client = GetOptionalObjectField(request_json, "client");

    if (request.original_method.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidMethod, "missing method");
    }
    if (request.method.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::UnsupportedMethod, "unsupported method");
    }

    if (request.method == "system.ping") {
        return HandleSystemPing(request);
    }
    if (request.method == "system.info") {
        return HandleSystemInfo(request);
    }
    if (request.method == "system.capabilities") {
        return HandleSystemCapabilities(request);
    }
    if (request.method == "auth.validate") {
        return HandleAuthValidate(request);
    }
    if (request.method == "auth.refresh") {
        return HandleAuthRefresh(request);
    }
    if (request.method == "auth.get_context") {
        return HandleAuthGetContext(request);
    }

    return BuildWsResponse(request.request_id, SdkStatusCode::UnsupportedMethod, "unsupported method");
}

Json SdkCommandDispatcher::BuildCapabilitiesJson() const {
    Json methods = Json::array();
    for (const SdkMethodDescriptor& descriptor : method_descriptors_) {
        methods.push_back(BuildMethodDescriptorJson(descriptor));
    }

    Json modules = Json::array({"system", "auth"});
    if (providers_.device_provider) {
        modules.push_back("device");
    }
    if (providers_.graphic_provider) {
        modules.push_back("graphic");
    }
    if (providers_.ocr_provider) {
        modules.push_back("ocr");
    }
    if (providers_.ofd_provider) {
        modules.push_back("ofd");
    }

    return Json{
        {"modules", std::move(modules)},
        {"methods", std::move(methods)},
        {"videoControlTypes", Json::array({"start", "stop", "setFormat"})},
    };
}

const SdkMethodDescriptor* SdkCommandDispatcher::FindMethodDescriptor(const std::string& method) const {
    for (const SdkMethodDescriptor& descriptor : method_descriptors_) {
        if (descriptor.method == method) {
            return &descriptor;
        }
    }
    return nullptr;
}

std::string SdkCommandDispatcher::ResolveMethod(const std::string& method) const {
    if (FindMethodDescriptor(method) != nullptr) {
        return method;
    }
    for (const SdkMethodDescriptor& descriptor : method_descriptors_) {
        for (const std::string& alias : descriptor.legacy_aliases) {
            if (alias == method) {
                return descriptor.method;
            }
        }
    }
    return "";
}

Json SdkCommandDispatcher::HandleSystemPing(const SdkCommandRequest& request) const {
    return BuildWsResponse(request.request_id, SdkStatusCode::Ok, "ok", Json{{"pong", true}});
}

Json SdkCommandDispatcher::HandleSystemInfo(const SdkCommandRequest& request) const {
    const Json data = status_supplier_ ? status_supplier_() : Json::object();
    return BuildWsResponse(request.request_id, SdkStatusCode::Ok, "ok", data);
}

Json SdkCommandDispatcher::HandleSystemCapabilities(const SdkCommandRequest& request) const {
    return BuildWsResponse(request.request_id, SdkStatusCode::Ok, "ok", BuildCapabilitiesJson());
}

Json SdkCommandDispatcher::HandleAuthValidate(const SdkCommandRequest& request) const {
    if (!providers_.auth_provider) {
        return BuildWsResponse(request.request_id, SdkStatusCode::ProviderNotReady, "provider not ready");
    }

    const std::string api_key = GetOptionalStringField(request.auth, "api_key");
    if (api_key.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::AuthRequired, "auth required");
    }

    const AuthValidateResult result = providers_.auth_provider->ValidateApiKey({api_key, NowTs()});
    if (!IsOkStatusCode(result.code)) {
        return BuildWsResponse(request.request_id, result.code, result.message);
    }
    return BuildWsResponse(
        request.request_id, SdkStatusCode::Ok, "ok", Json{{"auth_context", BuildAuthContextJson(result.auth_context)}});
}

Json SdkCommandDispatcher::HandleAuthRefresh(const SdkCommandRequest& request) const {
    if (!providers_.auth_provider) {
        return BuildWsResponse(request.request_id, SdkStatusCode::ProviderNotReady, "provider not ready");
    }

    const std::string api_key = GetOptionalStringField(request.auth, "api_key");
    if (api_key.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::AuthRequired, "auth required");
    }

    const AuthRefreshResult result = providers_.auth_provider->RefreshSession({api_key, NowTs()});
    if (!IsOkStatusCode(result.code)) {
        return BuildWsResponse(request.request_id, result.code, result.message);
    }
    return BuildWsResponse(
        request.request_id,
        SdkStatusCode::Ok,
        "ok",
        Json{
            {"session_key", result.session_token},
            {"session_token", result.session_token},
            {"expires_in", result.expires_in},
            {"auth_context", BuildAuthContextJson(result.auth_context)},
        });
}

Json SdkCommandDispatcher::HandleAuthGetContext(const SdkCommandRequest& request) const {
    if (!providers_.auth_provider) {
        return BuildWsResponse(request.request_id, SdkStatusCode::ProviderNotReady, "provider not ready");
    }

    const std::string api_key = GetOptionalStringField(request.auth, "api_key");
    const std::string session_token = GetSessionCredential(request.auth);
    if (api_key.empty() && session_token.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::AuthRequired, "auth required");
    }

    const AuthContextResult result = providers_.auth_provider->GetAuthContext({api_key, session_token, NowTs()});
    if (!IsOkStatusCode(result.code)) {
        return BuildWsResponse(request.request_id, result.code, result.message);
    }

    return BuildWsResponse(
        request.request_id,
        SdkStatusCode::Ok,
        "ok",
        Json{
            {"via_session", result.via_session},
            {"via_api_key", result.via_api_key},
            {"auth_context", BuildAuthContextJson(result.auth_context)},
        });
}

Json SdkCommandDispatcher::BuildAuthContextJson(const AuthContext& auth_context) const {
    Json device_scope = Json::array();
    for (const SdkDeviceGrant& device : auth_context.device_scope) {
        device_scope.push_back({
            {"vid", device.vid},
            {"pid", device.pid},
        });
    }

    Json capabilities = Json::array();
    for (const std::string& capability : auth_context.capabilities) {
        capabilities.push_back(capability);
    }

    return Json{
        {"is_valid", auth_context.is_valid},
        {"account_type", ToAccountTypeString(auth_context.account_type)},
        {"account_type_code", auth_context.account_type_code},
        {"auth_scene", auth_context.auth_scene},
        {"license_mode", auth_context.license_mode},
        {"device_scope", std::move(device_scope)},
        {"expires_at", auth_context.expires_at},
        {"capabilities", std::move(capabilities)},
    };
}

Json SdkCommandDispatcher::BuildMethodDescriptorJson(const SdkMethodDescriptor& descriptor) const {
    Json aliases = Json::array();
    for (const std::string& alias : descriptor.legacy_aliases) {
        aliases.push_back(alias);
    }
    return Json{
        {"method", descriptor.method},
        {"auth_scope", ToAuthScopeString(descriptor.auth_scope)},
        {"device_scope", ToDeviceScopeString(descriptor.device_scope)},
        {"status", descriptor.status},
        {"summary", descriptor.summary},
        {"legacy_aliases", std::move(aliases)},
    };
}

std::int64_t SdkCommandDispatcher::NowTs() const {
    return static_cast<std::int64_t>(std::time(nullptr));
}

} // namespace sdk
} // namespace editor
