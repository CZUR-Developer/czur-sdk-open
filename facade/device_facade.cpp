// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "device_facade.h"

#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
#include <map>
#include <memory>
#include <mutex>

#include <windows.h>

#include "sdk_json_utils.h"
#include "sdk_logger.h"
#endif

namespace editor {
namespace sdk {

namespace {

#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)

typedef const char* (*PrivateDeviceJsonFn)(const char*);
typedef void (*PrivateDeviceVideoFrameCallback)(const char*, const unsigned char*, size_t, void*);
typedef const char* (*PrivateDeviceVideoStartJsonFn)(const char*, PrivateDeviceVideoFrameCallback, void*);
typedef void (*PrivateDeviceFreeStringFn)(const char*);

struct PrivateDeviceCApi {
    HMODULE module = NULL;
    PrivateDeviceJsonFn list_devices = NULL;
    PrivateDeviceJsonFn get_device = NULL;
    PrivateDeviceJsonFn open_device = NULL;
    PrivateDeviceJsonFn close_device = NULL;
    PrivateDeviceJsonFn capture_still = NULL;
    PrivateDeviceVideoStartJsonFn start_video = NULL;
    PrivateDeviceJsonFn stop_video = NULL;
    PrivateDeviceJsonFn set_video_profile = NULL;
    PrivateDeviceFreeStringFn free_string = NULL;
};

struct PrivateVideoCallbackContext {
    SdkVideoFrameCallback callback;
};

std::mutex& PrivateVideoCallbackContextsMutex() {
    static std::mutex mu;
    return mu;
}

std::map<std::string, std::shared_ptr<PrivateVideoCallbackContext> >& PrivateVideoCallbackContexts() {
    static std::map<std::string, std::shared_ptr<PrivateVideoCallbackContext> > contexts;
    return contexts;
}

PrivateVideoCallbackContext* RegisterPrivateVideoCallbackContext(const std::string& device_id,
                                                                 SdkVideoFrameCallback callback) {
    std::shared_ptr<PrivateVideoCallbackContext> context(new PrivateVideoCallbackContext);
    context->callback = callback;
    std::lock_guard<std::mutex> lock(PrivateVideoCallbackContextsMutex());
    PrivateVideoCallbackContexts()[device_id] = context;
    return context.get();
}

void UnregisterPrivateVideoCallbackContext(const std::string& device_id) {
    std::lock_guard<std::mutex> lock(PrivateVideoCallbackContextsMutex());
    PrivateVideoCallbackContexts().erase(device_id);
}

PrivateDeviceCApi& GetPrivateDeviceCApi() {
    static PrivateDeviceCApi api;
    static bool loaded = false;
    if (loaded) {
        return api;
    }
    loaded = true;
    api.module = ::LoadLibraryA("sdk_private_providers.dll");
    if (api.module == NULL) {
        SDK_OPEN_LOG_WARN("[device_facade] private provider dll load failed, error={}", ::GetLastError());
        return api;
    }
    api.list_devices = reinterpret_cast<PrivateDeviceJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_device_list_json"));
    api.get_device = reinterpret_cast<PrivateDeviceJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_device_get_json"));
    api.open_device = reinterpret_cast<PrivateDeviceJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_device_open_json"));
    api.close_device = reinterpret_cast<PrivateDeviceJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_device_close_json"));
    api.capture_still = reinterpret_cast<PrivateDeviceJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_device_capture_json"));
    api.start_video = reinterpret_cast<PrivateDeviceVideoStartJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_video_start_json"));
    api.stop_video = reinterpret_cast<PrivateDeviceJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_video_stop_json"));
    api.set_video_profile = reinterpret_cast<PrivateDeviceJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_video_profile_json"));
    api.free_string = reinterpret_cast<PrivateDeviceFreeStringFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_providers_free_string"));
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

bool BoolField(const Json& object, const char* key, bool fallback = false) {
    if (!object.is_object()) {
        return fallback;
    }
    Json::const_iterator it = object.find(key);
    return it != object.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

void PopulateDetectedRectsFromJson(const Json& value, std::vector<SdkRect4P>* rects);

SdkVideoResolution VideoResolutionFromJson(const Json& value) {
    SdkVideoResolution resolution;
    if (!value.is_object()) {
        return resolution;
    }
    resolution.width = IntField(value, "width");
    resolution.height = IntField(value, "height");
    resolution.real_width = IntField(value, "real_width");
    resolution.real_height = IntField(value, "real_height");
    resolution.fps = IntField(value, "fps");
    resolution.pixel_format = StringField(value, "pixel_format");
    if (resolution.pixel_format.empty()) {
        resolution.pixel_format = "mjpeg";
    }
    resolution.is_default = BoolField(value, "is_default");
    return resolution;
}

SdkDeviceDescriptor DeviceDescriptorFromJson(const Json& value) {
    SdkDeviceDescriptor device;
    if (!value.is_object()) {
        return device;
    }
    device.device_id = StringField(value, "device_id");
    device.model = StringField(value, "model");
    device.display_name = StringField(value, "display_name");
    device.vid = IntField(value, "vid");
    device.pid = IntField(value, "pid");
    device.status = StringField(value, "status");
    if (device.status.empty()) {
        device.status = "offline";
    }
    device.authorized = BoolField(value, "authorized");
    device.supports_video = BoolField(value, "supports_video");
    device.image_transfer_protocol = BoolField(value, "image_transfer_protocol");

    Json::const_iterator resolutions_it = value.find("resolutions");
    if (resolutions_it != value.end() && resolutions_it->is_array()) {
        for (Json::const_iterator it = resolutions_it->begin(); it != resolutions_it->end(); ++it) {
            device.resolutions.push_back(VideoResolutionFromJson(*it));
        }
    }
    return device;
}

bool InvokePrivateDeviceCApi(PrivateDeviceJsonFn fn,
                             const Json& request,
                             Json* response,
                             std::string* message) {
    PrivateDeviceCApi& api = GetPrivateDeviceCApi();
    if (fn == NULL || api.free_string == NULL) {
        if (message != NULL) {
            *message = "private device c api not ready";
        }
        return false;
    }

    const std::string request_text = request.dump();
    const char* response_ptr = fn(request_text.c_str());
    if (response_ptr == NULL) {
        if (message != NULL) {
            *message = "private device c api returned null";
        }
        return false;
    }
    const std::string response_text(response_ptr);
    api.free_string(response_ptr);

    std::string parse_error;
    if (!TryParseJson(response_text, response, &parse_error) || response == NULL || !response->is_object()) {
        if (message != NULL) {
            *message = "private device c api returned invalid json";
        }
        return false;
    }
    return true;
}

DeviceListResult ListProviderDevicesWithCApi() {
    DeviceListResult result;
    PrivateDeviceCApi& api = GetPrivateDeviceCApi();
    Json response;
    std::string error;
    if (!InvokePrivateDeviceCApi(api.list_devices, Json::object(), &response, &error)) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = error;
        SDK_OPEN_LOG_WARN("[device_facade] list provider devices failed, message={}", error);
        return result;
    }

    result.code = IntField(response, "code");
    result.message = StringField(response, "message");
    if (result.message.empty()) {
        result.message = IsOkStatusCode(result.code) ? "ok" : "private device failed";
    }
    if (!IsOkStatusCode(result.code)) {
        SDK_OPEN_LOG_WARN("[device_facade] list provider devices returned error, code={}, message={}",
                          result.code,
                          result.message);
        return result;
    }

    Json::const_iterator devices_it = response.find("devices");
    if (devices_it != response.end() && devices_it->is_array()) {
        for (Json::const_iterator it = devices_it->begin(); it != devices_it->end(); ++it) {
            result.devices.push_back(DeviceDescriptorFromJson(*it));
        }
    }
    return result;
}

DeviceGetResult GetProviderDeviceWithCApi(const std::string& device_id) {
    DeviceGetResult result;
    PrivateDeviceCApi& api = GetPrivateDeviceCApi();
    Json response;
    std::string error;
    if (!InvokePrivateDeviceCApi(api.get_device, Json{{"device_id", device_id}}, &response, &error)) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = error;
        return result;
    }

    result.code = IntField(response, "code");
    result.message = StringField(response, "message");
    if (result.message.empty()) {
        result.message = IsOkStatusCode(result.code) ? "ok" : "private device failed";
    }
    Json::const_iterator device_it = response.find("device");
    if (device_it != response.end()) {
        result.device = DeviceDescriptorFromJson(*device_it);
    }
    return result;
}

SdkDeviceOpenResult OpenProviderDeviceWithCApi(const SdkDeviceOpenRequest& request) {
    SdkDeviceOpenResult result;
    PrivateDeviceCApi& api = GetPrivateDeviceCApi();
    Json response;
    std::string error;
    Json request_json{{"device_id", request.device_id},
                      {"width", request.width},
                      {"height", request.height},
                      {"fps", request.fps},
                      {"pixel_format", request.pixel_format}};
    if (!InvokePrivateDeviceCApi(api.open_device, request_json, &response, &error)) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = error;
        return result;
    }

    result.code = IntField(response, "code");
    result.message = StringField(response, "message");
    if (result.message.empty()) {
        result.message = IsOkStatusCode(result.code) ? "ok" : "private device failed";
    }
    result.opened = BoolField(response, "opened");
    Json::const_iterator device_it = response.find("device");
    if (device_it != response.end()) {
        result.device = DeviceDescriptorFromJson(*device_it);
    }
    return result;
}

SdkDeviceCloseResult CloseProviderDeviceWithCApi(const SdkDeviceCloseRequest& request) {
    SdkDeviceCloseResult result;
    PrivateDeviceCApi& api = GetPrivateDeviceCApi();
    Json response;
    std::string error;
    if (!InvokePrivateDeviceCApi(api.close_device, Json{{"device_id", request.device_id}}, &response, &error)) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = error;
        return result;
    }

    result.code = IntField(response, "code");
    result.message = StringField(response, "message");
    if (result.message.empty()) {
        result.message = IsOkStatusCode(result.code) ? "ok" : "private device failed";
    }
    result.closed = BoolField(response, "closed");
    result.was_opened = BoolField(response, "was_opened");
    return result;
}

SdkCaptureResult CaptureProviderStillWithCApi(const SdkCaptureRequest& request) {
    SdkCaptureResult result;
    PrivateDeviceCApi& api = GetPrivateDeviceCApi();
    Json response;
    std::string error;
    Json request_json{{"device_id", request.device_id},
                      {"output_dir", request.output_dir},
                      {"include_base64", request.include_base64},
                      {"timeout_ms", request.timeout_ms}};
    if (!InvokePrivateDeviceCApi(api.capture_still, request_json, &response, &error)) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = error;
        return result;
    }

    result.code = IntField(response, "code");
    result.message = StringField(response, "message");
    if (result.message.empty()) {
        result.message = IsOkStatusCode(result.code) ? "ok" : "private device failed";
    }
    result.captured = BoolField(response, "captured");
    result.output_path = StringField(response, "output_path");
    result.original_path = StringField(response, "original_path");
    result.laser_path = StringField(response, "laser_path");
    result.content_type = StringField(response, "content_type");
    result.payload = StringField(response, "payload");
    result.width = IntField(response, "width");
    result.height = IntField(response, "height");
    result.dpi = IntField(response, "dpi");
    Json::const_iterator size_it = response.find("size");
    if (size_it != response.end() && size_it->is_number_unsigned()) {
        result.size = size_it->get<uint64_t>();
    }
    Json::const_iterator rects_it = response.find("detected_rects");
    if (rects_it != response.end()) {
        PopulateDetectedRectsFromJson(*rects_it, &result.detected_rects);
    }
    result.detected_rects_source_width = IntField(response, "detected_rects_source_width");
    result.detected_rects_source_height = IntField(response, "detected_rects_source_height");
    result.scan_device_type = IntField(response, "scan_device_type");
    return result;
}

void PopulateDetectedRectsFromJson(const Json& value, std::vector<SdkRect4P>* rects) {
    if (rects == NULL || !value.is_array()) {
        return;
    }
    rects->clear();
    for (Json::const_iterator it = value.begin(); it != value.end(); ++it) {
        if (!it->is_object()) {
            continue;
        }
        SdkRect4P rect;
        Json::const_iterator left_top = it->find("left_top");
        Json::const_iterator right_top = it->find("right_top");
        Json::const_iterator right_down = it->find("right_down");
        Json::const_iterator left_down = it->find("left_down");
        if (left_top != it->end() && left_top->is_object()) {
            rect.left_top.x = left_top->value("x", 0.0f);
            rect.left_top.y = left_top->value("y", 0.0f);
        }
        if (right_top != it->end() && right_top->is_object()) {
            rect.right_top.x = right_top->value("x", 0.0f);
            rect.right_top.y = right_top->value("y", 0.0f);
        }
        if (right_down != it->end() && right_down->is_object()) {
            rect.right_down.x = right_down->value("x", 0.0f);
            rect.right_down.y = right_down->value("y", 0.0f);
        }
        if (left_down != it->end() && left_down->is_object()) {
            rect.left_down.x = left_down->value("x", 0.0f);
            rect.left_down.y = left_down->value("y", 0.0f);
        }
        rects->push_back(rect);
    }
}

void CALLBACK PrivateVideoFrameThunk(const char* frame_json,
                                     const unsigned char* payload,
                                     size_t payload_size,
                                     void* user_data) {
    PrivateVideoCallbackContext* context = reinterpret_cast<PrivateVideoCallbackContext*>(user_data);
    if (context == NULL || !context->callback || frame_json == NULL || payload == NULL || payload_size == 0) {
        return;
    }

    Json frame_meta;
    std::string parse_error;
    if (!TryParseJson(frame_json, &frame_meta, &parse_error) || !frame_meta.is_object()) {
        return;
    }

    SdkVideoFrame frame;
    frame.device_id = StringField(frame_meta, "device_id");
    frame.stream_id = StringField(frame_meta, "stream_id");
    Json::const_iterator seq_it = frame_meta.find("frame_seq");
    if (seq_it != frame_meta.end() && seq_it->is_number_unsigned()) {
        frame.frame_seq = seq_it->get<uint64_t>();
    }
    Json::const_iterator timestamp_it = frame_meta.find("timestamp_ms");
    if (timestamp_it != frame_meta.end() && timestamp_it->is_number_integer()) {
        frame.timestamp_ms = timestamp_it->get<int64_t>();
    }
    frame.width = IntField(frame_meta, "width");
    frame.height = IntField(frame_meta, "height");
    frame.pixel_format = StringField(frame_meta, "pixel_format");
    if (frame.pixel_format.empty()) {
        frame.pixel_format = "mjpeg";
    }
    frame.detected_rects_source_width = IntField(frame_meta, "detected_rects_source_width");
    frame.detected_rects_source_height = IntField(frame_meta, "detected_rects_source_height");
    Json::const_iterator rects_it = frame_meta.find("detected_rects");
    if (rects_it != frame_meta.end()) {
        PopulateDetectedRectsFromJson(*rects_it, &frame.detected_rects);
    }
    frame.payload.assign(payload, payload + payload_size);
    context->callback(frame);
}

SdkVideoStartResult StartProviderVideoWithCApi(const SdkVideoStartRequest& request,
                                               SdkVideoFrameCallback callback) {
    SdkVideoStartResult result;
    PrivateDeviceCApi& api = GetPrivateDeviceCApi();
    if (api.start_video == NULL || api.free_string == NULL) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "private device c api not ready";
        return result;
    }

    Json request_json{{"device_id", request.device_id},
                      {"stream_id", request.stream_id},
                      {"width", request.width},
                      {"height", request.height},
                      {"fps", request.fps},
                      {"pixel_format", request.pixel_format},
                      {"page_processing", request.page_processing},
                      {"single_page_realtime_detect_rects", request.single_page_realtime_detect_rects},
                      {"single_page_multi_target_paging", request.single_page_multi_target_paging}};
    PrivateVideoCallbackContext* callback_context =
        RegisterPrivateVideoCallbackContext(request.device_id, callback);
    const std::string request_text = request_json.dump();
    const char* response_ptr = api.start_video(request_text.c_str(), PrivateVideoFrameThunk, callback_context);
    if (response_ptr == NULL) {
        UnregisterPrivateVideoCallbackContext(request.device_id);
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "private device c api returned null";
        return result;
    }
    const std::string response_text(response_ptr);
    api.free_string(response_ptr);

    Json response;
    std::string parse_error;
    if (!TryParseJson(response_text, &response, &parse_error) || !response.is_object()) {
        UnregisterPrivateVideoCallbackContext(request.device_id);
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "private device c api returned invalid json";
        return result;
    }

    result.code = IntField(response, "code");
    result.message = StringField(response, "message");
    if (result.message.empty()) {
        result.message = IsOkStatusCode(result.code) ? "ok" : "private device failed";
    }
    result.accepted = BoolField(response, "accepted");
    if (!IsOkStatusCode(result.code) || !result.accepted) {
        UnregisterPrivateVideoCallbackContext(request.device_id);
    }
    const std::string response_pixel_format = StringField(response, "pixel_format");
    if (!response_pixel_format.empty()) {
        result.pixel_format = response_pixel_format;
    }
    result.width = IntField(response, "width", result.width);
    result.height = IntField(response, "height", result.height);
    result.fps = IntField(response, "fps", result.fps);
    return result;
}

SdkVideoStopResult StopProviderVideoWithCApi(const SdkVideoStopRequest& request) {
    SdkVideoStopResult result;
    PrivateDeviceCApi& api = GetPrivateDeviceCApi();
    Json response;
    std::string error;
    if (!InvokePrivateDeviceCApi(api.stop_video, Json{{"device_id", request.device_id}}, &response, &error)) {
        UnregisterPrivateVideoCallbackContext(request.device_id);
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = error;
        return result;
    }
    UnregisterPrivateVideoCallbackContext(request.device_id);

    result.code = IntField(response, "code");
    result.message = StringField(response, "message");
    if (result.message.empty()) {
        result.message = IsOkStatusCode(result.code) ? "ok" : "private device failed";
    }
    result.stopped = BoolField(response, "stopped");
    return result;
}

SdkVideoProfileResult SetProviderVideoProfileWithCApi(const SdkVideoProfileRequest& request) {
    SdkVideoProfileResult result;
    PrivateDeviceCApi& api = GetPrivateDeviceCApi();
    Json response;
    std::string error;
    Json request_json{{"device_id", request.device_id},
                      {"page_processing", request.page_processing},
                      {"single_page_realtime_detect_rects", request.single_page_realtime_detect_rects},
                      {"single_page_multi_target_paging", request.single_page_multi_target_paging}};
    if (!InvokePrivateDeviceCApi(api.set_video_profile, request_json, &response, &error)) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = error;
        return result;
    }
    result.code = IntField(response, "code");
    result.message = StringField(response, "message");
    if (result.message.empty()) {
        result.message = IsOkStatusCode(result.code) ? "ok" : "private device failed";
    }
    result.applied = BoolField(response, "applied");
    result.page_processing = StringField(response, "page_processing");
    result.single_page_realtime_detect_rects = BoolField(response, "single_page_realtime_detect_rects");
    result.single_page_multi_target_paging = BoolField(response, "single_page_multi_target_paging");
    return result;
}

#endif

} // namespace

DeviceFacade::DeviceFacade(const ProviderBundle& providers)
    : providers_(providers) {}

bool DeviceFacade::IsDeviceAuthorized(const AuthContext& auth_context, const SdkDeviceDescriptor& device) const {
    if (auth_context.device_scope.empty()) {
        return true;
    }
    for (std::vector<SdkDeviceGrant>::const_iterator it = auth_context.device_scope.begin();
         it != auth_context.device_scope.end();
         ++it) {
        if (it->vid == device.vid && it->pid == device.pid) {
            return true;
        }
    }
    return false;
}

DeviceListResult DeviceFacade::ListDevices(const AuthContext& auth_context) const {
    DeviceListResult result;
#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
    const DeviceListResult provider_result = ListProviderDevicesWithCApi();
    if (!IsOkStatusCode(provider_result.code)) {
        return provider_result;
    }
    const std::vector<SdkDeviceDescriptor> devices = provider_result.devices;
#else
    if (!providers_.device_provider) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "provider not ready";
        return result;
    }

    const std::vector<SdkDeviceDescriptor> devices = providers_.device_provider->ListDevices();
#endif
    for (std::vector<SdkDeviceDescriptor>::const_iterator it = devices.begin(); it != devices.end(); ++it) {
        if (IsDeviceAuthorized(auth_context, *it)) {
            SdkDeviceDescriptor device = *it;
            device.authorized = true;
            result.devices.push_back(device);
        }
    }
    return result;
}

DeviceGetResult DeviceFacade::LookupDevice(const AuthContext& auth_context, const std::string& device_id) const {
    DeviceGetResult result;
#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
    const DeviceListResult provider_result = ListProviderDevicesWithCApi();
    if (!IsOkStatusCode(provider_result.code)) {
        result.code = provider_result.code;
        result.message = provider_result.message;
        return result;
    }
    const std::vector<SdkDeviceDescriptor> devices = provider_result.devices;
#else
    if (!providers_.device_provider) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "provider not ready";
        return result;
    }

    const std::vector<SdkDeviceDescriptor> devices = providers_.device_provider->ListDevices();
#endif
    for (std::vector<SdkDeviceDescriptor>::const_iterator it = devices.begin(); it != devices.end(); ++it) {
        if (it->device_id == device_id) {
            if (!IsDeviceAuthorized(auth_context, *it)) {
                result.code = ToCode(SdkStatusCode::DeviceNotInAuthScope);
                result.message = "device not in auth scope";
                return result;
            }
            result.device = *it;
            result.device.authorized = true;
            return result;
        }
    }

    result.code = ToCode(SdkStatusCode::DeviceNotFound);
    result.message = "device not found";
    return result;
}

DeviceGetResult DeviceFacade::GetDevice(const AuthContext& auth_context, const std::string& device_id) const {
    DeviceGetResult result = LookupDevice(auth_context, device_id);
    if (!IsOkStatusCode(result.code)) {
        return result;
    }

#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
    const DeviceGetResult provider_result = GetProviderDeviceWithCApi(device_id);
#else
    SdkDeviceOpenRequest request;
    request.device_id = device_id;
    const SdkDeviceOpenResult provider_result = providers_.device_provider->GetDevice(request);
#endif
    if (!IsOkStatusCode(provider_result.code)) {
        result.code = provider_result.code;
        result.message = provider_result.message;
        return result;
    }
    result.device = provider_result.device;
    result.device.authorized = true;
    return result;
}

SdkDeviceOpenResult DeviceFacade::OpenDevice(const AuthContext& auth_context, const SdkDeviceOpenRequest& request) const {
    SdkDeviceOpenResult result;
    const DeviceGetResult device_result = LookupDevice(auth_context, request.device_id);
    if (!IsOkStatusCode(device_result.code)) {
        result.code = device_result.code;
        result.message = device_result.message;
        return result;
    }

#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
    result = OpenProviderDeviceWithCApi(request);
#else
    result = providers_.device_provider->OpenDevice(request);
#endif
    if (IsOkStatusCode(result.code)) {
        result.device.authorized = true;
    }
    return result;
}

SdkDeviceCloseResult DeviceFacade::CloseDevice(const AuthContext& auth_context, const SdkDeviceCloseRequest& request) const {
    SdkDeviceCloseResult result;
    const DeviceGetResult device_result = LookupDevice(auth_context, request.device_id);
    if (!IsOkStatusCode(device_result.code)) {
        result.code = device_result.code;
        result.message = device_result.message;
        return result;
    }
#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
    return CloseProviderDeviceWithCApi(request);
#else
    return providers_.device_provider->CloseDevice(request);
#endif
}

void DeviceFacade::CaptureStill(const AuthContext& auth_context,
                                const SdkCaptureRequest& request,
                                SdkCaptureCallback callback) const {
    SdkCaptureResult result;
#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
    (void)auth_context;
    result = CaptureProviderStillWithCApi(request);
    if (callback) {
        callback(result);
    }
    return;
#else
    const DeviceGetResult device_result = LookupDevice(auth_context, request.device_id);
    if (!IsOkStatusCode(device_result.code)) {
        result.code = device_result.code;
        result.message = device_result.message;
        if (callback) {
            callback(result);
        }
        return;
    }
    providers_.device_provider->CaptureStill(request, callback);
#endif
}

SdkVideoStartResult DeviceFacade::StartVideo(const AuthContext& auth_context,
                                             const SdkVideoStartRequest& request,
                                             SdkVideoFrameCallback callback) const {
    SdkVideoStartResult result;
#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
    (void)auth_context;
    return StartProviderVideoWithCApi(request, callback);
#else
    const DeviceGetResult device_result = LookupDevice(auth_context, request.device_id);
    if (!IsOkStatusCode(device_result.code)) {
        result.code = device_result.code;
        result.message = device_result.message;
        return result;
    }
    return providers_.device_provider->StartVideo(request, callback);
#endif
}

SdkVideoStopResult DeviceFacade::StopVideo(const AuthContext& auth_context, const SdkVideoStopRequest& request) const {
    SdkVideoStopResult result;
#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
    (void)auth_context;
    return StopProviderVideoWithCApi(request);
#else
    const DeviceGetResult device_result = LookupDevice(auth_context, request.device_id);
    if (!IsOkStatusCode(device_result.code)) {
        result.code = device_result.code;
        result.message = device_result.message;
        return result;
    }
    return providers_.device_provider->StopVideo(request);
#endif
}

SdkVideoFormatResult DeviceFacade::SetVideoFormat(const AuthContext& auth_context,
                                                  const SdkVideoFormatRequest& request) const {
    SdkVideoFormatResult result;
#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
    (void)auth_context;
    (void)request;
    result.code = ToCode(SdkStatusCode::UnsupportedMethod);
    result.message = "video.set_format is not supported by the private Windows provider";
    return result;
#else
    const DeviceGetResult device_result = LookupDevice(auth_context, request.device_id);
    if (!IsOkStatusCode(device_result.code)) {
        result.code = device_result.code;
        result.message = device_result.message;
        return result;
    }
    try {
        return providers_.device_provider->SetVideoFormat(request);
    } catch (const std::exception& e) {
        result.code = ToCode(SdkStatusCode::InternalError);
        result.message = e.what();
    } catch (...) {
        result.code = ToCode(SdkStatusCode::InternalError);
        result.message = "video.set_format failed";
    }
    return result;
#endif
}

SdkVideoProfileResult DeviceFacade::SetVideoProfile(const AuthContext& auth_context,
                                                    const SdkVideoProfileRequest& request) const {
    SdkVideoProfileResult result;
#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
    (void)auth_context;
    return SetProviderVideoProfileWithCApi(request);
#else
    const DeviceGetResult device_result = LookupDevice(auth_context, request.device_id);
    if (!IsOkStatusCode(device_result.code)) {
        result.code = device_result.code;
        result.message = device_result.message;
        return result;
    }
    try {
        return providers_.device_provider->SetVideoProfile(request);
    } catch (const std::exception& e) {
        result.code = ToCode(SdkStatusCode::InternalError);
        result.message = e.what();
    } catch (...) {
        result.code = ToCode(SdkStatusCode::InternalError);
        result.message = "video.set_profile failed";
    }
    return result;
#endif
}

} // namespace sdk
} // namespace editor
