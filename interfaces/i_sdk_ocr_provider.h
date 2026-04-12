// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

#include "sdk_provider_types.h"

namespace editor {
namespace sdk {

class ISdkOcrProvider {
public:
    virtual ~ISdkOcrProvider() = default;
    virtual std::string ProviderName() const = 0;
    virtual SdkOcrRecognizeResult Recognize(const SdkOcrRecognizeRequest& request) = 0;
};

} // namespace sdk
} // namespace editor
