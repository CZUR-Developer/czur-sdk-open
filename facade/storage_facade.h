// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "sdk_provider_bundle.h"

namespace editor {
namespace sdk {

class StorageFacade {
public:
    explicit StorageFacade(const ProviderBundle& providers);
    SdkStorageCleanupResult CleanupTemp(const SdkStorageCleanupRequest& request) const;

private:
    ProviderBundle providers_;
};

} // namespace sdk
} // namespace editor
