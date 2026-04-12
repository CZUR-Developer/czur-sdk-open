// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

#include "sdk_provider_types.h"

namespace editor {
namespace sdk {

class ISdkOfdProvider {
public:
    virtual ~ISdkOfdProvider() = default;
    virtual std::string ProviderName() const = 0;
    virtual SdkFileConvertResult Convert(const SdkFileConvertRequest& request) = 0;
};

} // namespace sdk
} // namespace editor
