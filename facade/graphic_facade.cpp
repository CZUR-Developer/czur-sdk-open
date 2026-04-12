// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "graphic_facade.h"

namespace editor {
namespace sdk {

GraphicFacade::GraphicFacade(const ProviderBundle& providers)
    : providers_(providers) {}

SdkImageProcessResult GraphicFacade::Process(const SdkImageProcessRequest& request) const {
    if (!providers_.graphic_provider) {
        SdkImageProcessResult result;
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "provider not ready";
        return result;
    }
    return providers_.graphic_provider->Process(request);
}

} // namespace sdk
} // namespace editor
