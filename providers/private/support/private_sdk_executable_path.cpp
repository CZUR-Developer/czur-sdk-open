// Copyright (c) 2026 CZUR Tech. All rights reserved.

#include "support/private_sdk_executable_path.h"

#include <cstdlib>
#include <limits.h>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

namespace editor {
namespace sdk {
namespace private_impl {

namespace {

std::string ParentDir(const std::string& path) {
    const std::string::size_type pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return ".";
    }
    if (pos == 0) {
        return path.substr(0, 1);
    }
    return path.substr(0, pos);
}

} // namespace

std::string GetSdkExecutableDir() {
#if defined(_WIN32)
    char buffer[MAX_PATH] = {0};
    const DWORD len = ::GetModuleFileNameA(NULL, buffer, static_cast<DWORD>(sizeof(buffer)));
    if (len == 0 || len >= sizeof(buffer)) {
        return ".";
    }
    return ParentDir(std::string(buffer, len));
#elif defined(__APPLE__)
    uint32_t size = 0;
    (void)::_NSGetExecutablePath(NULL, &size);
    if (size == 0) {
        return ".";
    }

    std::vector<char> buffer(size + 1, '\0');
    if (::_NSGetExecutablePath(&buffer[0], &size) != 0) {
        return ".";
    }

    char resolved[PATH_MAX] = {0};
    if (::realpath(&buffer[0], resolved) != NULL) {
        return ParentDir(resolved);
    }
    return ParentDir(&buffer[0]);
#else
    char buffer[PATH_MAX] = {0};
    const ssize_t len = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len <= 0) {
        return ".";
    }
    buffer[len] = '\0';
    return ParentDir(buffer);
#endif
}

} // namespace private_impl
} // namespace sdk
} // namespace editor
