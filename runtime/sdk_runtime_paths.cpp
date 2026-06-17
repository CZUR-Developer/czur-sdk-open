// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_runtime_paths.h"

#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#if defined(_WIN32)
#include <direct.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace editor {
namespace sdk {

namespace {

const char kSdkOpenWorkDirEnv[] = "SDK_OPEN_WORK_DIR";
const char kSdkOpenLogDirEnv[] = "SDK_OPEN_LOG_DIR";
const char kCzurSdkRuntimeDirEnv[] = "CZUR_SDK_RUNTIME_DIR";
const char kSystemSdkOpenWorkDir[] = "/var/lib/czur/sdk-open";

bool IsAbsolutePath(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    if (path[0] == '/' || path[0] == '\\') {
        return true;
    }
    return path.size() >= 3 &&
           std::isalpha(static_cast<unsigned char>(path[0])) &&
           path[1] == ':' &&
           (path[2] == '/' || path[2] == '\\');
}

bool DirectoryExists(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

char PreferredPathSeparator(const std::string& path) {
    return path.find('\\') != std::string::npos ? '\\' : '/';
}

int MakeDirectory(const std::string& path) {
#if defined(_WIN32)
    return ::_mkdir(path.c_str());
#else
    return ::mkdir(path.c_str(), 0755);
#endif
}

std::string CurrentWorkingDirectory() {
#if defined(_WIN32)
    char buffer[_MAX_PATH] = {0};
    return ::_getcwd(buffer, _MAX_PATH) != NULL ? std::string(buffer) : ".";
#else
    char buffer[PATH_MAX] = {0};
    return ::getcwd(buffer, sizeof(buffer)) != NULL ? std::string(buffer) : ".";
#endif
}

std::string ResolveSdkOpenWorkDir() {
    const char* runtime_dir = std::getenv(kCzurSdkRuntimeDirEnv);
    if (runtime_dir != NULL && runtime_dir[0] != '\0') {
        return std::string(runtime_dir);
    }

    const char* override_dir = std::getenv(kSdkOpenWorkDirEnv);
    if (override_dir != NULL && override_dir[0] != '\0') {
        return std::string(override_dir);
    }

#if defined(_WIN32)
    const char* home = std::getenv("USERPROFILE");
#else
    if (::geteuid() == 0) {
        return kSystemSdkOpenWorkDir;
    }
    const char* home = std::getenv("HOME");
#endif
    if (home != NULL && home[0] != '\0') {
        return JoinPath(JoinPath(std::string(home), ".czur"), "sdk");
    }

#if !defined(_WIN32)
    return kSystemSdkOpenWorkDir;
#endif

    return JoinPath(JoinPath(CurrentWorkingDirectory(), ".czur"), "sdk");
}

} // namespace

std::string JoinPath(const std::string& base, const std::string& leaf) {
    if (base.empty()) {
        return leaf;
    }
    if (leaf.empty()) {
        return base;
    }
    if (base[base.size() - 1] == '/' || base[base.size() - 1] == '\\') {
        return base + leaf;
    }
    return base + PreferredPathSeparator(base) + leaf;
}

bool EnsureDirectoryRecursive(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    if (DirectoryExists(path)) {
        return true;
    }

    std::string current;
    std::size_t pos = 0;
    if (IsAbsolutePath(path)) {
#if defined(_WIN32)
        if (path.size() >= 3 &&
            std::isalpha(static_cast<unsigned char>(path[0])) &&
            path[1] == ':' &&
            (path[2] == '/' || path[2] == '\\')) {
            current = path.substr(0, 3);
            pos = 3;
        } else if (path.size() >= 2 && path[0] == '\\' && path[1] == '\\') {
            current = "\\\\";
            pos = 2;
        } else {
            current = path.substr(0, 1);
            pos = 1;
        }
#else
        current = "/";
        pos = 1;
#endif
    }

    while (pos <= path.size()) {
        const std::size_t next = path.find_first_of("/\\", pos);
        const std::string part = path.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
        if (!part.empty()) {
            current = current.empty() ? part : JoinPath(current, part);
            if (!DirectoryExists(current) && MakeDirectory(current) != 0 && !DirectoryExists(current)) {
                return false;
            }
        }
        if (next == std::string::npos) {
            break;
        }
        pos = next + 1;
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
