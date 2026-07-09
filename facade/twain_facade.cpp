// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "twain_facade.h"

#include <utility>

#include "sdk_status_code.h"

namespace editor {
namespace sdk {

namespace {

template <typename Result>
Result MissingProviderResult() {
    Result result;
    result.code = ToCode(SdkStatusCode::TwainNotAvailable);
    result.message = "TWAIN is only available on Windows and the TWAIN provider is not ready";
    return result;
}

} // namespace

TwainFacade::TwainFacade(const ProviderBundle& providers)
    : providers_(providers) {}

void TwainFacade::SetSourceEventSink(SdkTwainSourceEventCallback sink) {
    if (providers_.twain_provider) {
        providers_.twain_provider->SetSourceEventSink(std::move(sink));
    }
}

void TwainFacade::SetScanTaskEventSink(SdkTwainScanTaskEventCallback sink) {
    if (providers_.twain_provider) {
        providers_.twain_provider->SetScanTaskEventSink(std::move(sink));
    }
}

SdkTwainStatusResult TwainFacade::GetStatus() {
    if (!providers_.twain_provider) {
        return MissingProviderResult<SdkTwainStatusResult>();
    }
    return providers_.twain_provider->GetStatus();
}

SdkTwainListResult TwainFacade::ListSources(const SdkTwainListRequest& request) {
    if (!providers_.twain_provider) {
        return MissingProviderResult<SdkTwainListResult>();
    }
    return providers_.twain_provider->ListSources(request);
}

SdkTwainWatchResult TwainFacade::WatchStart(const SdkTwainWatchRequest& request) {
    if (!providers_.twain_provider) {
        return MissingProviderResult<SdkTwainWatchResult>();
    }
    return providers_.twain_provider->WatchStart(request);
}

SdkTwainWatchResult TwainFacade::WatchStop(const SdkTwainWatchRequest& request) {
    if (!providers_.twain_provider) {
        return MissingProviderResult<SdkTwainWatchResult>();
    }
    return providers_.twain_provider->WatchStop(request);
}

SdkTwainOpenResult TwainFacade::OpenSource(const SdkTwainOpenRequest& request) {
    if (!providers_.twain_provider) {
        return MissingProviderResult<SdkTwainOpenResult>();
    }
    return providers_.twain_provider->OpenSource(request);
}

SdkTwainCloseResult TwainFacade::CloseSource(const SdkTwainCloseRequest& request) {
    if (!providers_.twain_provider) {
        return MissingProviderResult<SdkTwainCloseResult>();
    }
    return providers_.twain_provider->CloseSource(request);
}

SdkTwainGetCapabilitiesResult TwainFacade::GetCapabilities(const SdkTwainGetCapabilitiesRequest& request) {
    if (!providers_.twain_provider) {
        return MissingProviderResult<SdkTwainGetCapabilitiesResult>();
    }
    return providers_.twain_provider->GetCapabilities(request);
}

SdkTwainSetCapabilitiesResult TwainFacade::SetCapabilities(const SdkTwainSetCapabilitiesRequest& request) {
    if (!providers_.twain_provider) {
        return MissingProviderResult<SdkTwainSetCapabilitiesResult>();
    }
    return providers_.twain_provider->SetCapabilities(request);
}

SdkTwainProfileListResult TwainFacade::ListProfiles(const SdkTwainProfileRequest& request) {
    if (!providers_.twain_provider) {
        return MissingProviderResult<SdkTwainProfileListResult>();
    }
    return providers_.twain_provider->ListProfiles(request);
}

SdkTwainProfileResult TwainFacade::SaveProfile(const SdkTwainProfileRequest& request) {
    if (!providers_.twain_provider) {
        return MissingProviderResult<SdkTwainProfileResult>();
    }
    return providers_.twain_provider->SaveProfile(request);
}

SdkTwainProfileResult TwainFacade::ApplyProfile(const SdkTwainProfileRequest& request) {
    if (!providers_.twain_provider) {
        return MissingProviderResult<SdkTwainProfileResult>();
    }
    return providers_.twain_provider->ApplyProfile(request);
}

SdkTwainProfileResult TwainFacade::DeleteProfile(const SdkTwainProfileRequest& request) {
    if (!providers_.twain_provider) {
        return MissingProviderResult<SdkTwainProfileResult>();
    }
    return providers_.twain_provider->DeleteProfile(request);
}

SdkTwainScanResult TwainFacade::Scan(const SdkTwainScanRequest& request) {
    if (!providers_.twain_provider) {
        return MissingProviderResult<SdkTwainScanResult>();
    }
    return providers_.twain_provider->Scan(request);
}

SdkTwainScanResult TwainFacade::GetScan(const SdkTwainScanGetRequest& request) {
    if (!providers_.twain_provider) {
        return MissingProviderResult<SdkTwainScanResult>();
    }
    return providers_.twain_provider->GetScan(request);
}

SdkTwainScanResult TwainFacade::CancelScan(const SdkTwainScanCancelRequest& request) {
    if (!providers_.twain_provider) {
        return MissingProviderResult<SdkTwainScanResult>();
    }
    return providers_.twain_provider->CancelScan(request);
}

} // namespace sdk
} // namespace editor
