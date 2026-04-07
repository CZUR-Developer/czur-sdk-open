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

std::vector<std::string> GetOptionalStringArrayField(const Json& obj, const char* key) {
    std::vector<std::string> values;
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_array()) {
        return values;
    }
    for (Json::const_iterator value_it = it->begin(); value_it != it->end(); ++value_it) {
        if (value_it->is_string()) {
            values.push_back(value_it->get<std::string>());
        }
    }
    return values;
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

SdkMethodDescriptor MakeMethodDescriptor(const std::string& method,
                                         SdkAuthScope auth_scope,
                                         SdkDeviceScopePolicy device_scope,
                                         const std::string& status,
                                         const std::string& summary,
                                         const std::vector<std::string>& legacy_aliases) {
    SdkMethodDescriptor descriptor;
    descriptor.method = method;
    descriptor.auth_scope = auth_scope;
    descriptor.device_scope = device_scope;
    descriptor.status = status;
    descriptor.summary = summary;
    descriptor.legacy_aliases = legacy_aliases;
    return descriptor;
}

} // namespace

SdkCommandDispatcher::SdkCommandDispatcher(const SdkConfig& config, const ProviderBundle& providers)
    : config_(config),
      providers_(providers) {
    method_descriptors_.push_back(MakeMethodDescriptor(
        "system.ping", SdkAuthScope::Anonymous, SdkDeviceScopePolicy::None, "ga", "SDK 存活探测",
        std::vector<std::string>(1, "ping")));
    method_descriptors_.push_back(MakeMethodDescriptor(
        "system.info", SdkAuthScope::Anonymous, SdkDeviceScopePolicy::None, "ga", "返回运行状态、端口和 provider 信息",
        std::vector<std::string>(1, "getStatus")));
    method_descriptors_.push_back(MakeMethodDescriptor(
        "system.capabilities", SdkAuthScope::Anonymous, SdkDeviceScopePolicy::None, "ga", "返回当前开放 method 清单",
        std::vector<std::string>(1, "listCapabilities")));
    method_descriptors_.push_back(MakeMethodDescriptor(
        "auth.validate", SdkAuthScope::Authenticated, SdkDeviceScopePolicy::None, "ga", "校验 ApiKey 并返回授权上下文",
        std::vector<std::string>()));
    method_descriptors_.push_back(MakeMethodDescriptor(
        "auth.refresh", SdkAuthScope::Authenticated, SdkDeviceScopePolicy::None, "beta",
        "校验 ApiKey 并签发短期 session key", std::vector<std::string>()));
    method_descriptors_.push_back(MakeMethodDescriptor(
        "auth.get_context", SdkAuthScope::Authenticated, SdkDeviceScopePolicy::None, "beta",
        "使用 session key 或 ApiKey 查询授权上下文", std::vector<std::string>()));
    method_descriptors_.push_back(MakeMethodDescriptor(
        "device.list", SdkAuthScope::Entitled, SdkDeviceScopePolicy::AllBoundDevices, "beta", "列出当前可访问的设备",
        std::vector<std::string>()));
    method_descriptors_.push_back(MakeMethodDescriptor(
        "device.get", SdkAuthScope::Entitled, SdkDeviceScopePolicy::TargetDevice, "beta", "查询指定设备的占位信息",
        std::vector<std::string>()));
    method_descriptors_.push_back(MakeMethodDescriptor(
        "device.open", SdkAuthScope::Entitled, SdkDeviceScopePolicy::TargetDevice, "beta", "打开指定设备的占位指令",
        std::vector<std::string>()));
    method_descriptors_.push_back(MakeMethodDescriptor(
        "capture.take", SdkAuthScope::Entitled, SdkDeviceScopePolicy::TargetDevice, "beta", "执行采集占位指令",
        std::vector<std::string>()));
    method_descriptors_.push_back(MakeMethodDescriptor(
        "capture.take_base64", SdkAuthScope::Entitled, SdkDeviceScopePolicy::TargetDevice, "alpha",
        "执行 Base64 采集占位指令", std::vector<std::string>()));
    method_descriptors_.push_back(MakeMethodDescriptor(
        "image.process", SdkAuthScope::Entitled, SdkDeviceScopePolicy::None, "beta", "调用图像处理 provider",
        std::vector<std::string>()));
    method_descriptors_.push_back(MakeMethodDescriptor(
        "ocr.recognize", SdkAuthScope::Entitled, SdkDeviceScopePolicy::None, "beta", "提交 OCR 识别任务",
        std::vector<std::string>()));
    method_descriptors_.push_back(MakeMethodDescriptor(
        "file.convert", SdkAuthScope::Entitled, SdkDeviceScopePolicy::None, "beta", "调用文件转换占位适配",
        std::vector<std::string>()));
    method_descriptors_.push_back(MakeMethodDescriptor(
        "recognition.barcode", SdkAuthScope::Entitled, SdkDeviceScopePolicy::None, "alpha", "执行条码识别占位指令",
        std::vector<std::string>()));
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
    const SdkMethodDescriptor* descriptor = FindMethodDescriptor(request.method);
    if (descriptor != nullptr && MethodRequiresSession(*descriptor)) {
        AuthContext auth_context;
        Json failure_response;
        if (!EnsureMethodAuthorized(request, *descriptor, &auth_context, &failure_response)) {
            return failure_response;
        }
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
    if (request.method == "device.list") {
        return HandleDeviceList(request);
    }
    if (request.method == "device.get") {
        return HandleDeviceGet(request);
    }
    if (request.method == "device.open") {
        return HandleDeviceOpen(request);
    }
    if (request.method == "capture.take") {
        return HandleCaptureTake(request, false);
    }
    if (request.method == "capture.take_base64") {
        return HandleCaptureTake(request, true);
    }
    if (request.method == "image.process") {
        return HandleImageProcess(request);
    }
    if (request.method == "ocr.recognize") {
        return HandleOcrRecognize(request);
    }
    if (request.method == "file.convert") {
        return HandleFileConvert(request);
    }
    if (request.method == "recognition.barcode") {
        return HandleRecognitionBarcode(request);
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
        modules.push_back("capture");
    }
    if (providers_.graphic_provider) {
        modules.push_back("image");
    }
    if (providers_.ocr_provider) {
        modules.push_back("ocr");
        modules.push_back("recognition");
    }
    if (providers_.ofd_provider) {
        modules.push_back("file");
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

bool SdkCommandDispatcher::MethodRequiresSession(const SdkMethodDescriptor& descriptor) const {
    return descriptor.auth_scope == SdkAuthScope::Entitled;
}

bool SdkCommandDispatcher::EnsureMethodAuthorized(const SdkCommandRequest& request,
                                                  const SdkMethodDescriptor& descriptor,
                                                  AuthContext* auth_context,
                                                  Json* failure_response) const {
    if (!providers_.auth_provider) {
        if (failure_response != nullptr) {
            *failure_response =
                BuildWsResponse(request.request_id, SdkStatusCode::ProviderNotReady, "provider not ready");
        }
        return false;
    }

    const std::string session_token = GetSessionCredential(request.auth);
    if (session_token.empty()) {
        if (failure_response != nullptr) {
            *failure_response =
                BuildWsResponse(request.request_id, SdkStatusCode::AuthRequired, "session key required");
        }
        return false;
    }

    SessionValidateRequest validate_request;
    validate_request.session_token = session_token;
    validate_request.now_ts = NowTs();
    const SessionValidateResult validate_result = providers_.auth_provider->ValidateSession(validate_request);
    if (!IsOkStatusCode(validate_result.code)) {
        if (failure_response != nullptr) {
            *failure_response = BuildWsResponse(request.request_id, validate_result.code, validate_result.message);
        }
        return false;
    }
    if (!HasCapability(validate_result.auth_context, descriptor.method)) {
        if (failure_response != nullptr) {
            *failure_response =
                BuildWsResponse(request.request_id, SdkStatusCode::CapabilityNotAllowed, "capability not allowed");
        }
        return false;
    }

    if (auth_context != nullptr) {
        *auth_context = validate_result.auth_context;
    }
    return true;
}

bool SdkCommandDispatcher::HasCapability(const AuthContext& auth_context, const std::string& capability) const {
    for (std::vector<std::string>::const_iterator it = auth_context.capabilities.begin();
         it != auth_context.capabilities.end();
         ++it) {
        if (*it == capability) {
            return true;
        }
    }
    return false;
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

    AuthValidateRequest validate_request;
    validate_request.api_key = api_key;
    validate_request.now_ts = NowTs();
    const AuthValidateResult result = providers_.auth_provider->ValidateApiKey(validate_request);
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

    AuthRefreshRequest refresh_request;
    refresh_request.api_key = api_key;
    refresh_request.now_ts = NowTs();
    const AuthRefreshResult result = providers_.auth_provider->RefreshSession(refresh_request);
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

    AuthLookupRequest lookup_request;
    lookup_request.api_key = api_key;
    lookup_request.session_token = session_token;
    lookup_request.now_ts = NowTs();
    const AuthContextResult result = providers_.auth_provider->GetAuthContext(lookup_request);
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

Json SdkCommandDispatcher::HandleDeviceList(const SdkCommandRequest& request) const {
    if (!providers_.device_provider) {
        return BuildWsResponse(request.request_id, SdkStatusCode::ProviderNotReady, "provider not ready");
    }

    const std::vector<std::string> devices = providers_.device_provider->ListDevices();
    Json device_json = Json::array();
    for (std::vector<std::string>::const_iterator it = devices.begin(); it != devices.end(); ++it) {
        device_json.push_back(*it);
    }
    return BuildWsResponse(
        request.request_id, SdkStatusCode::Ok, "ok", Json{{"devices", device_json}, {"count", devices.size()}});
}

Json SdkCommandDispatcher::HandleDeviceGet(const SdkCommandRequest& request) const {
    if (!providers_.device_provider) {
        return BuildWsResponse(request.request_id, SdkStatusCode::ProviderNotReady, "provider not ready");
    }

    const std::string device_id = GetOptionalStringField(request.params, "device_id");
    if (device_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "device_id required");
    }

    const std::vector<std::string> devices = providers_.device_provider->ListDevices();
    bool matched = false;
    for (std::vector<std::string>::const_iterator it = devices.begin(); it != devices.end(); ++it) {
        if (*it == device_id) {
            matched = true;
            break;
        }
    }
    if (!matched) {
        return BuildWsResponse(request.request_id, SdkStatusCode::ProviderCallFailed, "device not found");
    }

    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"device_id", device_id}, {"state", "available"}, {"provider", providers_.device_provider->ProviderName()}});
}

Json SdkCommandDispatcher::HandleDeviceOpen(const SdkCommandRequest& request) const {
    if (!providers_.device_provider) {
        return BuildWsResponse(request.request_id, SdkStatusCode::ProviderNotReady, "provider not ready");
    }

    const std::string device_id = GetOptionalStringField(request.params, "device_id");
    if (device_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "device_id required");
    }

    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"device_id", device_id},
                                {"opened", true},
                                {"provider", providers_.device_provider->ProviderName()},
                                {"note", "sdk_open exposes a placeholder device.open adapter in this build"}});
}

Json SdkCommandDispatcher::HandleCaptureTake(const SdkCommandRequest& request, bool include_base64) const {
    const std::string device_id = GetOptionalStringField(request.params, "device_id");
    if (device_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "device_id required");
    }

    Json data{
        {"device_id", device_id},
        {"captured", true},
        {"content_type", include_base64 ? "image/base64" : "image/file"},
        {"note", "sdk_open capture adapter is a session-gated placeholder in this build"},
    };
    if (include_base64) {
        data["payload"] = "c2RrX29wZW5fY2FwdHVyZV9wbGFjZWhvbGRlcg==";
    }
    return BuildWsResponse(request.request_id, SdkStatusCode::Ok, "ok", data);
}

Json SdkCommandDispatcher::HandleImageProcess(const SdkCommandRequest& request) const {
    if (!providers_.graphic_provider) {
        return BuildWsResponse(request.request_id, SdkStatusCode::ProviderNotReady, "provider not ready");
    }

    const std::string input_path = GetOptionalStringField(request.params, "input_path");
    const std::string output_path = GetOptionalStringField(request.params, "output_path");
    if (input_path.empty() || output_path.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "input_path and output_path required");
    }

    const bool ok = providers_.graphic_provider->ProcessSampleTask(input_path, output_path);
    if (!ok) {
        return BuildWsResponse(request.request_id, SdkStatusCode::ProviderCallFailed, "graphic provider call failed");
    }

    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"input_path", input_path},
                                {"output_path", output_path},
                                {"processed", true},
                                {"provider", providers_.graphic_provider->ProviderName()}});
}

Json SdkCommandDispatcher::HandleOcrRecognize(const SdkCommandRequest& request) const {
    if (!providers_.ocr_provider) {
        return BuildWsResponse(request.request_id, SdkStatusCode::ProviderNotReady, "provider not ready");
    }

    const std::vector<std::string> input_files = GetOptionalStringArrayField(request.params, "input_files");
    const std::string output_path = GetOptionalStringField(request.params, "output_path");
    if (input_files.empty() || output_path.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "input_files and output_path required");
    }

    const std::string task_id = providers_.ocr_provider->SubmitTask(input_files, output_path);
    if (task_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::ProviderCallFailed, "ocr provider call failed");
    }

    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"task_id", task_id},
                                {"input_count", input_files.size()},
                                {"output_path", output_path},
                                {"provider", providers_.ocr_provider->ProviderName()}});
}

Json SdkCommandDispatcher::HandleFileConvert(const SdkCommandRequest& request) const {
    if (!providers_.ofd_provider) {
        return BuildWsResponse(request.request_id, SdkStatusCode::ProviderNotReady, "provider not ready");
    }

    const std::string input_path = GetOptionalStringField(request.params, "input_path");
    const std::string output_path = GetOptionalStringField(request.params, "output_path");
    if (input_path.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "input_path required");
    }

    const bool ok = providers_.ofd_provider->OpenDocument(input_path);
    if (!ok) {
        return BuildWsResponse(request.request_id, SdkStatusCode::ProviderCallFailed, "file provider call failed");
    }

    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"input_path", input_path},
                                {"output_path", output_path},
                                {"accepted", true},
                                {"provider", providers_.ofd_provider->ProviderName()},
                                {"note", "current sdk_open file.convert uses the open-document provider adapter"}});
}

Json SdkCommandDispatcher::HandleRecognitionBarcode(const SdkCommandRequest& request) const {
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::ProviderCallFailed,
                           "barcode recognition provider not implemented");
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
