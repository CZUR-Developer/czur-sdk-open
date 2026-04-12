// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>

#include "sdk_json_utils.h"

namespace editor {
namespace sdk {

class AdminApplicationService {
public:
    using StatusSupplier = std::function<Json()>;

    void SetStatusSupplier(StatusSupplier supplier);
    Json BuildHealthJson() const;
    Json BuildStatusJson() const;

private:
    StatusSupplier status_supplier_;
};

} // namespace sdk
} // namespace editor
