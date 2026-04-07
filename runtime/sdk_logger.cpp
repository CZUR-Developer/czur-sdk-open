// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_logger.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <limits.h>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

#include "spdlog/sinks/rotating_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"

namespace editor {
namespace sdk {

namespace {

const char kSdkOpenLoggerName[] = "sdk_open.runtime";
const char kSdkOpenLogFileName[] = "sdk_open_runtime.log";
const size_t kSdkOpenMaxLogFileSize = 5 * 1024 * 1024;
const size_t kSdkOpenMaxLogFiles = 3;

std::shared_ptr<spdlog::logger> g_sdk_open_logger;
std::string g_sdk_open_log_dir;
std::string g_sdk_open_log_path;
std::mutex g_sdk_open_logger_mu;

bool IsAbsolutePath(const std::string& path) {
    return !path.empty() && path[0] == '/';
}

std::string JoinPath(const std::string& base, const std::string& leaf) {
    if (base.empty()) {
        return leaf;
    }
    if (leaf.empty()) {
        return base;
    }
    if (base[base.size() - 1] == '/') {
        return base + leaf;
    }
    return base + "/" + leaf;
}

bool DirectoryExists(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string CurrentWorkingDirectory() {
    char buffer[PATH_MAX];
    if (::getcwd(buffer, sizeof(buffer)) != NULL) {
        return std::string(buffer);
    }
    return ".";
}

bool EnsureDirectoryRecursive(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    if (DirectoryExists(path)) {
        return true;
    }

    std::string current;
    size_t pos = 0;
    if (IsAbsolutePath(path)) {
        current = "/";
        pos = 1;
    }

    while (pos <= path.size()) {
        const size_t slash = path.find('/', pos);
        const std::string part = path.substr(pos, slash == std::string::npos ? std::string::npos : slash - pos);
        if (!part.empty()) {
            if (!current.empty() && current[current.size() - 1] != '/') {
                current += "/";
            }
            current += part;
            if (!DirectoryExists(current)) {
                if (::mkdir(current.c_str(), 0755) != 0 && errno != EEXIST) {
                    return false;
                }
            }
        }
        if (slash == std::string::npos) {
            break;
        }
        pos = slash + 1;
    }

    return DirectoryExists(path);
}

std::string ToLowerAscii(const std::string& value) {
    std::string normalized = value;
    for (size_t i = 0; i < normalized.size(); ++i) {
        normalized[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(normalized[i])));
    }
    return normalized;
}

spdlog::level::level_enum ResolveSdkOpenLogLevel() {
    const char* override_level = std::getenv("SDK_OPEN_LOG_LEVEL");
    if (override_level != NULL && override_level[0] != '\0') {
        const std::string value = ToLowerAscii(override_level);
        if (value == "trace") {
            return spdlog::level::trace;
        }
        if (value == "debug") {
            return spdlog::level::debug;
        }
        if (value == "warn" || value == "warning") {
            return spdlog::level::warn;
        }
        if (value == "error") {
            return spdlog::level::err;
        }
        if (value == "critical") {
            return spdlog::level::critical;
        }
        if (value == "off") {
            return spdlog::level::off;
        }
        return spdlog::level::info;
    }

#ifdef NDEBUG
    return spdlog::level::info;
#else
    return spdlog::level::debug;
#endif
}

std::string ResolveSdkOpenLogDir() {
    const char* override_dir = std::getenv("SDK_OPEN_LOG_DIR");
    if (override_dir != NULL && override_dir[0] != '\0') {
        return std::string(override_dir);
    }

    const char* home = std::getenv("HOME");
    const std::string base = (home != NULL && home[0] != '\0') ? std::string(home) : CurrentWorkingDirectory();
    return JoinPath(JoinPath(base, ".czur"), "sdk");
}

std::shared_ptr<spdlog::logger> BuildSdkOpenLogger(std::string* init_warning) {
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    g_sdk_open_log_dir = ResolveSdkOpenLogDir();
    g_sdk_open_log_path.clear();
    if (EnsureDirectoryRecursive(g_sdk_open_log_dir)) {
        g_sdk_open_log_path = JoinPath(g_sdk_open_log_dir, kSdkOpenLogFileName);
        try {
            sinks.push_back(std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                g_sdk_open_log_path, kSdkOpenMaxLogFileSize, kSdkOpenMaxLogFiles));
        } catch (const spdlog::spdlog_ex& ex) {
            g_sdk_open_log_path.clear();
            if (init_warning != NULL) {
                *init_warning = ex.what();
            }
        }
    } else if (init_warning != NULL) {
        *init_warning = "failed to create log directory: " + g_sdk_open_log_dir + ", errno=" + std::strerror(errno);
    }

    std::shared_ptr<spdlog::logger> logger(
        new spdlog::logger(kSdkOpenLoggerName, sinks.begin(), sinks.end()));
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] [%n] %v");
    logger->set_level(ResolveSdkOpenLogLevel());
    logger->flush_on(spdlog::level::info);
    return logger;
}

} // namespace

void InitializeSdkOpenLogger() {
    GetSdkOpenLogger();
}

std::shared_ptr<spdlog::logger> GetSdkOpenLogger() {
    std::lock_guard<std::mutex> lock(g_sdk_open_logger_mu);
    if (g_sdk_open_logger) {
        return g_sdk_open_logger;
    }

    std::string init_warning;
    g_sdk_open_logger = BuildSdkOpenLogger(&init_warning);
    if (!g_sdk_open_log_path.empty()) {
        g_sdk_open_logger->info("[sdk_logger] initialized, log_path={}", g_sdk_open_log_path);
    } else if (!init_warning.empty()) {
        g_sdk_open_logger->warn("[sdk_logger] initialized without file sink, reason={}", init_warning);
    } else {
        g_sdk_open_logger->warn("[sdk_logger] initialized without file sink");
    }
    return g_sdk_open_logger;
}

const std::string& GetSdkOpenLogDir() {
    InitializeSdkOpenLogger();
    return g_sdk_open_log_dir;
}

const std::string& GetSdkOpenLogPath() {
    InitializeSdkOpenLogger();
    return g_sdk_open_log_path;
}

} // namespace sdk
} // namespace editor
