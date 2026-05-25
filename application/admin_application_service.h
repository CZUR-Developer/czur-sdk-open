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
    using JsonSupplier = std::function<Json()>;
    using LogReadHandler = std::function<Json(const std::string&, std::size_t)>;
    using OfflineActivationHandler = std::function<Json(const std::string&, const Json&)>;

    explicit AdminApplicationService(std::shared_ptr<RuntimeConfigService> runtime_config = std::shared_ptr<RuntimeConfigService>());
    void SetStatusSupplier(JsonSupplier supplier);
    void SetSystemSupplier(JsonSupplier supplier);
    void SetAuthSupplier(JsonSupplier supplier);
    void SetLogsSupplier(JsonSupplier supplier);
    void SetLogReadHandler(LogReadHandler handler);
    void SetRecordsSupplier(JsonSupplier supplier);
    void SetOfflineActivationHandler(OfflineActivationHandler handler);
    Json BuildHealthJson() const;
    Json BuildStatusJson() const;
    Json BuildSystemJson() const;
    Json BuildAuthJson() const;
    Json BuildLogsJson() const;
    Json BuildLogReadJson(const std::string& log_id, std::size_t tail_bytes) const;
    Json BuildRecordsJson() const;
    Json ActivateOfflineSessionJson(const std::string& connection_id, const Json& request) const;
    Json BuildConfigJson() const;
    Json UpdateConfigJson(const Json& request) const;

private:
    JsonSupplier status_supplier_;
    JsonSupplier system_supplier_;
    JsonSupplier auth_supplier_;
    JsonSupplier logs_supplier_;
    LogReadHandler log_read_handler_;
    JsonSupplier records_supplier_;
    OfflineActivationHandler offline_activation_handler_;
    std::shared_ptr<RuntimeConfigService> runtime_config_;
};

} // namespace sdk
} // namespace editor
