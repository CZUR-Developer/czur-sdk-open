// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

namespace editor {
namespace sdk {

class ISdkGraphicProvider {
public:
    virtual ~ISdkGraphicProvider() = default;
    virtual std::string ProviderName() const = 0;
    virtual bool ProcessSampleTask(const std::string& input_path, const std::string& output_path) = 0;
};

} // namespace sdk
} // namespace editor

