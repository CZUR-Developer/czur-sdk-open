#include "graphic/graphic_algorithm_runtime.h"

#include <array>
#include <cstdlib>
#include <limits.h>
#include <sys/stat.h>

#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "czcv/czcv_all_header.hpp"
#include "private_sdk_support.h"
#include "support/private_sdk_executable_path.h"
#include "support/private_sdk_logger.h"

namespace editor {
namespace sdk {
namespace private_impl {
namespace graphic {

namespace {

char PathSeparator() {
#if defined(_WIN32)
    return '\\';
#else
    return '/';
#endif
}

bool IsPathSeparator(char ch) {
    return ch == '/' || ch == '\\';
}

bool DirectoryExists(const std::string& path) {
    struct stat st;
    return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string JoinPath(const std::string& dir, const std::string& name) {
    if (dir.empty() || dir == ".") {
        return name;
    }
    if (IsPathSeparator(dir[dir.size() - 1])) {
        return dir + name;
    }
    return dir + PathSeparator() + name;
}

std::string EnvValue(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
}

std::string CurrentDir() {
#if defined(_WIN32)
    char buffer[MAX_PATH] = {0};
    if (::_getcwd(buffer, sizeof(buffer)) == nullptr) {
        return ".";
    }
    return std::string(buffer);
#else
    char buffer[PATH_MAX] = {0};
    if (::getcwd(buffer, sizeof(buffer)) == nullptr) {
        return ".";
    }
    return std::string(buffer);
#endif
}

bool IsAbsolutePath(const std::string& path) {
#if defined(_WIN32)
    return path.size() >= 3 &&
           ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
           path[1] == ':' &&
           IsPathSeparator(path[2]);
#else
    return !path.empty() && path[0] == '/';
#endif
}

std::string EnsureTrailingSlash(const std::string& path) {
    if (path.empty() || IsPathSeparator(path[path.size() - 1])) {
        return path;
    }
    return path + PathSeparator();
}

std::string NormalizeAbsoluteDir(const std::string& path) {
    if (path.empty()) {
        return std::string();
    }
    if (IsAbsolutePath(path)) {
        return EnsureTrailingSlash(path);
    }
    return EnsureTrailingSlash(JoinPath(CurrentDir(), path));
}

std::array<char, 48> BuildCzcvLicense() {
    static const unsigned char kEncodedLicense[48] = {
        0x5a, 0x90, 0xd6, 0x4a, 0x8a, 0xc8, 0x1e, 0x52,
        0xc8, 0x0f, 0x56, 0x9a, 0x1a, 0x46, 0x8e, 0xee,
        0x5a, 0x9b, 0xf6, 0x2a, 0x6a, 0xe6, 0x3e, 0x7a,
        0xaa, 0x2e, 0x77, 0xbb, 0xfa, 0x6d, 0xae, 0xea,
        0x3a, 0x7b, 0xfe, 0x2a, 0x6a, 0xc6, 0x3e, 0x72,
        0xaa, 0x08, 0x76, 0xba, 0x23, 0x4d, 0x8e, 0x0a
    };

    std::array<char, 48> license = {{0}};
    for (std::size_t index = 0; index < license.size(); ++index) {
        const std::size_t encoded_index = (index * 13 + 7) % license.size();
        const unsigned char mask = static_cast<unsigned char>((0x5a + index * 17) & 0xff);
        license[index] = static_cast<char>(kEncodedLicense[encoded_index] ^ mask);
    }
    return license;
}

void SecureClear(std::array<char, 48>& value) {
    volatile char* data = value.data();
    for (std::size_t index = 0; index < value.size(); ++index) {
        data[index] = 0;
    }
}

} // namespace

GraphicAlgorithmRuntime& GraphicAlgorithmRuntime::Instance() {
    static GraphicAlgorithmRuntime runtime;
    return runtime;
}

bool GraphicAlgorithmRuntime::EnsureInitialized(SdkStatusCode& code, std::string& message) {
    std::lock_guard<std::mutex> lock(init_mu_);
    if (initialized_) {
        return true;
    }

    EnsureDirectoryRecursive(GetSdkRuntimeLogDir());
    const std::string log_path = ResolveCzcvLogPath();
    const std::string model_dir = ResolveModelDir();
    if (model_dir.empty() || !DirectoryExists(model_dir)) {
        code = SdkStatusCode::ProviderCallFailed;
        message = "graphic model directory is not configured";
        SDK_PRIVATE_LOG_ERROR("graphic.czcv init_failed model_dir={} code={} message={}",
                              SafePathSummary(model_dir),
                              ToCode(code),
                              message);
        return false;
    }

    std::array<char, 48> license = BuildCzcvLicense();
    SDK_PRIVATE_LOG_INFO("graphic.czcv init begin model_dir={} log_path={} license_present=true license_size=48",
                         SafePathSummary(model_dir),
                         SafePathSummary(log_path));
    try {
        czcv::init_czcv(log_path.c_str(), model_dir.c_str(), czcv::LogLevel::CZCV_LOG_INFO, license.data());
        SecureClear(license);
    } catch (...) {
        SecureClear(license);
        code = SdkStatusCode::ProviderCallFailed;
        message = "graphic service initialization failed";
        SDK_PRIVATE_LOG_ERROR("graphic.czcv init_failed model_dir={} code={} message={}",
                              SafePathSummary(model_dir),
                              ToCode(code),
                              message);
        return false;
    }

    initialized_ = true;
    SDK_PRIVATE_LOG_INFO("graphic.czcv init success model_dir={} log_path={}",
                         SafePathSummary(model_dir),
                         SafePathSummary(log_path));
    return true;
}

std::mutex& GraphicAlgorithmRuntime::AlgorithmMutex() {
    return algorithm_mu_;
}

std::string GraphicAlgorithmRuntime::ResolveRawModelDir() const {
    const std::string env = EnvValue("SDK_GRAPHIC_MODEL_DIR");
    if (!env.empty()) {
        return env;
    }
    return JoinPath(GetSdkExecutableDir(), "czcv_model");
}

std::string GraphicAlgorithmRuntime::ResolveModelDir() const {
    return NormalizeAbsoluteDir(ResolveRawModelDir());
}

std::string GraphicAlgorithmRuntime::ResolveCzcvLogPath() const {
    const std::string override_path = EnvValue("SDK_GRAPHIC_LOG_PATH");
    if (!override_path.empty()) {
        return override_path;
    }
    EnsureDirectoryRecursive(GetSdkRuntimeLogDir());
    return JoinPath(GetSdkRuntimeLogDir(), "czcv_sdk.log");
}

} // namespace graphic
} // namespace private_impl
} // namespace sdk
} // namespace editor
