// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "command_application_service.h"

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

std::string BuildDefaultAssetBaseUrl(const SdkConfig& config) {
    if (!config.asset_base_url.empty()) {
        return config.asset_base_url;
    }
    const std::string host = config.bind_host == "0.0.0.0" ? "127.0.0.1" : config.bind_host;
    return "http://" + host + ":" + std::to_string(config.asset_http_port);
}

int GetOptionalIntField(const Json& obj, const char* key, int default_value) {
    const auto it = obj.find(key);
    if (it != obj.end() && it->is_number_integer()) {
        return it->get<int>();
    }
    return default_value;
}

bool GetOptionalBoolField(const Json& obj, const char* key, bool default_value) {
    const auto it = obj.find(key);
    if (it != obj.end() && it->is_boolean()) {
        return it->get<bool>();
    }
    return default_value;
}

SdkCaptureProfile ParseCaptureProfile(const Json& params, const std::string& device_id) {
    SdkCaptureProfile profile;
    profile.device_id = device_id;
    const Json profile_json = GetOptionalObjectField(params, "profile");
    if (profile_json.empty()) {
        return profile;
    }
    profile.profile_version = GetOptionalStringField(profile_json, "profile_version");
    if (profile.profile_version.empty()) {
        profile.profile_version = "capture.profile.v1";
    }
    profile.revision = GetOptionalIntField(profile_json, "revision", 1);

    const Json device_json = GetOptionalObjectField(profile_json, "device");
    if (!device_json.empty()) {
        const std::string profile_device_id = GetOptionalStringField(device_json, "device_id");
        if (!profile_device_id.empty()) {
            profile.device_id = profile_device_id;
        }
        const Json resolution_json = GetOptionalObjectField(device_json, "resolution");
        profile.width = GetOptionalIntField(resolution_json, "width", 0);
        profile.height = GetOptionalIntField(resolution_json, "height", 0);
        profile.fps = GetOptionalIntField(resolution_json, "fps", 0);
    }

    const Json capture_json = GetOptionalObjectField(profile_json, "capture");
    if (!capture_json.empty()) {
        const std::string page_processing = GetOptionalStringField(capture_json, "page_processing");
        const std::string color_mode = GetOptionalStringField(capture_json, "color_mode");
        if (!page_processing.empty()) {
            profile.page_processing = page_processing;
        }
        if (!color_mode.empty()) {
            profile.color_mode = color_mode;
        }
    }

    const Json output_json = GetOptionalObjectField(profile_json, "output");
    if (!output_json.empty()) {
        const std::string format = GetOptionalStringField(output_json, "format");
        if (!format.empty()) {
            profile.output_format = format;
        }
        const Json thumbnails_json = GetOptionalObjectField(output_json, "thumbnails");
        profile.thumbnail_original = GetOptionalBoolField(thumbnails_json, "original", profile.thumbnail_original);
        profile.thumbnail_page_processed = GetOptionalBoolField(thumbnails_json, "page_processed", profile.thumbnail_page_processed);
        profile.thumbnail_color_processed = GetOptionalBoolField(thumbnails_json, "color_processed", profile.thumbnail_color_processed);
        profile.thumbnail_final = GetOptionalBoolField(thumbnails_json, "final", profile.thumbnail_final);
    }
    return profile;
}

Json BuildAssetJson(const SdkCaptureAsset& asset) {
    return Json{{"asset_id", asset.asset_id},
                {"kind", asset.kind},
                {"path", asset.path},
                {"url", asset.url},
                {"download_url", asset.download_url},
                {"content_type", asset.content_type},
                {"width", asset.width},
                {"height", asset.height},
                {"size", asset.size}};
}

Json BuildStageJson(const SdkCaptureStageResult& stage) {
    return Json{{"name", stage.name},
                {"status", stage.status},
                {"input", stage.input_assets},
                {"output", stage.output_assets},
                {"provider", stage.provider},
                {"message", stage.message}};
}

Json BuildCaptureTaskJson(const CaptureTaskSnapshot& task) {
    Json stages = Json::array();
    for (std::vector<SdkCaptureStageResult>::const_iterator it = task.stages.begin(); it != task.stages.end(); ++it) {
        stages.push_back(BuildStageJson(*it));
    }
    Json assets = Json::array();
    for (std::vector<SdkCaptureAsset>::const_iterator it = task.assets.begin(); it != task.assets.end(); ++it) {
        assets.push_back(BuildAssetJson(*it));
    }
    return Json{{"task_id", task.task_id},
                {"status", task.status},
                {"device_id", task.device_id},
                {"profile_revision", task.profile_revision},
                {"stages", stages},
                {"assets", assets},
                {"warnings", task.warnings},
                {"error", task.error}};
}

CommandApplicationService::MethodDescriptor MakeMethod(const std::string& method,
                                                       bool requires_session,
                                                       const std::string& summary) {
    CommandApplicationService::MethodDescriptor descriptor;
    descriptor.method = method;
    descriptor.requires_session = requires_session;
    descriptor.summary = summary;
    return descriptor;
}

} // namespace

CommandApplicationService::CommandApplicationService(const SdkConfig& config, const ProviderBundle& providers)
    : config_(config),
      providers_(providers),
      authorization_service_(providers_),
      device_facade_(providers_),
      graphic_facade_(providers_),
      ocr_facade_(providers_),
      ofd_facade_(providers_),
      capture_task_service_(providers_, BuildDefaultAssetBaseUrl(config_)) {
    methods_.push_back(MakeMethod("system.ping", false, "SDK heartbeat probe"));
    methods_.push_back(MakeMethod("system.info", false, "SDK runtime status snapshot"));
    methods_.push_back(MakeMethod("system.capabilities", false, "List public methods and auth model"));
    methods_.push_back(MakeMethod("auth.create_session", false, "Create a bound session from token"));
    methods_.push_back(MakeMethod("auth.get_context", true, "Get the current bound auth context"));
    methods_.push_back(MakeMethod("auth.refresh_session", true, "Refresh the current bound session"));
    methods_.push_back(MakeMethod("auth.activate_offline", true, "Unlock one offline api key on this machine"));
    methods_.push_back(MakeMethod("auth.destroy_session", true, "Destroy the current bound session"));
    methods_.push_back(MakeMethod("device.list", true, "List devices visible to the current session"));
    methods_.push_back(MakeMethod("device.get", true, "Get one device visible to the current session"));
    methods_.push_back(MakeMethod("device.open", true, "Open a device"));
    methods_.push_back(MakeMethod("device.close", true, "Close a device and release active preview resources"));
    methods_.push_back(MakeMethod("capture.take", true, "Capture a still image"));
    methods_.push_back(MakeMethod("capture.get", true, "Get one capture task snapshot"));
    methods_.push_back(MakeMethod("video.start", true, "Create one video stream session"));
    methods_.push_back(MakeMethod("video.stop", true, "Stop one video stream session"));
    methods_.push_back(MakeMethod("video.set_format", true, "Update one video stream format"));
    methods_.push_back(MakeMethod("image.process", true, "Run one image-processing request"));
    methods_.push_back(MakeMethod("ocr.recognize", true, "Submit one OCR request"));
    methods_.push_back(MakeMethod("file.convert", true, "Submit one file conversion request"));
}

void CommandApplicationService::SetStatusSupplier(StatusSupplier supplier) {
    status_supplier_ = std::move(supplier);
}

void CommandApplicationService::SetVideoFrameSink(VideoFrameSink sink) {
    video_frame_sink_ = std::move(sink);
}

void CommandApplicationService::SetVideoStreamClosedSink(VideoStreamClosedSink sink) {
    video_stream_closed_sink_ = std::move(sink);
}

void CommandApplicationService::SetCommandEventSink(CommandEventSink sink) {
    capture_task_service_.SetEventSink(std::move(sink));
}

Json CommandApplicationService::HandleRequest(const std::string& connection_id, const Json& request_json) {
    if (!request_json.is_object()) {
        return BuildWsResponse("", SdkStatusCode::InvalidRequest, "invalid request");
    }

    Request request;
    request.request_id = GetOptionalStringField(request_json, "request_id");
    request.method = GetOptionalStringField(request_json, "method");
    request.params = GetOptionalObjectField(request_json, "params");
    request.client = GetOptionalObjectField(request_json, "client");

    if (request.request_id.empty()) {
        return BuildWsResponse("", SdkStatusCode::InvalidRequest, "request_id required");
    }
    if (request.method.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidMethod, "missing method");
    }

    const MethodDescriptor* descriptor = FindMethod(request.method);
    if (descriptor == NULL) {
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
    if (request.method == "auth.create_session") {
        return HandleAuthCreateSession(connection_id, request);
    }
    if (request.method == "auth.get_context") {
        return HandleAuthGetContext(connection_id, request);
    }
    if (request.method == "auth.refresh_session") {
        return HandleAuthRefreshSession(connection_id, request);
    }
    if (request.method == "auth.activate_offline") {
        return HandleAuthActivateOffline(connection_id, request);
    }
    if (request.method == "auth.destroy_session") {
        return HandleAuthDestroySession(connection_id, request);
    }
    if (request.method == "device.list") {
        return HandleDeviceList(connection_id, request);
    }
    if (request.method == "device.get") {
        return HandleDeviceGet(connection_id, request);
    }
    if (request.method == "device.open") {
        return HandleDeviceOpen(connection_id, request);
    }
    if (request.method == "device.close") {
        return HandleDeviceClose(connection_id, request);
    }
    if (request.method == "capture.take") {
        return HandleCaptureTake(connection_id, request);
    }
    if (request.method == "capture.get") {
        return HandleCaptureGet(connection_id, request);
    }
    if (request.method == "video.start") {
        return HandleVideoStart(connection_id, request);
    }
    if (request.method == "video.stop") {
        return HandleVideoStop(connection_id, request);
    }
    if (request.method == "video.set_format") {
        return HandleVideoSetFormat(connection_id, request);
    }
    if (request.method == "image.process") {
        return HandleImageProcess(connection_id, request);
    }
    if (request.method == "ocr.recognize") {
        return HandleOcrRecognize(connection_id, request);
    }
    if (request.method == "file.convert") {
        return HandleFileConvert(connection_id, request);
    }

    return BuildWsResponse(request.request_id, SdkStatusCode::UnsupportedMethod, "unsupported method");
}

void CommandApplicationService::OnConnectionClosed(const std::string& connection_id) {
    authorization_service_.ClearConnection(connection_id);
    const std::vector<VideoSessionService::StreamBinding> removed = video_session_service_.ClearConnection(connection_id);
    for (std::vector<VideoSessionService::StreamBinding>::const_iterator it = removed.begin();
         it != removed.end();
         ++it) {
        if (providers_.device_provider) {
            SdkVideoStopRequest stop_request;
            stop_request.device_id = it->device_id;
            providers_.device_provider->StopVideo(stop_request);
        }
        if (video_stream_closed_sink_) {
            video_stream_closed_sink_(it->stream_id);
        }
    }
    const std::vector<std::string> opened_devices = ClearOpenedDevices(connection_id);
    for (std::vector<std::string>::const_iterator it = opened_devices.begin(); it != opened_devices.end(); ++it) {
        SdkDeviceCloseRequest close_request;
        close_request.device_id = *it;
        device_facade_.CloseDevice(AuthContext(), close_request);
    }
}

Json CommandApplicationService::BuildCapabilitiesJson() const {
    Json methods = Json::array();
    for (std::vector<MethodDescriptor>::const_iterator it = methods_.begin(); it != methods_.end(); ++it) {
        methods.push_back(Json{
            {"method", it->method},
            {"requires_session", it->requires_session},
            {"summary", it->summary},
        });
    }

    return Json{
        {"auth_model",
         {
             {"connection_requires_token", false},
             {"session_field", "session_token"},
             {"session_binding", "connection_bound"},
             {"video_binding", "session_token + stream_id"},
         }},
        {"methods", std::move(methods)},
    };
}

VideoSessionService::ValidationResult CommandApplicationService::ValidateVideoStream(const std::string& session_token,
                                                                                     const std::string& stream_id) const {
    return video_session_service_.Validate(session_token, stream_id);
}

CommandApplicationService::AssetAccessResult CommandApplicationService::ResolveAsset(const std::string& session_token,
                                                                                    const std::string& task_id,
                                                                                    const std::string& asset_id) const {
    AssetAccessResult result;
    AuthorizationService::SessionResult session_result = authorization_service_.RequireSessionToken(session_token);
    if (!IsOkStatusCode(session_result.code)) {
        result.code = session_result.code;
        result.message = session_result.message;
        return result;
    }
    if (!IsOkStatusCode(authorization_service_.RequireCapability(session_result.connection_id, "capture.get").code)) {
        result.code = ToCode(SdkStatusCode::CapabilityNotAllowed);
        result.message = "capability not allowed";
        return result;
    }

    const CaptureAssetResult asset_result =
        capture_task_service_.GetAsset(session_result.connection_id, task_id, asset_id);
    result.code = asset_result.code;
    result.message = asset_result.message;
    result.asset = asset_result.asset;
    return result;
}

std::size_t CommandApplicationService::ActiveSessionCount() const {
    return authorization_service_.ActiveSessionCount();
}

std::size_t CommandApplicationService::ActiveStreamCount() const {
    return video_session_service_.ActiveStreamCount();
}

Json CommandApplicationService::HandleSystemPing(const Request& request) const {
    return BuildWsResponse(request.request_id, SdkStatusCode::Ok, "ok", Json{{"pong", true}});
}

Json CommandApplicationService::HandleSystemInfo(const Request& request) const {
    return BuildWsResponse(request.request_id, SdkStatusCode::Ok, "ok", status_supplier_ ? status_supplier_() : Json::object());
}

Json CommandApplicationService::HandleSystemCapabilities(const Request& request) const {
    return BuildWsResponse(request.request_id, SdkStatusCode::Ok, "ok", BuildCapabilitiesJson());
}

Json CommandApplicationService::HandleAuthCreateSession(const std::string& connection_id, const Request& request) {
    const std::string token = GetOptionalStringField(request.params, "token");
    const AuthorizationService::SessionResult session_result = authorization_service_.CreateSession(connection_id, token);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    return BuildWsResponse(request.request_id, SdkStatusCode::Ok, "ok", BuildSessionJson(session_result));
}

Json CommandApplicationService::HandleAuthGetContext(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = authorization_service_.GetContext(connection_id);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"session_token", session_result.session_token},
                                {"auth_context", BuildAuthContextJson(session_result.auth_context)}});
}

Json CommandApplicationService::HandleAuthRefreshSession(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = authorization_service_.RefreshSession(connection_id);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    return BuildWsResponse(request.request_id, SdkStatusCode::Ok, "ok", BuildSessionJson(session_result));
}

Json CommandApplicationService::HandleAuthActivateOffline(const std::string& connection_id, const Request& request) {
    const std::string auth_code = GetOptionalStringField(request.params, "auth_code");
    if (auth_code.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "auth_code required");
    }

    const AuthorizationService::SessionResult session_result =
        authorization_service_.ActivateOffline(connection_id, auth_code);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    return BuildWsResponse(request.request_id, SdkStatusCode::Ok, "ok", BuildSessionJson(session_result));
}

Json CommandApplicationService::HandleAuthDestroySession(const std::string& connection_id, const Request& request) {
    const std::vector<VideoSessionService::StreamBinding> removed = video_session_service_.ClearConnection(connection_id);
    for (std::vector<VideoSessionService::StreamBinding>::const_iterator it = removed.begin();
         it != removed.end();
         ++it) {
        if (providers_.device_provider) {
            SdkVideoStopRequest stop_request;
            stop_request.device_id = it->device_id;
            providers_.device_provider->StopVideo(stop_request);
        }
        if (video_stream_closed_sink_) {
            video_stream_closed_sink_(it->stream_id);
        }
    }
    const std::vector<std::string> opened_devices = ClearOpenedDevices(connection_id);
    for (std::vector<std::string>::const_iterator it = opened_devices.begin(); it != opened_devices.end(); ++it) {
        SdkDeviceCloseRequest close_request;
        close_request.device_id = *it;
        device_facade_.CloseDevice(AuthContext(), close_request);
    }
    const AuthorizationService::SessionResult session_result = authorization_service_.DestroySession(connection_id);
    return BuildWsResponse(request.request_id, session_result.code, session_result.message, Json{{"destroyed", true}});
}

Json CommandApplicationService::HandleDeviceList(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }

    const DeviceListResult result = device_facade_.ListDevices(session_result.auth_context);
    if (!IsOkStatusCode(result.code)) {
        return BuildWsResponse(request.request_id, result.code, result.message);
    }

    Json devices = Json::array();
    for (std::vector<SdkDeviceDescriptor>::const_iterator it = result.devices.begin(); it != result.devices.end(); ++it) {
        devices.push_back(BuildDeviceJson(*it));
    }

    return BuildWsResponse(request.request_id, SdkStatusCode::Ok, "ok", Json{{"devices", devices}, {"count", devices.size()}});
}

Json CommandApplicationService::HandleDeviceGet(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    const std::string device_id = GetOptionalStringField(request.params, "device_id");
    if (device_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "device_id required");
    }

    const DeviceGetResult result = device_facade_.GetDevice(session_result.auth_context, device_id);
    if (!IsOkStatusCode(result.code)) {
        return BuildWsResponse(request.request_id, result.code, result.message);
    }
    Json data = BuildDeviceJson(result.device);
    data["provider"] = providers_.device_provider ? providers_.device_provider->ProviderName() : "";
    return BuildWsResponse(request.request_id, SdkStatusCode::Ok, "ok", data);
}

Json CommandApplicationService::HandleDeviceOpen(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    SdkDeviceOpenRequest open_request;
    open_request.device_id = GetOptionalStringField(request.params, "device_id");
    open_request.width = GetOptionalIntField(request.params, "width", 0);
    open_request.height = GetOptionalIntField(request.params, "height", 0);
    open_request.fps = GetOptionalIntField(request.params, "fps", 0);
    open_request.pixel_format = GetOptionalStringField(request.params, "pixel_format");
    if (open_request.pixel_format.empty()) {
        open_request.pixel_format = "jpeg";
    }
    if (open_request.device_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "device_id required");
    }
    const SdkDeviceOpenResult result = device_facade_.OpenDevice(session_result.auth_context, open_request);
    if (!IsOkStatusCode(result.code)) {
        return BuildWsResponse(request.request_id, result.code, result.message);
    }
    Json data = BuildDeviceJson(result.device);
    data["device_id"] = result.device.device_id.empty() ? open_request.device_id : result.device.device_id;
    data["opened"] = result.opened;
    data["provider"] = providers_.device_provider ? providers_.device_provider->ProviderName() : "";
    if (result.opened) {
        RememberOpenedDevice(connection_id, open_request.device_id);
    }
    return BuildWsResponse(request.request_id, SdkStatusCode::Ok, "ok", data);
}

Json CommandApplicationService::HandleDeviceClose(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }

    SdkDeviceCloseRequest close_request;
    close_request.device_id = GetOptionalStringField(request.params, "device_id");
    if (close_request.device_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "device_id required");
    }

    bool stopped_stream = false;
    std::string stopped_stream_id;
    SdkVideoStopRequest stop_request;
    stop_request.device_id = close_request.device_id;
    const SdkVideoStopResult stop_result = device_facade_.StopVideo(session_result.auth_context, stop_request);
    if (!IsOkStatusCode(stop_result.code)) {
        return BuildWsResponse(request.request_id, stop_result.code, stop_result.message);
    }
    const VideoSessionService::StreamResult stream_result =
        video_session_service_.StopStream(connection_id, close_request.device_id);
    if (IsOkStatusCode(stream_result.code)) {
        stopped_stream = true;
        stopped_stream_id = stream_result.binding.stream_id;
        if (video_stream_closed_sink_) {
            video_stream_closed_sink_(stream_result.binding.stream_id);
        }
    } else if (stream_result.code != ToCode(SdkStatusCode::StreamNotFound)) {
        return BuildWsResponse(request.request_id, stream_result.code, stream_result.message);
    }

    const SdkDeviceCloseResult close_result = device_facade_.CloseDevice(session_result.auth_context, close_request);
    if (!IsOkStatusCode(close_result.code)) {
        return BuildWsResponse(request.request_id, close_result.code, close_result.message);
    }
    ForgetOpenedDevice(connection_id, close_request.device_id);

    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"device_id", close_request.device_id},
                                {"closed", close_result.closed},
                                {"was_opened", close_result.was_opened},
                                {"stopped_stream", stopped_stream},
                                {"stream_id", stopped_stream_id},
                                {"provider", providers_.device_provider ? providers_.device_provider->ProviderName() : ""}});
}

Json CommandApplicationService::HandleCaptureTake(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    const AuthorizationService::SessionResult quota_result = ConsumeQuota(connection_id, request.method, request.request_id);
    if (!IsOkStatusCode(quota_result.code)) {
        return BuildWsResponse(request.request_id, quota_result.code, quota_result.message);
    }

    const std::string device_id = GetOptionalStringField(request.params, "device_id");
    if (device_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "device_id required");
    }
    const Json profile_json = GetOptionalObjectField(request.params, "profile");
    if (profile_json.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "profile required");
    }

    CaptureTaskStartRequest start_request;
    start_request.connection_id = connection_id;
    start_request.session_token = session_result.session_token;
    start_request.device_id = device_id;
    start_request.output_dir = GetOptionalStringField(request.params, "output_dir");
    start_request.include_base64 = GetOptionalBoolField(request.params, "include_base64", false);
    start_request.timeout_ms = GetOptionalIntField(request.params, "timeout_ms", 15000);
    start_request.auth_context = session_result.auth_context;
    start_request.profile = ParseCaptureProfile(request.params, device_id);
    if (start_request.profile.device_id.empty()) {
        start_request.profile.device_id = device_id;
    }

    const CaptureTaskStartResult result = capture_task_service_.StartTask(start_request);
    if (!IsOkStatusCode(result.code)) {
        return BuildWsResponse(request.request_id, result.code, result.message);
    }
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"accepted", result.accepted},
                                {"task_id", result.task.task_id},
                                {"status", result.task.status}});
}

Json CommandApplicationService::HandleCaptureGet(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    const std::string task_id = GetOptionalStringField(request.params, "task_id");
    if (task_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "task_id required");
    }
    const CaptureTaskSnapshot task = capture_task_service_.GetTask(connection_id, task_id);
    if (!IsOkStatusCode(task.code)) {
        return BuildWsResponse(request.request_id, task.code, task.message);
    }
    return BuildWsResponse(request.request_id, SdkStatusCode::Ok, "ok", BuildCaptureTaskJson(task));
}

Json CommandApplicationService::HandleVideoStart(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }

    SdkVideoStartRequest start_request;
    start_request.device_id = GetOptionalStringField(request.params, "device_id");
    start_request.width = GetOptionalIntField(request.params, "width", 0);
    start_request.height = GetOptionalIntField(request.params, "height", 0);
    start_request.fps = GetOptionalIntField(request.params, "fps", 0);
    start_request.pixel_format = GetOptionalStringField(request.params, "pixel_format");
    if (start_request.pixel_format.empty()) {
        start_request.pixel_format = "jpeg";
    }
    if (start_request.device_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "device_id required");
    }

    const VideoSessionService::StreamResult stream_result =
        video_session_service_.RegisterStream(connection_id,
                                              session_result.session_token,
                                              start_request.device_id,
                                              start_request.pixel_format,
                                              start_request.width > 0 ? start_request.width : 1280,
                                              start_request.height > 0 ? start_request.height : 720,
                                              start_request.fps > 0 ? start_request.fps : 15);
    start_request.stream_id = stream_result.binding.stream_id;

    const SdkVideoStartResult start_result =
        device_facade_.StartVideo(session_result.auth_context,
                                  start_request,
                                  [this](const SdkVideoFrame& frame) {
                                      if (video_frame_sink_) {
                                          video_frame_sink_(frame);
                                      }
                                  });
    if (!IsOkStatusCode(start_result.code)) {
        video_session_service_.StopStreamById(stream_result.binding.stream_id);
        return BuildWsResponse(request.request_id, start_result.code, start_result.message);
    }

    const VideoSessionService::StreamResult updated_stream_result =
        video_session_service_.UpdateStreamFormat(connection_id,
                                                  start_request.device_id,
                                                  start_result.pixel_format,
                                                  start_result.width,
                                                  start_result.height,
                                                  start_result.fps);
    if (!IsOkStatusCode(updated_stream_result.code)) {
        return BuildWsResponse(request.request_id, updated_stream_result.code, updated_stream_result.message);
    }
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"device_id", start_request.device_id},
                                {"stream_id", updated_stream_result.binding.stream_id},
                                {"session_token", updated_stream_result.binding.session_token},
                                {"pixel_format", updated_stream_result.binding.pixel_format},
                                {"width", updated_stream_result.binding.width},
                                {"height", updated_stream_result.binding.height},
                                {"fps", updated_stream_result.binding.fps}});
}

Json CommandApplicationService::HandleVideoStop(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }

    SdkVideoStopRequest stop_request;
    stop_request.device_id = GetOptionalStringField(request.params, "device_id");
    if (stop_request.device_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "device_id required");
    }

    const SdkVideoStopResult stop_result = device_facade_.StopVideo(session_result.auth_context, stop_request);
    if (!IsOkStatusCode(stop_result.code)) {
        return BuildWsResponse(request.request_id, stop_result.code, stop_result.message);
    }
    const VideoSessionService::StreamResult stream_result = video_session_service_.StopStream(connection_id, stop_request.device_id);
    if (!IsOkStatusCode(stream_result.code)) {
        return BuildWsResponse(request.request_id, stream_result.code, stream_result.message);
    }
    if (video_stream_closed_sink_) {
        video_stream_closed_sink_(stream_result.binding.stream_id);
    }
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"device_id", stop_request.device_id},
                                {"stream_id", stream_result.binding.stream_id},
                                {"stopped", true}});
}

Json CommandApplicationService::HandleVideoSetFormat(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }

    SdkVideoFormatRequest format_request;
    format_request.device_id = GetOptionalStringField(request.params, "device_id");
    format_request.pixel_format = GetOptionalStringField(request.params, "pixel_format");
    format_request.width = GetOptionalIntField(request.params, "width", 1280);
    format_request.height = GetOptionalIntField(request.params, "height", 720);
    format_request.fps = GetOptionalIntField(request.params, "fps", 15);
    if (format_request.device_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "device_id required");
    }
    if (format_request.pixel_format.empty()) {
        format_request.pixel_format = "jpeg";
    }

    const SdkVideoFormatResult format_result = device_facade_.SetVideoFormat(session_result.auth_context, format_request);
    if (!IsOkStatusCode(format_result.code)) {
        return BuildWsResponse(request.request_id, format_result.code, format_result.message);
    }
    const VideoSessionService::StreamResult stream_result =
        video_session_service_.UpdateStreamFormat(connection_id,
                                                  format_request.device_id,
                                                  format_request.pixel_format,
                                                  format_request.width,
                                                  format_request.height,
                                                  format_request.fps);
    if (!IsOkStatusCode(stream_result.code)) {
        return BuildWsResponse(request.request_id, stream_result.code, stream_result.message);
    }

    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"device_id", format_request.device_id},
                                {"stream_id", stream_result.binding.stream_id},
                                {"pixel_format", stream_result.binding.pixel_format},
                                {"width", stream_result.binding.width},
                                {"height", stream_result.binding.height},
                                {"fps", stream_result.binding.fps}});
}

Json CommandApplicationService::HandleImageProcess(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    const AuthorizationService::SessionResult quota_result = ConsumeQuota(connection_id, request.method, request.request_id);
    if (!IsOkStatusCode(quota_result.code)) {
        return BuildWsResponse(request.request_id, quota_result.code, quota_result.message);
    }

    SdkImageProcessRequest process_request;
    process_request.input_path = GetOptionalStringField(request.params, "input_path");
    process_request.output_path = GetOptionalStringField(request.params, "output_path");
    if (process_request.input_path.empty() || process_request.output_path.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "input_path and output_path required");
    }

    const SdkImageProcessResult result = graphic_facade_.Process(process_request);
    if (!IsOkStatusCode(result.code)) {
        return BuildWsResponse(request.request_id, result.code, result.message);
    }
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"input_path", process_request.input_path},
                                {"output_path", process_request.output_path},
                                {"processed", result.processed},
                                {"provider", providers_.graphic_provider ? providers_.graphic_provider->ProviderName() : ""}});
}

Json CommandApplicationService::HandleOcrRecognize(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }

    SdkOcrRecognizeRequest ocr_request;
    ocr_request.input_files = GetOptionalStringArrayField(request.params, "input_files");
    ocr_request.output_path = GetOptionalStringField(request.params, "output_path");
    if (ocr_request.input_files.empty() || ocr_request.output_path.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "input_files and output_path required");
    }

    const SdkOcrRecognizeResult result = ocr_facade_.Recognize(ocr_request);
    if (!IsOkStatusCode(result.code)) {
        return BuildWsResponse(request.request_id, result.code, result.message);
    }
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"task_id", result.task_id},
                                {"input_count", ocr_request.input_files.size()},
                                {"output_path", ocr_request.output_path},
                                {"provider", providers_.ocr_provider ? providers_.ocr_provider->ProviderName() : ""}});
}

Json CommandApplicationService::HandleFileConvert(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    const AuthorizationService::SessionResult quota_result = ConsumeQuota(connection_id, request.method, request.request_id);
    if (!IsOkStatusCode(quota_result.code)) {
        return BuildWsResponse(request.request_id, quota_result.code, quota_result.message);
    }

    SdkFileConvertRequest convert_request;
    convert_request.input_path = GetOptionalStringField(request.params, "input_path");
    convert_request.output_path = GetOptionalStringField(request.params, "output_path");
    if (convert_request.input_path.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "input_path required");
    }

    const SdkFileConvertResult result = ofd_facade_.Convert(convert_request);
    if (!IsOkStatusCode(result.code)) {
        return BuildWsResponse(request.request_id, result.code, result.message);
    }
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"input_path", convert_request.input_path},
                                {"output_path", convert_request.output_path},
                                {"accepted", result.accepted},
                                {"provider", providers_.ofd_provider ? providers_.ofd_provider->ProviderName() : ""}});
}

AuthorizationService::SessionResult CommandApplicationService::RequireCapability(const std::string& connection_id,
                                                                                 const std::string& capability) const {
    return authorization_service_.RequireCapability(connection_id, capability);
}

AuthorizationService::SessionResult CommandApplicationService::ConsumeQuota(const std::string& connection_id,
                                                                            const std::string& capability,
                                                                            const std::string& request_id,
                                                                            int units) {
    return authorization_service_.ConsumeQuota(connection_id, capability, request_id, units);
}

Json CommandApplicationService::BuildSessionJson(const AuthorizationService::SessionResult& session_result) const {
    return Json{
        {"session_token", session_result.session_token},
        {"expires_in", session_result.expires_in},
        {"auth_context", BuildAuthContextJson(session_result.auth_context)},
    };
}

Json CommandApplicationService::BuildAuthContextJson(const AuthContext& auth_context) const {
    Json device_scope = Json::array();
    for (std::vector<SdkDeviceGrant>::const_iterator it = auth_context.device_scope.begin();
         it != auth_context.device_scope.end();
         ++it) {
        device_scope.push_back(Json{
            {"vid", it->vid},
            {"pid", it->pid},
        });
    }

    Json capabilities = Json::array();
    for (std::vector<std::string>::const_iterator it = auth_context.capabilities.begin();
         it != auth_context.capabilities.end();
         ++it) {
        capabilities.push_back(*it);
    }

    Json quota_buckets = Json::array();
    for (std::vector<AuthQuotaBucket>::const_iterator it = auth_context.quota_buckets.begin();
         it != auth_context.quota_buckets.end();
         ++it) {
        Json methods = Json::array();
        for (std::vector<std::string>::const_iterator method_it = it->methods.begin(); method_it != it->methods.end();
             ++method_it) {
            methods.push_back(*method_it);
        }
        quota_buckets.push_back(Json{
            {"bucket", it->bucket},
            {"methods", methods},
            {"limit", it->limit},
            {"remaining", it->remaining},
            {"enforcement", it->enforcement},
        });
    }

    return Json{
        {"is_valid", auth_context.is_valid},
        {"account_type", ToAccountTypeString(auth_context.account_type)},
        {"account_type_code", auth_context.account_type_code},
        {"auth_scene", auth_context.auth_scene},
        {"license_mode", auth_context.license_mode},
        {"entitlement_state", auth_context.entitlement_state},
        {"machine_code", auth_context.machine_code},
        {"device_scope", device_scope},
        {"expires_at", auth_context.expires_at},
        {"capabilities", capabilities},
        {"quota_buckets", quota_buckets},
    };
}

Json CommandApplicationService::BuildDeviceJson(const SdkDeviceDescriptor& device) const {
    Json resolutions = Json::array();
    for (std::vector<SdkVideoResolution>::const_iterator it = device.resolutions.begin();
         it != device.resolutions.end();
         ++it) {
        resolutions.push_back(Json{
            {"width", it->width},
            {"height", it->height},
            {"real_width", it->real_width},
            {"real_height", it->real_height},
            {"fps", it->fps},
            {"pixel_format", it->pixel_format},
            {"is_default", it->is_default},
        });
    }

    return Json{
        {"device_id", device.device_id},
        {"model", device.model},
        {"display_name", device.display_name},
        {"vid", device.vid},
        {"pid", device.pid},
        {"status", device.status},
        {"authorized", device.authorized},
        {"supports_video", device.supports_video},
        {"features", Json{{"image_transfer_protocol", device.image_transfer_protocol}}},
        {"resolutions", resolutions},
    };
}

const CommandApplicationService::MethodDescriptor* CommandApplicationService::FindMethod(const std::string& method) const {
    for (std::vector<MethodDescriptor>::const_iterator it = methods_.begin(); it != methods_.end(); ++it) {
        if (it->method == method) {
            return &(*it);
        }
    }
    return NULL;
}

void CommandApplicationService::RememberOpenedDevice(const std::string& connection_id, const std::string& device_id) {
    if (connection_id.empty() || device_id.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(opened_devices_mu_);
    opened_devices_by_connection_[connection_id].insert(device_id);
}

void CommandApplicationService::ForgetOpenedDevice(const std::string& connection_id, const std::string& device_id) {
    std::lock_guard<std::mutex> lock(opened_devices_mu_);
    std::map<std::string, std::set<std::string> >::iterator it = opened_devices_by_connection_.find(connection_id);
    if (it == opened_devices_by_connection_.end()) {
        return;
    }
    it->second.erase(device_id);
    if (it->second.empty()) {
        opened_devices_by_connection_.erase(it);
    }
}

std::vector<std::string> CommandApplicationService::ClearOpenedDevices(const std::string& connection_id) {
    std::vector<std::string> devices;
    std::lock_guard<std::mutex> lock(opened_devices_mu_);
    std::map<std::string, std::set<std::string> >::iterator it = opened_devices_by_connection_.find(connection_id);
    if (it == opened_devices_by_connection_.end()) {
        return devices;
    }
    for (std::set<std::string>::const_iterator device_it = it->second.begin(); device_it != it->second.end(); ++device_it) {
        devices.push_back(*device_it);
    }
    opened_devices_by_connection_.erase(it);
    return devices;
}

} // namespace sdk
} // namespace editor
