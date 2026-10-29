#include "private_sdk_support.h"

#include <cerrno>
#include <cstdlib>
#include <sys/stat.h>

namespace editor {
namespace sdk {
namespace private_impl {

namespace {

bool DirectoryExists(const std::string& path) {
    struct stat value;
    return !path.empty() && ::stat(path.c_str(), &value) == 0 && S_ISDIR(value.st_mode);
}

std::string ParentDir(const std::string& path) {
    const std::string::size_type pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return std::string();
    }
    return pos == 0 ? "/" : path.substr(0, pos);
}

std::string JoinPathLocal(const std::string& left, const std::string& right) {
    if (left.empty()) {
        return right;
    }
    if (right.empty()) {
        return left;
    }
    return left[left.size() - 1] == '/' ? left + right : left + "/" + right;
}

} // namespace

std::string GetSdkRuntimeDir() {
    const char* work_dir = std::getenv("SDK_OPEN_WORK_DIR");
    return work_dir != NULL && work_dir[0] != '\0' ? std::string(work_dir) : std::string(".");
}

std::string GetSdkRuntimeLogDir() {
    const char* log_dir = std::getenv("SDK_OPEN_LOG_DIR");
    return log_dir != NULL && log_dir[0] != '\0'
               ? std::string(log_dir)
               : JoinPathLocal(GetSdkRuntimeDir(), "logs");
}

std::string GetSdkDeviceRuntimeDir(const std::string& device_id) {
    return JoinPathLocal(JoinPathLocal(GetSdkRuntimeDir(), "devices"), device_id);
}

std::string GetSdkDeviceLaserConfigPath(const std::string& device_id) {
    return JoinPathLocal(JoinPathLocal(GetSdkDeviceRuntimeDir(device_id), "laser"), "config.data");
}

bool EnsureDirectoryRecursive(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    if (DirectoryExists(path)) {
        return true;
    }
    const std::string parent = ParentDir(path);
    if (!parent.empty() && parent != path && !EnsureDirectoryRecursive(parent)) {
        return false;
    }
    return ::mkdir(path.c_str(), 0755) == 0 || (errno == EEXIST && DirectoryExists(path));
}

} // namespace private_impl
} // namespace sdk
} // namespace editor
