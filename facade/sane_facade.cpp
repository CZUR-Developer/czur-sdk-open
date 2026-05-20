// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sane_facade.h"

#include <utility>

#include "sdk_status_code.h"

namespace editor {
namespace sdk {

namespace {

template <typename Result>
Result MissingProviderResult() {
    Result result;
    result.code = ToCode(SdkStatusCode::SaneNotAvailable);
    result.message = "SANE is only available on Linux and the SANE provider is not ready";
    return result;
}

} // namespace

SaneFacade::SaneFacade(const ProviderBundle& providers)
    : providers_(providers) {}

void SaneFacade::SetDeviceEventSink(SdkSaneDeviceEventCallback sink) {
    if (providers_.sane_provider) {
        providers_.sane_provider->SetDeviceEventSink(std::move(sink));
    }
}

void SaneFacade::SetScanTaskEventSink(SdkSaneScanTaskEventCallback sink) {
    if (providers_.sane_provider) {
        providers_.sane_provider->SetScanTaskEventSink(std::move(sink));
    }
}

SdkSaneStatusResult SaneFacade::GetStatus() {
    if (!providers_.sane_provider) {
        return MissingProviderResult<SdkSaneStatusResult>();
    }
    return providers_.sane_provider->GetStatus();
}

SdkSaneListResult SaneFacade::ListDevices(const SdkSaneListRequest& request) {
    if (!providers_.sane_provider) {
        return MissingProviderResult<SdkSaneListResult>();
    }
    return providers_.sane_provider->ListDevices(request);
}

SdkSaneWatchResult SaneFacade::WatchStart(const SdkSaneWatchRequest& request) {
    if (!providers_.sane_provider) {
        return MissingProviderResult<SdkSaneWatchResult>();
    }
    return providers_.sane_provider->WatchStart(request);
}

SdkSaneWatchResult SaneFacade::WatchStop(const SdkSaneWatchRequest& request) {
    if (!providers_.sane_provider) {
        return MissingProviderResult<SdkSaneWatchResult>();
    }
    return providers_.sane_provider->WatchStop(request);
}

SdkSaneOpenResult SaneFacade::OpenDevice(const SdkSaneOpenRequest& request) {
    if (!providers_.sane_provider) {
        return MissingProviderResult<SdkSaneOpenResult>();
    }
    return providers_.sane_provider->OpenDevice(request);
}

SdkSaneCloseResult SaneFacade::CloseDevice(const SdkSaneCloseRequest& request) {
    if (!providers_.sane_provider) {
        return MissingProviderResult<SdkSaneCloseResult>();
    }
    return providers_.sane_provider->CloseDevice(request);
}

SdkSaneGetOptionsResult SaneFacade::GetOptions(const SdkSaneGetOptionsRequest& request) {
    if (!providers_.sane_provider) {
        return MissingProviderResult<SdkSaneGetOptionsResult>();
    }
    return providers_.sane_provider->GetOptions(request);
}

SdkSaneSetOptionsResult SaneFacade::SetOptions(const SdkSaneSetOptionsRequest& request) {
    if (!providers_.sane_provider) {
        return MissingProviderResult<SdkSaneSetOptionsResult>();
    }
    return providers_.sane_provider->SetOptions(request);
}

SdkSaneProfileListResult SaneFacade::ListProfiles(const SdkSaneProfileRequest& request) {
    if (!providers_.sane_provider) {
        return MissingProviderResult<SdkSaneProfileListResult>();
    }
    return providers_.sane_provider->ListProfiles(request);
}

SdkSaneProfileResult SaneFacade::SaveProfile(const SdkSaneProfileRequest& request) {
    if (!providers_.sane_provider) {
        return MissingProviderResult<SdkSaneProfileResult>();
    }
    return providers_.sane_provider->SaveProfile(request);
}

SdkSaneProfileResult SaneFacade::ApplyProfile(const SdkSaneProfileRequest& request) {
    if (!providers_.sane_provider) {
        return MissingProviderResult<SdkSaneProfileResult>();
    }
    return providers_.sane_provider->ApplyProfile(request);
}

SdkSaneProfileResult SaneFacade::DeleteProfile(const SdkSaneProfileRequest& request) {
    if (!providers_.sane_provider) {
        return MissingProviderResult<SdkSaneProfileResult>();
    }
    return providers_.sane_provider->DeleteProfile(request);
}

SdkSaneScanResult SaneFacade::Scan(const SdkSaneScanRequest& request) {
    if (!providers_.sane_provider) {
        return MissingProviderResult<SdkSaneScanResult>();
    }
    return providers_.sane_provider->Scan(request);
}

SdkSaneScanResult SaneFacade::GetScan(const SdkSaneScanGetRequest& request) {
    if (!providers_.sane_provider) {
        return MissingProviderResult<SdkSaneScanResult>();
    }
    return providers_.sane_provider->GetScan(request);
}

SdkSaneScanResult SaneFacade::CancelScan(const SdkSaneScanCancelRequest& request) {
    if (!providers_.sane_provider) {
        return MissingProviderResult<SdkSaneScanResult>();
    }
    return providers_.sane_provider->CancelScan(request);
}

} // namespace sdk
} // namespace editor
