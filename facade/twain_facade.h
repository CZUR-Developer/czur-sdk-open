// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "sdk_provider_bundle.h"

namespace editor {
namespace sdk {

class TwainFacade {
public:
    explicit TwainFacade(const ProviderBundle& providers);

    void SetSourceEventSink(SdkTwainSourceEventCallback sink);
    void SetScanTaskEventSink(SdkTwainScanTaskEventCallback sink);
    SdkTwainStatusResult GetStatus();
    SdkTwainListResult ListSources(const SdkTwainListRequest& request);
    SdkTwainWatchResult WatchStart(const SdkTwainWatchRequest& request);
    SdkTwainWatchResult WatchStop(const SdkTwainWatchRequest& request);
    SdkTwainOpenResult OpenSource(const SdkTwainOpenRequest& request);
    SdkTwainCloseResult CloseSource(const SdkTwainCloseRequest& request);
    SdkTwainGetCapabilitiesResult GetCapabilities(const SdkTwainGetCapabilitiesRequest& request);
    SdkTwainSetCapabilitiesResult SetCapabilities(const SdkTwainSetCapabilitiesRequest& request);
    SdkTwainProfileListResult ListProfiles(const SdkTwainProfileRequest& request);
    SdkTwainProfileResult SaveProfile(const SdkTwainProfileRequest& request);
    SdkTwainProfileResult ApplyProfile(const SdkTwainProfileRequest& request);
    SdkTwainProfileResult DeleteProfile(const SdkTwainProfileRequest& request);
    SdkTwainScanResult Scan(const SdkTwainScanRequest& request);
    SdkTwainScanResult GetScan(const SdkTwainScanGetRequest& request);
    SdkTwainScanResult CancelScan(const SdkTwainScanCancelRequest& request);

private:
    ProviderBundle providers_;
};

} // namespace sdk
} // namespace editor
