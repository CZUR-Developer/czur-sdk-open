// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "storage_facade.h"

namespace editor {
namespace sdk {

StorageFacade::StorageFacade(const ProviderBundle& providers) : providers_(providers) {}

SdkStorageCleanupResult StorageFacade::CleanupTemp(const SdkStorageCleanupRequest& request) const {
    // Open 层不直接操作文件系统，真实删除统一下沉到 storage provider。
    if (!providers_.storage_provider) {
        SdkStorageCleanupResult result;
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "storage provider is not available";
        return result;
    }
    return providers_.storage_provider->CleanupTemp(request);
}

} // namespace sdk
} // namespace editor
