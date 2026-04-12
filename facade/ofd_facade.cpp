// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ofd_facade.h"

namespace editor {
namespace sdk {

OfdFacade::OfdFacade(const ProviderBundle& providers)
    : providers_(providers) {}

SdkFileConvertResult OfdFacade::Convert(const SdkFileConvertRequest& request) const {
    if (!providers_.ofd_provider) {
        SdkFileConvertResult result;
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "provider not ready";
        return result;
    }
    return providers_.ofd_provider->Convert(request);
}

} // namespace sdk
} // namespace editor
