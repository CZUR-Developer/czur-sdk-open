#include "private_storage_cleanup.h"

#include <cerrno>
#include <cctype>
#include <cstdio>
#include <sys/stat.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

namespace editor {
namespace sdk {
namespace private_impl {

namespace {

bool IsSeparator(char ch) {
    return ch == '/' || ch == '\\';
}

bool HasParentComponent(const std::string& path) {
    // 在路径规范化前拒绝任何“..”分量，避免调用者把清理范围逃逸到工作目录之外。
    std::string part;
    for (std::size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || IsSeparator(path[i])) {
            if (part == "..") {
                return true;
            }
            part.clear();
        } else {
            part.push_back(path[i]);
        }
    }
    return false;
}

bool IsRootPath(const std::string& path) {
    if (path.empty()) {
        return true;
    }
    bool separators_only = true;
    for (std::size_t i = 0; i < path.size(); ++i) {
        separators_only = separators_only && IsSeparator(path[i]);
    }
    if (separators_only) {
        return true;
    }
#if defined(_WIN32)
    if (path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':') {
        for (std::size_t i = 2; i < path.size(); ++i) {
            if (!IsSeparator(path[i])) {
                return false;
            }
        }
        return true;
    }
#endif
    return false;
}

bool NormalizeWorkDir(const std::string& input, std::string* output) {
    if (output == NULL || input.empty() || HasParentComponent(input)) {
        return false;
    }
#if defined(_WIN32)
    // Windows 仅接受普通盘符绝对路径；设备路径和盘符根目录都不允许作为清理根目录。
    if (input.size() < 3 || !std::isalpha(static_cast<unsigned char>(input[0])) ||
        input[1] != ':' || !IsSeparator(input[2]) ||
        input.find("\\\\?\\") == 0 || input.find("\\\\.\\") == 0) {
        return false;
    }
    const DWORD size = ::GetFullPathNameA(input.c_str(), 0, NULL, NULL);
    if (size == 0) {
        return false;
    }
    std::string normalized(size, '\0');
    const DWORD written = ::GetFullPathNameA(input.c_str(), size, &normalized[0], NULL);
    if (written == 0 || written >= size) {
        return false;
    }
    normalized.resize(written);
    *output = normalized;
#else
    // POSIX 同样要求绝对路径，并在本地折叠“.”和重复分隔符后再次检查根目录。
    if (input[0] != '/') {
        return false;
    }
    std::string normalized;
    std::string part;
    for (std::size_t i = 1; i <= input.size(); ++i) {
        if (i == input.size() || IsSeparator(input[i])) {
            if (!part.empty() && part != ".") {
                normalized += "/" + part;
            }
            part.clear();
        } else {
            part.push_back(input[i]);
        }
    }
    *output = normalized.empty() ? "/" : normalized;
#endif
    return !IsRootPath(*output);
}

std::string Join(const std::string& base, const char* leaf) {
    if (!base.empty() && IsSeparator(base[base.size() - 1])) {
        return base + leaf;
    }
#if defined(_WIN32)
    return base + "\\" + leaf;
#else
    return base + "/" + leaf;
#endif
}

#if defined(_WIN32)
bool RemoveTree(const std::string& path, bool* existed) {
    const DWORD attributes = ::GetFileAttributesA(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (::GetLastError() == ERROR_FILE_NOT_FOUND || ::GetLastError() == ERROR_PATH_NOT_FOUND) {
            return true;
        }
        return false;
    }
    *existed = true;
    // reparse point 可能指向工作目录之外，清理流程一律拒绝跟随。
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        return false;
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return false;
    }

    WIN32_FIND_DATAA data;
    const std::string pattern = Join(path, "*");
    HANDLE find = ::FindFirstFileA(pattern.c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            const std::string name(data.cFileName);
            if (name == "." || name == "..") {
                continue;
            }
            const std::string child = Join(path, name.c_str());
            // 子项也必须逐级检查，不能只检查顶层 capture/tasks 目录。
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
                ::FindClose(find);
                return false;
            }
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
                bool child_existed = false;
                if (!RemoveTree(child, &child_existed)) {
                    ::FindClose(find);
                    return false;
                }
            } else {
                if ((data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0) {
                    ::SetFileAttributesA(child.c_str(), data.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
                }
                if (!::DeleteFileA(child.c_str())) {
                    ::FindClose(find);
                    return false;
                }
            }
        } while (::FindNextFileA(find, &data));
        ::FindClose(find);
    } else if (::GetLastError() != ERROR_FILE_NOT_FOUND) {
        return false;
    }
    return ::RemoveDirectoryA(path.c_str()) != FALSE;
}
#else
bool RemoveTree(const std::string& path, bool* existed) {
    struct stat st;
    if (::lstat(path.c_str(), &st) != 0) {
        return errno == ENOENT;
    }
    *existed = true;
    // lstat 保证判断的是链接本身，避免递归进入符号链接指向的外部目录。
    if (S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode)) {
        return false;
    }
    DIR* dir = ::opendir(path.c_str());
    if (dir == NULL) {
        return false;
    }
    for (dirent* entry = ::readdir(dir); entry != NULL; entry = ::readdir(dir)) {
        const std::string name(entry->d_name);
        if (name == "." || name == "..") {
            continue;
        }
        const std::string child = Join(path, name.c_str());
        struct stat child_st;
        if (::lstat(child.c_str(), &child_st) != 0) {
            ::closedir(dir);
            return false;
        }
        if (S_ISLNK(child_st.st_mode)) {
            if (::unlink(child.c_str()) != 0) {
                ::closedir(dir);
                return false;
            }
        } else if (S_ISDIR(child_st.st_mode)) {
            bool child_existed = false;
            if (!RemoveTree(child, &child_existed)) {
                ::closedir(dir);
                return false;
            }
        } else if (::unlink(child.c_str()) != 0) {
            ::closedir(dir);
            return false;
        }
    }
    ::closedir(dir);
    return ::rmdir(path.c_str()) == 0;
}
#endif

} // namespace

SdkStorageCleanupResult RemoveSdkOpenTempDirectories(const std::string& work_dir) {
    SdkStorageCleanupResult result;
    std::string normalized_work_dir;
    if (!NormalizeWorkDir(work_dir, &normalized_work_dir)) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "invalid SDK Open work directory";
        return result;
    }

    // 删除范围固定为规范化工作目录下的两个精确子目录，不接受调用方传入任意目标路径。
    bool capture_existed = false;
    if (!RemoveTree(Join(normalized_work_dir, "capture"), &capture_existed)) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = "failed to remove SDK Open capture directory";
        return result;
    }
    bool tasks_existed = false;
    if (!RemoveTree(Join(normalized_work_dir, "tasks"), &tasks_existed)) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = "failed to remove SDK Open tasks directory";
        return result;
    }
    result.capture_removed = capture_existed;
    result.tasks_removed = tasks_existed;
    return result;
}

} // namespace private_impl
} // namespace sdk
} // namespace editor
