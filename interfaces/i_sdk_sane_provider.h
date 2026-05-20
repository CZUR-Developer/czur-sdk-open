// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

#include "sdk_provider_types.h"

namespace editor {
namespace sdk {

class ISdkSaneProvider {
public:
    virtual ~ISdkSaneProvider() = default;
    virtual std::string ProviderName() const = 0;
    virtual void SetDeviceEventSink(SdkSaneDeviceEventCallback) {}
    virtual void SetScanTaskEventSink(SdkSaneScanTaskEventCallback) {}
    virtual SdkSaneStatusResult GetStatus() = 0;
    virtual SdkSaneListResult ListDevices(const SdkSaneListRequest& request) = 0;
    virtual SdkSaneWatchResult WatchStart(const SdkSaneWatchRequest& request) = 0;
    virtual SdkSaneWatchResult WatchStop(const SdkSaneWatchRequest& request) = 0;
    virtual SdkSaneOpenResult OpenDevice(const SdkSaneOpenRequest& request) = 0;
    virtual SdkSaneCloseResult CloseDevice(const SdkSaneCloseRequest& request) = 0;
    virtual SdkSaneGetOptionsResult GetOptions(const SdkSaneGetOptionsRequest& request) = 0;
    virtual SdkSaneSetOptionsResult SetOptions(const SdkSaneSetOptionsRequest& request) = 0;
    virtual SdkSaneProfileListResult ListProfiles(const SdkSaneProfileRequest& request) = 0;
    virtual SdkSaneProfileResult SaveProfile(const SdkSaneProfileRequest& request) = 0;
    virtual SdkSaneProfileResult ApplyProfile(const SdkSaneProfileRequest& request) = 0;
    virtual SdkSaneProfileResult DeleteProfile(const SdkSaneProfileRequest& request) = 0;
    virtual SdkSaneScanResult Scan(const SdkSaneScanRequest& request) = 0;
    virtual SdkSaneScanResult GetScan(const SdkSaneScanGetRequest& request) = 0;
    virtual SdkSaneScanResult CancelScan(const SdkSaneScanCancelRequest& request) = 0;
};

} // namespace sdk
} // namespace editor
