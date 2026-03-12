// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

namespace editor {
namespace sdk {

class ISdkOfdProvider {
public:
    virtual ~ISdkOfdProvider() = default;
    virtual std::string ProviderName() const = 0;
    virtual bool OpenDocument(const std::string& path) = 0;
};

} // namespace sdk
} // namespace editor

