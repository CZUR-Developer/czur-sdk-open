// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "sdk_provider_bundle.h"
#include "sdk_provider_types.h"

namespace editor {
namespace sdk {

class OfdFacade {
public:
    explicit OfdFacade(const ProviderBundle& providers);

    SdkFileConvertResult Convert(const SdkFileConvertRequest& request) const;

private:
    ProviderBundle providers_;
};

} // namespace sdk
} // namespace editor
