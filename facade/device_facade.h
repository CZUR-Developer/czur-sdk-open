// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

#include "sdk_auth_types.h"
#include "sdk_provider_bundle.h"
#include "sdk_provider_types.h"

namespace editor {
namespace sdk {

struct DeviceListResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    std::vector<SdkDeviceDescriptor> devices;
};

struct DeviceGetResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    SdkDeviceDescriptor device;
};

class DeviceFacade {
public:
    explicit DeviceFacade(const ProviderBundle& providers);

    DeviceListResult ListDevices(const AuthContext& auth_context) const;
    DeviceGetResult GetDevice(const AuthContext& auth_context, const std::string& device_id) const;
    SdkDeviceOpenResult OpenDevice(const AuthContext& auth_context, const std::string& device_id) const;
    SdkCaptureResult CaptureStill(const AuthContext& auth_context, const SdkCaptureRequest& request) const;
    SdkVideoStartResult StartVideo(const AuthContext& auth_context, const SdkVideoStartRequest& request) const;
    SdkVideoStopResult StopVideo(const AuthContext& auth_context, const SdkVideoStopRequest& request) const;
    SdkVideoFormatResult SetVideoFormat(const AuthContext& auth_context, const SdkVideoFormatRequest& request) const;

private:
    bool IsDeviceAuthorized(const AuthContext& auth_context, const SdkDeviceDescriptor& device) const;
    DeviceGetResult LookupDevice(const AuthContext& auth_context, const std::string& device_id) const;

    ProviderBundle providers_;
};

} // namespace sdk
} // namespace editor
