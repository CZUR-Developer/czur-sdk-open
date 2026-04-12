// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "sdk_provider_bundle.h"
#include "sdk_provider_types.h"

namespace editor {
namespace sdk {

class GraphicFacade {
public:
    explicit GraphicFacade(const ProviderBundle& providers);

    SdkImageProcessResult Process(const SdkImageProcessRequest& request) const;

private:
    ProviderBundle providers_;
};

} // namespace sdk
} // namespace editor
