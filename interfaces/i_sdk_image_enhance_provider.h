// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

#include "sdk_provider_types.h"

namespace editor {
namespace sdk {

class ISdkImageEnhanceProvider {
public:
    virtual ~ISdkImageEnhanceProvider() = default;
    virtual std::string ProviderName() const = 0;
    virtual SdkImageEnhanceCapabilityResult ListCapabilities() = 0;
    virtual SdkImageEnhanceStepResult RunStep(const SdkImageEnhanceStepRequest& request) = 0;
};

} // namespace sdk
} // namespace editor
