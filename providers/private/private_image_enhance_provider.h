// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "i_sdk_image_enhance_provider.h"

namespace editor {
namespace sdk {
namespace private_impl {

class PrivateImageEnhanceProvider : public ISdkImageEnhanceProvider {
public:
    std::string ProviderName() const override;
    SdkImageEnhanceCapabilityResult ListCapabilities() override;
    SdkImageEnhanceStepResult RunStep(const SdkImageEnhanceStepRequest& request) override;
};

} // namespace private_impl
} // namespace sdk
} // namespace editor
