// Copyright (c) 2026 CZUR Tech. All rights reserved.

#pragma once

#include <memory>
#include <string>

#include "spdlog/spdlog.h"

#if defined(SDK_OPEN_HARMONY)
#include "sdk_logger.h"
#endif

namespace editor {
namespace sdk {
namespace private_impl {

#if defined(SDK_OPEN_HARMONY)
inline void InitializeSdkPrivateLogger() {
    ::editor::sdk::InitializeSdkOpenLogger();
}

inline std::shared_ptr<spdlog::logger> GetSdkPrivateLogger() {
    return ::editor::sdk::GetSdkOpenLogger();
}

inline std::string GetSdkPrivateLogPath() {
    return ::editor::sdk::GetSdkOpenLogPath();
}

inline std::string SafeDeviceId(const std::string& device_id) {
    return device_id.empty() ? "empty" : device_id;
}

inline std::string SafePathSummary(const std::string& path) {
    if (path.empty()) {
        return "empty";
    }
    const std::string::size_type pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

inline std::string SafeMessage(const std::string& message) {
    return message;
}
#else
void InitializeSdkPrivateLogger();
std::shared_ptr<spdlog::logger> GetSdkPrivateLogger();
std::string GetSdkPrivateLogPath();
std::string SafeDeviceId(const std::string& device_id);
std::string SafePathSummary(const std::string& path);
std::string SafeMessage(const std::string& message);
#endif

} // namespace private_impl
} // namespace sdk
} // namespace editor

#define SDK_PRIVATE_LOG_DEBUG(...) SPDLOG_LOGGER_DEBUG(::editor::sdk::private_impl::GetSdkPrivateLogger(), __VA_ARGS__)
#define SDK_PRIVATE_LOG_INFO(...) SPDLOG_LOGGER_INFO(::editor::sdk::private_impl::GetSdkPrivateLogger(), __VA_ARGS__)
#define SDK_PRIVATE_LOG_WARN(...) SPDLOG_LOGGER_WARN(::editor::sdk::private_impl::GetSdkPrivateLogger(), __VA_ARGS__)
#define SDK_PRIVATE_LOG_ERROR(...) SPDLOG_LOGGER_ERROR(::editor::sdk::private_impl::GetSdkPrivateLogger(), __VA_ARGS__)
