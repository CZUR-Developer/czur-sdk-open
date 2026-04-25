// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "device_facade.h"

namespace editor {
namespace sdk {

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
    if (!providers_.device_provider) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "provider not ready";
        return result;
    }

    const std::vector<SdkDeviceDescriptor> devices = providers_.device_provider->ListDevices();
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
    if (!providers_.device_provider) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "provider not ready";
        return result;
    }

    const std::vector<SdkDeviceDescriptor> devices = providers_.device_provider->ListDevices();
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

    SdkDeviceOpenRequest request;
    request.device_id = device_id;
    const SdkDeviceOpenResult provider_result = providers_.device_provider->GetDevice(request);
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

SdkCaptureResult DeviceFacade::CaptureStill(const AuthContext& auth_context, const SdkCaptureRequest& request) const {
    SdkCaptureResult result;
    const DeviceGetResult device_result = LookupDevice(auth_context, request.device_id);
    if (!IsOkStatusCode(device_result.code)) {
        result.code = device_result.code;
        result.message = device_result.message;
        return result;
    }
    return providers_.device_provider->CaptureStill(request);
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

} // namespace sdk
} // namespace editor
