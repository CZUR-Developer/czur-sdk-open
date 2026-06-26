// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "device_facade.h"

#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
#include <windows.h>

#include "sdk_json_utils.h"
#endif

namespace editor {
namespace sdk {

namespace {

#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)

typedef const char* (*PrivateDeviceJsonFn)(const char*);
typedef void (*PrivateDeviceFreeStringFn)(const char*);

struct PrivateDeviceCApi {
    HMODULE module = NULL;
    PrivateDeviceJsonFn list_devices = NULL;
    PrivateDeviceJsonFn get_device = NULL;
    PrivateDeviceFreeStringFn free_string = NULL;
};

PrivateDeviceCApi& GetPrivateDeviceCApi() {
    static PrivateDeviceCApi api;
    static bool loaded = false;
    if (loaded) {
        return api;
    }
    loaded = true;
    api.module = ::LoadLibraryA("sdk_private_providers.dll");
    if (api.module == NULL) {
        return api;
    }
    api.list_devices = reinterpret_cast<PrivateDeviceJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_device_list_json"));
    api.get_device = reinterpret_cast<PrivateDeviceJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_device_get_json"));
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

    const char* response_ptr = fn(request.dump().c_str());
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
        return result;
    }

    result.code = IntField(response, "code");
    result.message = StringField(response, "message");
    if (result.message.empty()) {
        result.message = IsOkStatusCode(result.code) ? "ok" : "private device failed";
    }
    if (!IsOkStatusCode(result.code)) {
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

    result = providers_.device_provider->OpenDevice(request);
    if (IsOkStatusCode(result.code)) {
        result.device.authorized = true;
    }
    return result;
}

SdkDeviceCloseResult DeviceFacade::CloseDevice(const AuthContext& auth_context, const SdkDeviceCloseRequest& request) const {
    SdkDeviceCloseResult result;
    const DeviceGetResult device_result = LookupDevice(auth_context, request.device_id);
    if (!IsOkStatusCode(device_result.code)) {
        if (device_result.code == ToCode(SdkStatusCode::DeviceNotFound) &&
            providers_.device_provider &&
            providers_.device_provider->IsDeviceRecentlyRemoved(request.device_id)) {
            return providers_.device_provider->CloseDevice(request);
        }
        result.code = device_result.code;
        result.message = device_result.message;
        return result;
    }
    return providers_.device_provider->CloseDevice(request);
}

void DeviceFacade::CaptureStill(const AuthContext& auth_context,
                                const SdkCaptureRequest& request,
                                SdkCaptureCallback callback) const {
    SdkCaptureResult result;
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
}

SdkVideoStartResult DeviceFacade::StartVideo(const AuthContext& auth_context,
                                             const SdkVideoStartRequest& request,
                                             SdkVideoFrameCallback callback) const {
    SdkVideoStartResult result;
    const DeviceGetResult device_result = LookupDevice(auth_context, request.device_id);
    if (!IsOkStatusCode(device_result.code)) {
        result.code = device_result.code;
        result.message = device_result.message;
        return result;
    }
    return providers_.device_provider->StartVideo(request, callback);
}

SdkVideoStopResult DeviceFacade::StopVideo(const AuthContext& auth_context, const SdkVideoStopRequest& request) const {
    SdkVideoStopResult result;
    const DeviceGetResult device_result = LookupDevice(auth_context, request.device_id);
    if (!IsOkStatusCode(device_result.code)) {
        if (device_result.code == ToCode(SdkStatusCode::DeviceNotFound) &&
            providers_.device_provider &&
            providers_.device_provider->IsDeviceRecentlyRemoved(request.device_id)) {
            return providers_.device_provider->StopVideo(request);
        }
        result.code = device_result.code;
        result.message = device_result.message;
        return result;
    }
    return providers_.device_provider->StopVideo(request);
}

SdkVideoFormatResult DeviceFacade::SetVideoFormat(const AuthContext& auth_context,
                                                  const SdkVideoFormatRequest& request) const {
    SdkVideoFormatResult result;
    const DeviceGetResult device_result = LookupDevice(auth_context, request.device_id);
    if (!IsOkStatusCode(device_result.code)) {
        result.code = device_result.code;
        result.message = device_result.message;
        return result;
    }
    return providers_.device_provider->SetVideoFormat(request);
}

SdkVideoProfileResult DeviceFacade::SetVideoProfile(const AuthContext& auth_context,
                                                    const SdkVideoProfileRequest& request) const {
    SdkVideoProfileResult result;
    const DeviceGetResult device_result = LookupDevice(auth_context, request.device_id);
    if (!IsOkStatusCode(device_result.code)) {
        result.code = device_result.code;
        result.message = device_result.message;
        return result;
    }
    return providers_.device_provider->SetVideoProfile(request);
}

SdkTurnDetectResult DeviceFacade::SetTurnDetect(const AuthContext& auth_context,
                                                const SdkTurnDetectRequest& request) const {
    SdkTurnDetectResult result;
    const DeviceGetResult device_result = LookupDevice(auth_context, request.device_id);
    if (!IsOkStatusCode(device_result.code)) {
        result.code = device_result.code;
        result.message = device_result.message;
        return result;
    }
    return providers_.device_provider->SetTurnDetect(request);
}

} // namespace sdk
} // namespace editor
