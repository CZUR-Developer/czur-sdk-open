// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_runtime_paths.h"

#include <cerrno>
#include <cstdlib>
#include <limits.h>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace editor {
namespace sdk {

namespace {

const char kSdkOpenWorkDirEnv[] = "SDK_OPEN_WORK_DIR";
const char kSdkOpenLogDirEnv[] = "SDK_OPEN_LOG_DIR";

bool IsAbsolutePath(const std::string& path) {
    return !path.empty() && path[0] == '/';
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

std::string ResolveSdkOpenWorkDir() {
    const char* override_dir = std::getenv(kSdkOpenWorkDirEnv);
    if (override_dir != NULL && override_dir[0] != '\0') {
        return std::string(override_dir);
    }

    const char* home = std::getenv("HOME");
    const std::string base = (home != NULL && home[0] != '\0') ? std::string(home) : CurrentWorkingDirectory();
    return JoinPath(JoinPath(base, ".czur"), "sdk");
}

} // namespace

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

const std::string& GetSdkOpenWorkDir() {
    static const std::string work_dir = ResolveSdkOpenWorkDir();
    return work_dir;
}

std::string ResolveSdkOpenLogDir() {
    const char* override_dir = std::getenv(kSdkOpenLogDirEnv);
    if (override_dir != NULL && override_dir[0] != '\0') {
        return std::string(override_dir);
    }
    return JoinPath(GetSdkOpenWorkDir(), "logs");
}

std::string GetSdkOpenTasksDir() {
    return JoinPath(GetSdkOpenWorkDir(), "tasks");
}

std::string GetSdkOpenTaskDir(const std::string& module, const std::string& task_id) {
    return JoinPath(JoinPath(GetSdkOpenTasksDir(), module), task_id);
}

std::string GetSdkOpenTaskAssetDir(const std::string& module,
                                   const std::string& task_id,
                                   const std::string& asset_group) {
    return JoinPath(GetSdkOpenTaskDir(module, task_id), asset_group);
}

std::string GetSdkOpenCaptureDir() {
    return JoinPath(GetSdkOpenWorkDir(), "capture");
}

std::string GetSdkOpenCaptureTaskDir(const std::string& task_id) {
    return JoinPath(GetSdkOpenCaptureDir(), task_id);
}

} // namespace sdk
} // namespace editor
