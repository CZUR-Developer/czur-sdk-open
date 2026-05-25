// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "admin_application_service.h"

#include <ctime>

namespace editor {
namespace sdk {

AdminApplicationService::AdminApplicationService(std::shared_ptr<RuntimeConfigService> runtime_config)
    : runtime_config_(runtime_config) {}

void AdminApplicationService::SetStatusSupplier(JsonSupplier supplier) {
    status_supplier_ = supplier;
}

void AdminApplicationService::SetSystemSupplier(JsonSupplier supplier) {
    system_supplier_ = supplier;
}

void AdminApplicationService::SetAuthSupplier(JsonSupplier supplier) {
    auth_supplier_ = supplier;
}

void AdminApplicationService::SetLogsSupplier(JsonSupplier supplier) {
    logs_supplier_ = supplier;
}

void AdminApplicationService::SetLogReadHandler(LogReadHandler handler) {
    log_read_handler_ = handler;
}

void AdminApplicationService::SetRecordsSupplier(JsonSupplier supplier) {
    records_supplier_ = supplier;
}

void AdminApplicationService::SetOfflineActivationHandler(OfflineActivationHandler handler) {
    offline_activation_handler_ = handler;
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

Json AdminApplicationService::BuildSystemJson() const {
    return system_supplier_ ? system_supplier_() : Json::object();
}

Json AdminApplicationService::BuildAuthJson() const {
    return auth_supplier_ ? auth_supplier_() : Json::object();
}

Json AdminApplicationService::BuildLogsJson() const {
    return logs_supplier_ ? logs_supplier_() : Json::object();
}

Json AdminApplicationService::BuildLogReadJson(const std::string& log_id, std::size_t tail_bytes) const {
    return log_read_handler_ ? log_read_handler_(log_id, tail_bytes) : Json::object();
}

Json AdminApplicationService::BuildRecordsJson() const {
    return records_supplier_ ? records_supplier_() : Json::object();
}

Json AdminApplicationService::ActivateOfflineSessionJson(const std::string& connection_id, const Json& request) const {
    return offline_activation_handler_ ? offline_activation_handler_(connection_id, request)
                                       : BuildErrorBody(SdkStatusCode::InvalidMethod, "offline activation api unavailable");
}

Json AdminApplicationService::BuildConfigJson() const {
    return runtime_config_ ? runtime_config_->BuildConfigJson() : Json::object();
}

Json AdminApplicationService::UpdateConfigJson(const Json& request) const {
    return runtime_config_ ? runtime_config_->UpdateConfigJson(request) : Json::object();
}

} // namespace sdk
} // namespace editor
