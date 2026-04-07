// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <string>

#include "spdlog/spdlog.h"

namespace editor {
namespace sdk {

void InitializeSdkOpenLogger();
std::shared_ptr<spdlog::logger> GetSdkOpenLogger();
const std::string& GetSdkOpenLogDir();
const std::string& GetSdkOpenLogPath();

} // namespace sdk
} // namespace editor

#define SDK_OPEN_LOG_TRACE(...) SPDLOG_LOGGER_TRACE(::editor::sdk::GetSdkOpenLogger(), __VA_ARGS__)
#define SDK_OPEN_LOG_DEBUG(...) SPDLOG_LOGGER_DEBUG(::editor::sdk::GetSdkOpenLogger(), __VA_ARGS__)
#define SDK_OPEN_LOG_INFO(...) SPDLOG_LOGGER_INFO(::editor::sdk::GetSdkOpenLogger(), __VA_ARGS__)
#define SDK_OPEN_LOG_WARN(...) SPDLOG_LOGGER_WARN(::editor::sdk::GetSdkOpenLogger(), __VA_ARGS__)
#define SDK_OPEN_LOG_ERROR(...) SPDLOG_LOGGER_ERROR(::editor::sdk::GetSdkOpenLogger(), __VA_ARGS__)
#define SDK_OPEN_LOG_CRITICAL(...) SPDLOG_LOGGER_CRITICAL(::editor::sdk::GetSdkOpenLogger(), __VA_ARGS__)
