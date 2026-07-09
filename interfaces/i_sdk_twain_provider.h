// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

#include "sdk_provider_types.h"

namespace editor {
namespace sdk {

class ISdkTwainProvider {
public:
    virtual ~ISdkTwainProvider() = default;
    virtual std::string ProviderName() const = 0;
    virtual void SetSourceEventSink(SdkTwainSourceEventCallback) {}
    virtual void SetScanTaskEventSink(SdkTwainScanTaskEventCallback) {}
    virtual SdkTwainStatusResult GetStatus() = 0;
    virtual SdkTwainListResult ListSources(const SdkTwainListRequest& request) = 0;
    virtual SdkTwainWatchResult WatchStart(const SdkTwainWatchRequest& request) = 0;
    virtual SdkTwainWatchResult WatchStop(const SdkTwainWatchRequest& request) = 0;
    virtual SdkTwainOpenResult OpenSource(const SdkTwainOpenRequest& request) = 0;
    virtual SdkTwainCloseResult CloseSource(const SdkTwainCloseRequest& request) = 0;
    virtual SdkTwainGetCapabilitiesResult GetCapabilities(const SdkTwainGetCapabilitiesRequest& request) = 0;
    virtual SdkTwainSetCapabilitiesResult SetCapabilities(const SdkTwainSetCapabilitiesRequest& request) = 0;
    virtual SdkTwainProfileListResult ListProfiles(const SdkTwainProfileRequest& request) = 0;
    virtual SdkTwainProfileResult SaveProfile(const SdkTwainProfileRequest& request) = 0;
    virtual SdkTwainProfileResult ApplyProfile(const SdkTwainProfileRequest& request) = 0;
    virtual SdkTwainProfileResult DeleteProfile(const SdkTwainProfileRequest& request) = 0;
    virtual SdkTwainScanResult Scan(const SdkTwainScanRequest& request) = 0;
    virtual SdkTwainScanResult GetScan(const SdkTwainScanGetRequest& request) = 0;
    virtual SdkTwainScanResult CancelScan(const SdkTwainScanCancelRequest& request) = 0;
};

} // namespace sdk
} // namespace editor
