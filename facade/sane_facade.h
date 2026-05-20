// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "sdk_provider_bundle.h"

namespace editor {
namespace sdk {

class SaneFacade {
public:
    explicit SaneFacade(const ProviderBundle& providers);

    void SetDeviceEventSink(SdkSaneDeviceEventCallback sink);
    void SetScanTaskEventSink(SdkSaneScanTaskEventCallback sink);
    SdkSaneStatusResult GetStatus();
    SdkSaneListResult ListDevices(const SdkSaneListRequest& request);
    SdkSaneWatchResult WatchStart(const SdkSaneWatchRequest& request);
    SdkSaneWatchResult WatchStop(const SdkSaneWatchRequest& request);
    SdkSaneOpenResult OpenDevice(const SdkSaneOpenRequest& request);
    SdkSaneCloseResult CloseDevice(const SdkSaneCloseRequest& request);
    SdkSaneGetOptionsResult GetOptions(const SdkSaneGetOptionsRequest& request);
    SdkSaneSetOptionsResult SetOptions(const SdkSaneSetOptionsRequest& request);
    SdkSaneProfileListResult ListProfiles(const SdkSaneProfileRequest& request);
    SdkSaneProfileResult SaveProfile(const SdkSaneProfileRequest& request);
    SdkSaneProfileResult ApplyProfile(const SdkSaneProfileRequest& request);
    SdkSaneProfileResult DeleteProfile(const SdkSaneProfileRequest& request);
    SdkSaneScanResult Scan(const SdkSaneScanRequest& request);
    SdkSaneScanResult GetScan(const SdkSaneScanGetRequest& request);
    SdkSaneScanResult CancelScan(const SdkSaneScanCancelRequest& request);

private:
    ProviderBundle providers_;
};

} // namespace sdk
} // namespace editor
