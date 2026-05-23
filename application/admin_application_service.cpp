// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "admin_application_service.h"

#include <ctime>

namespace editor {
namespace sdk {

AdminApplicationService::AdminApplicationService(std::shared_ptr<RuntimeConfigService> runtime_config)
    : runtime_config_(runtime_config) {}

void AdminApplicationService::SetStatusSupplier(StatusSupplier supplier) {
    status_supplier_ = supplier;
}

Json AdminApplicationService::BuildHealthJson() const {
    return Json{
        {"ok", true},
        {"service", "sdk_open"},
        {"ts", static_cast<std::int64_t>(std::time(NULL))},
    };
}

Json AdminApplicationService::BuildStatusJson() const {
    return status_supplier_ ? status_supplier_() : Json::object();
}

Json AdminApplicationService::BuildConfigJson() const {
    return runtime_config_ ? runtime_config_->BuildConfigJson() : Json::object();
}

Json AdminApplicationService::UpdateConfigJson(const Json& request) const {
    return runtime_config_ ? runtime_config_->UpdateConfigJson(request) : Json::object();
}

} // namespace sdk
} // namespace editor
