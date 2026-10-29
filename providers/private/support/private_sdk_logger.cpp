// Copyright (c) 2026 CZUR Tech. All rights reserved.

#include "support/private_sdk_logger.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sys/stat.h>
#include <vector>

#include "private_sdk_support.h"
#include "spdlog/details/os.h"
#include "spdlog/sinks/rotating_file_sink.h"

namespace editor {
namespace sdk {
namespace private_impl {

namespace {

const char kLoggerName[] = "sdk_private.main";
const char kLogFileName[] = "sdk_private_main.log";
const std::size_t kMaxLogFileSize = 5 * 1024 * 1024;
const std::size_t kMaxLogFiles = 3;

std::shared_ptr<spdlog::logger> g_logger;
std::string g_log_path;
std::mutex g_logger_mu;

std::string JoinPathLocal(const std::string& left, const std::string& right) {
    if (left.empty()) {
        return right;
    }
    if (right.empty()) {
        return left;
    }
    if (left[left.size() - 1] == '/') {
        return left + right;
    }
    return left + "/" + right;
}

std::string ToLowerAscii(std::string value) {
    for (std::size_t index = 0; index < value.size(); ++index) {
        value[index] = static_cast<char>(std::tolower(static_cast<unsigned char>(value[index])));
    }
    return value;
}

spdlog::filename_t ToSpdlogFilename(const std::string& path) {
#if defined(_WIN32) && defined(SPDLOG_WCHAR_FILENAMES)
    spdlog::wmemory_buf_t wide_path;
    spdlog::details::os::utf8_to_wstrbuf(path, wide_path);
    return spdlog::filename_t(wide_path.data(), wide_path.size());
#else
    return path;
#endif
}

spdlog::level::level_enum ResolveLogLevel() {
    const char* value = std::getenv("SDK_PRIVATE_LOG_LEVEL");
    if (value != NULL && value[0] != '\0') {
        const std::string level = ToLowerAscii(value);
        if (level == "debug") {
            return spdlog::level::debug;
        }
        if (level == "warn" || level == "warning") {
            return spdlog::level::warn;
        }
        if (level == "error") {
            return spdlog::level::err;
        }
        if (level == "off") {
            return spdlog::level::off;
        }
    }
    return spdlog::level::info;
}

std::string Basename(const std::string& path) {
    const std::string::size_type pos = path.find_last_of('/');
    if (pos == std::string::npos) {
        return path;
    }
    return path.substr(pos + 1);
}

uint64_t FileSize(const std::string& path) {
    struct stat st;
    if (path.empty() || ::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        return 0;
    }
    return static_cast<uint64_t>(st.st_size);
}

std::string ShortDigest(const std::string& value) {
    const std::string digest = Sha256HexString(value);
    return digest.size() > 12 ? digest.substr(0, 12) : digest;
}

std::string PrefixVidPid(const std::string& device_id) {
    const std::string::size_type first = device_id.find(':');
    if (first == std::string::npos) {
        return "device";
    }
    const std::string::size_type second = device_id.find(':', first + 1);
    if (second == std::string::npos) {
        return device_id.substr(0, first);
    }
    return device_id.substr(0, second);
}

std::string ReplaceAll(std::string value, const std::string& needle, const std::string& replacement) {
    if (needle.empty()) {
        return value;
    }
    std::string::size_type pos = 0;
    while ((pos = value.find(needle, pos)) != std::string::npos) {
        value.replace(pos, needle.size(), replacement);
        pos += replacement.size();
    }
    return value;
}

} // namespace

void InitializeSdkPrivateLogger() {
    GetSdkPrivateLogger();
}

std::shared_ptr<spdlog::logger> GetSdkPrivateLogger() {
    std::lock_guard<std::mutex> lock(g_logger_mu);
    if (g_logger) {
        return g_logger;
    }

    const std::string log_dir = GetSdkRuntimeLogDir();
    std::vector<spdlog::sink_ptr> sinks;
    if (EnsureDirectoryRecursive(log_dir)) {
        g_log_path = JoinPathLocal(log_dir, kLogFileName);
        try {
            sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                ToSpdlogFilename(g_log_path), kMaxLogFileSize, kMaxLogFiles));
        } catch (const spdlog::spdlog_ex&) {
            g_log_path.clear();
        }
    }

    if (sinks.empty()) {
        g_logger = std::make_shared<spdlog::logger>(kLoggerName);
    } else {
        g_logger = std::make_shared<spdlog::logger>(kLoggerName, sinks.begin(), sinks.end());
    }
    g_logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [%n] %v");
    g_logger->set_level(ResolveLogLevel());
    g_logger->flush_on(spdlog::level::info);
    if (!g_log_path.empty()) {
        g_logger->info("sdk_private logger initialized log_path={}", g_log_path);
    }
    return g_logger;
}

std::string GetSdkPrivateLogPath() {
    InitializeSdkPrivateLogger();
    return g_log_path;
}

std::string SafeDeviceId(const std::string& device_id) {
    if (device_id.empty()) {
        return "empty";
    }
    return PrefixVidPid(device_id) + "#" + ShortDigest(device_id);
}

std::string SafePathSummary(const std::string& path) {
    if (path.empty()) {
        return "empty";
    }
    return Basename(path) + "(size=" + std::to_string(FileSize(path)) + ",id=" + ShortDigest(path) + ")";
}

std::string SafeMessage(const std::string& message) {
    std::string safe = message;
    safe = ReplaceAll(safe, "api_key", "api_key[redacted]");
    safe = ReplaceAll(safe, "token", "token[redacted]");
    safe = ReplaceAll(safe, "license", "license[redacted]");
    return safe;
}

} // namespace private_impl
} // namespace sdk
} // namespace editor
