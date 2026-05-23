// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <memory>

#include "runtime_config_service.h"
#include "sdk_json_utils.h"

namespace editor {
namespace sdk {

class AdminApplicationService {
public:
    using StatusSupplier = std::function<Json()>;

    explicit AdminApplicationService(std::shared_ptr<RuntimeConfigService> runtime_config = std::shared_ptr<RuntimeConfigService>());
    void SetStatusSupplier(StatusSupplier supplier);
    Json BuildHealthJson() const;
    Json BuildStatusJson() const;
    Json BuildConfigJson() const;
    Json UpdateConfigJson(const Json& request) const;

private:
    StatusSupplier status_supplier_;
    std::shared_ptr<RuntimeConfigService> runtime_config_;
};

} // namespace sdk
} // namespace editor
