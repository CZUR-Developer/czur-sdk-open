// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

#include "sdk_provider_types.h"

namespace editor {
namespace sdk {

class ISdkDeviceProvider {
public:
    virtual ~ISdkDeviceProvider() = default;
    virtual std::string ProviderName() const = 0;
    virtual std::vector<SdkDeviceDescriptor> ListDevices() const = 0;
    virtual SdkDeviceOpenResult GetDevice(const SdkDeviceOpenRequest& request) = 0;
    virtual SdkDeviceOpenResult OpenDevice(const SdkDeviceOpenRequest& request) = 0;
    virtual SdkDeviceCloseResult CloseDevice(const SdkDeviceCloseRequest& request) = 0;
    virtual void CaptureStill(const SdkCaptureRequest& request, SdkCaptureCallback callback) = 0;
    virtual SdkVideoStartResult StartVideo(const SdkVideoStartRequest& request, SdkVideoFrameCallback callback) = 0;
    virtual SdkVideoStopResult StopVideo(const SdkVideoStopRequest& request) = 0;
    virtual SdkVideoFormatResult SetVideoFormat(const SdkVideoFormatRequest& request) = 0;
    virtual SdkVideoProfileResult SetVideoProfile(const SdkVideoProfileRequest& request) = 0;
    virtual bool IsDeviceRecentlyRemoved(const std::string&) const { return false; }
    virtual void SetDeviceActionEventSink(SdkDeviceActionEventCallback) {}
    virtual void SetDeviceEventSink(SdkDeviceEventCallback) {}
    virtual SdkTurnDetectResult SetTurnDetect(const SdkTurnDetectRequest&) {
        SdkTurnDetectResult result;
        result.code = ToCode(SdkStatusCode::UnsupportedMethod);
        result.message = "turn detect not supported";
        return result;
    }
};

} // namespace sdk
} // namespace editor
