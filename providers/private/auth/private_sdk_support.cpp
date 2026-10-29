// Copyright (c) 2026 CZUR Tech. All rights reserved.

#include "private_sdk_support.h"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(_WIN32)
#include <direct.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include <openssl/sha.h>

#include <httplib.h>

#include "support/private_sdk_logger.h"

namespace editor {
namespace sdk {
namespace private_impl {

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
    if (path.empty()) {
        return false;
    }
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }
    return S_ISDIR(st.st_mode);
}

bool FileExists(const std::string& path) {
    struct stat st;
    if (path.empty()) {
        return false;
    }
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }
#if defined(_WIN32)
    return (st.st_mode & _S_IFREG) == _S_IFREG;
#else
    return S_ISREG(st.st_mode);
#endif
}

std::string EnvString(const char* name) {
    if (name == NULL || name[0] == '\0') {
        return std::string();
    }
    const char* value = std::getenv(name);
    return value == NULL ? std::string() : std::string(value);
}

bool IsDirectoryWritable(const std::string& path) {
#if defined(_WIN32)
    return !path.empty() && ::_access(path.c_str(), 2) == 0;
#else
    return !path.empty() && ::access(path.c_str(), W_OK) == 0;
#endif
}

std::string DirName(const std::string& path) {
    const std::size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) {
        return "";
    }
    if (pos == 0) {
        return path.substr(0, 1);
    }
#if defined(_WIN32)
    if (pos == 2 && path.size() >= 3 && path[1] == ':' && IsPathSeparator(path[2])) {
        return path.substr(0, 3);
    }
#endif
    return path.substr(0, pos);
}

std::string JoinPath(const std::string& left, const std::string& right) {
    if (left.empty()) {
        return right;
    }
    if (right.empty()) {
        return left;
    }
    if (IsPathSeparator(left[left.size() - 1])) {
        return left + right;
    }
    return left + PathSeparator() + right;
}

std::string SanitizePathToken(const std::string& value) {
    std::string token;
    token.reserve(value.size());
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        const unsigned char ch = static_cast<unsigned char>(*it);
        if (std::isalnum(ch) || *it == '-' || *it == '_') {
            token.push_back(static_cast<char>(ch));
        } else {
            token.push_back('_');
        }
    }
    return token.empty() ? "unknown" : token;
}

#if defined(_WIN32)
std::string ResolveWindowsLocalAppDataSdkDir() {
    const char* local_app_data = std::getenv("LOCALAPPDATA");
    if (local_app_data != NULL && local_app_data[0] != '\0') {
        return JoinPath(JoinPath(std::string(local_app_data), ".czur"), "sdk");
    }

    const char* user_profile = std::getenv("USERPROFILE");
    if (user_profile != NULL && user_profile[0] != '\0') {
        return JoinPath(JoinPath(JoinPath(JoinPath(std::string(user_profile), "AppData"), "Local"), ".czur"), "sdk");
    }

    const char* temp = std::getenv("TEMP");
    if (temp != NULL && temp[0] != '\0') {
        return JoinPath(JoinPath(std::string(temp), ".czur"), "sdk");
    }
    return JoinPath(".czur", "sdk");
}
#endif

} // namespace

std::string GetSdkRuntimeDir() {
    const char* override_dir = std::getenv("CZUR_SDK_RUNTIME_DIR");
    if (override_dir != NULL && override_dir[0] != '\0') {
        return std::string(override_dir);
    }

    const char* sdk_open_work_dir = std::getenv("SDK_OPEN_WORK_DIR");
    if (sdk_open_work_dir != NULL && sdk_open_work_dir[0] != '\0') {
        return std::string(sdk_open_work_dir);
    }

#if !defined(_WIN32)
    if (::geteuid() == 0) {
        const std::string system_dir = "/var/lib/czur/sdk-open";
        if ((DirectoryExists(system_dir) || EnsureDirectoryRecursive(system_dir)) && IsDirectoryWritable(system_dir)) {
            return system_dir;
        }
    }
#endif

#if defined(_WIN32)
    const std::string local_dir = ResolveWindowsLocalAppDataSdkDir();
    if ((DirectoryExists(local_dir) || EnsureDirectoryRecursive(local_dir)) && IsDirectoryWritable(local_dir)) {
        return local_dir;
    }
    return JoinPath(".czur", "sdk");
#else
    const char* home = std::getenv("HOME");
    if (home != NULL && home[0] != '\0') {
        const std::string home_dir = JoinPath(JoinPath(std::string(home), ".czur"), "sdk");
        if ((DirectoryExists(home_dir) || EnsureDirectoryRecursive(home_dir)) && IsDirectoryWritable(home_dir)) {
            return home_dir;
        }
    }

    const std::string system_dir = "/var/lib/czur/sdk-open";
    if ((DirectoryExists(system_dir) || EnsureDirectoryRecursive(system_dir)) && IsDirectoryWritable(system_dir)) {
        return system_dir;
    }

    const std::string fallback_dir = "/tmp/.czur/sdk";
    if (DirectoryExists(fallback_dir) || EnsureDirectoryRecursive(fallback_dir)) {
        return fallback_dir;
    }
    return ".czur/sdk";
#endif
}

std::string GetSdkRuntimeLogDir() {
    return JoinPath(GetSdkRuntimeDir(), "logs");
}

std::string GetSdkDeviceRuntimeDir(const std::string& device_id) {
    return JoinPath(JoinPath(GetSdkRuntimeDir(), "devices"), SanitizePathToken(device_id));
}

std::string GetSdkDeviceLaserConfigPath(const std::string& device_id) {
    return JoinPath(JoinPath(GetSdkDeviceRuntimeDir(device_id), "laser"), "config.data");
}

bool EnsureDirectoryRecursive(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    if (DirectoryExists(path)) {
        return true;
    }
    const std::string parent = DirName(path);
    if (!parent.empty() && parent != path && !DirectoryExists(parent)) {
        if (!EnsureDirectoryRecursive(parent)) {
            return false;
        }
    }
#if defined(_WIN32)
    if (::_mkdir(path.c_str()) == 0) {
#else
    if (::mkdir(path.c_str(), 0755) == 0) {
#endif
        return true;
    }
    return errno == EEXIST && DirectoryExists(path);
}

std::string Sha256HexString(const std::string& value) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(value.data()), value.size(), digest);
    static const char kHex[] = "0123456789abcdef";
    std::string output;
    output.reserve(SHA256_DIGEST_LENGTH * 2);
    for (std::size_t index = 0; index < SHA256_DIGEST_LENGTH; ++index) {
        output.push_back(kHex[(digest[index] >> 4) & 0x0f]);
        output.push_back(kHex[digest[index] & 0x0f]);
    }
    return output;
}

bool LoadJsonFile(const std::string& path, nlohmann::json* json_value) {
    if (json_value == NULL) {
        return false;
    }
    std::ifstream input(path.c_str());
    if (!input.is_open()) {
        return false;
    }
    nlohmann::json parsed = nlohmann::json::parse(input, NULL, false);
    if (parsed.is_discarded()) {
        return false;
    }
    *json_value = parsed;
    return true;
}

bool SaveJsonFile(const std::string& path, const nlohmann::json& json_value) {
    const std::string parent = DirName(path);
    if (!parent.empty() && !EnsureDirectoryRecursive(parent)) {
        return false;
    }

    const std::string tmp_path = path + ".tmp";
    {
        std::ofstream output(tmp_path.c_str(), std::ios::out | std::ios::trunc);
        if (!output.is_open()) {
            return false;
        }
        output << json_value.dump(2);
        output.flush();
        if (!output.good()) {
            return false;
        }
    }
#if defined(_WIN32)
    std::remove(path.c_str());
#endif
    if (std::rename(tmp_path.c_str(), path.c_str()) != 0) {
        std::remove(tmp_path.c_str());
        return false;
    }
    return true;
}

std::string DumpJsonUtf8ReplacingInvalidBytes(const nlohmann::json& json_value,
                                              bool* replaced_invalid_utf8) {
    if (replaced_invalid_utf8 != NULL) {
        *replaced_invalid_utf8 = false;
    }
    try {
        return json_value.dump();
    } catch (const nlohmann::json::type_error& error) {
        if (error.id != 316) {
            throw;
        }
        if (replaced_invalid_utf8 != NULL) {
            *replaced_invalid_utf8 = true;
        }
        return json_value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }
}

int GetIntEnvOrDefault(const char* name, int default_value) {
    if (name == NULL || name[0] == '\0') {
        return default_value;
    }
    const char* value = std::getenv(name);
    if (value == NULL || value[0] == '\0') {
        return default_value;
    }
    const long parsed = std::strtol(value, NULL, 10);
    if (parsed <= 0 || parsed > INT_MAX) {
        return default_value;
    }
    return static_cast<int>(parsed);
}

SdkHttpsTrustStoreConfig ResolveSdkHttpsTrustStore() {
    SdkHttpsTrustStoreConfig config;

    const std::string env_ca_file = EnvString("SSL_CERT_FILE");
    if (!env_ca_file.empty() && FileExists(env_ca_file)) {
        config.set_ca_cert_path = true;
        config.ca_cert_file_path = env_ca_file;
        config.source = "env-file";
        return config;
    }

    const std::string env_ca_dir = EnvString("SSL_CERT_DIR");

#if defined(_WIN32)
    if (!env_ca_dir.empty() && DirectoryExists(env_ca_dir)) {
        config.set_ca_cert_path = true;
        config.ca_cert_dir_path = env_ca_dir;
        config.source = "env-dir";
        return config;
    }
    config.source = !env_ca_file.empty() || !env_ca_dir.empty() ? "env-missing-windows-system-store"
                                                               : "windows-system-store";
    return config;
#elif defined(__linux__)
    const char kSystemCaFile[] = "/etc/ssl/certs/ca-certificates.crt";
    if (FileExists(kSystemCaFile)) {
        config.set_ca_cert_path = true;
        config.ca_cert_file_path = kSystemCaFile;
        config.source = !env_ca_file.empty() ? "env-file-missing-system-ca-file" : "system-ca-file";
        return config;
    }

    if (!env_ca_dir.empty() && DirectoryExists(env_ca_dir)) {
        config.set_ca_cert_path = true;
        config.ca_cert_dir_path = env_ca_dir;
        config.source = "env-dir";
        return config;
    }

    const char kSystemCaDir[] = "/etc/ssl/certs";
    if (DirectoryExists(kSystemCaDir)) {
        config.set_ca_cert_path = true;
        config.ca_cert_dir_path = kSystemCaDir;
        config.source = !env_ca_dir.empty() ? "env-dir-missing-system-ca-dir" : "system-ca-dir";
        return config;
    }

    config.source = !env_ca_file.empty() || !env_ca_dir.empty() ? "env-missing-openssl-default"
                                                               : "openssl-default";
    return config;
#elif defined(__APPLE__)
    const char kSystemCaFile[] = "/etc/ssl/cert.pem";
    if (FileExists(kSystemCaFile)) {
        config.set_ca_cert_path = true;
        config.ca_cert_file_path = kSystemCaFile;
        config.source = !env_ca_file.empty() ? "env-file-missing-macos-system-ca-file"
                                             : "macos-system-ca-file";
        return config;
    }

    if (!env_ca_dir.empty() && DirectoryExists(env_ca_dir)) {
        config.set_ca_cert_path = true;
        config.ca_cert_dir_path = env_ca_dir;
        config.source = "env-dir";
        return config;
    }

    config.source = !env_ca_file.empty() || !env_ca_dir.empty() ? "env-missing-openssl-default"
                                                               : "openssl-default";
    return config;
#else
    if (!env_ca_dir.empty() && DirectoryExists(env_ca_dir)) {
        config.set_ca_cert_path = true;
        config.ca_cert_dir_path = env_ca_dir;
        config.source = "env-dir";
        return config;
    }
    config.source = !env_ca_file.empty() || !env_ca_dir.empty() ? "env-missing-openssl-default"
                                                               : "openssl-default";
    return config;
#endif
}

SdkHttpsTrustStoreConfig ConfigureSdkHttpsClientTrustStore(httplib::Client* client) {
    const SdkHttpsTrustStoreConfig config = ResolveSdkHttpsTrustStore();
    if (client == NULL) {
        SDK_PRIVATE_LOG_WARN("https.trust_store skip reason=null_client source={}", config.source);
        return config;
    }

    if (config.set_ca_cert_path) {
        client->set_ca_cert_path(config.ca_cert_file_path, config.ca_cert_dir_path);
        SDK_PRIVATE_LOG_INFO("https.trust_store configured source={} ca_file_present={} ca_dir_present={}",
                             config.source,
                             !config.ca_cert_file_path.empty(),
                             !config.ca_cert_dir_path.empty());
        return config;
    }

    if (config.source.find("env-missing") != std::string::npos) {
        SDK_PRIVATE_LOG_WARN("https.trust_store fallback source={}", config.source);
    } else {
        SDK_PRIVATE_LOG_INFO("https.trust_store default source={}", config.source);
    }
    return config;
}

ParsedHttpUrl ParseHttpUrl(const std::string& url) {
    ParsedHttpUrl parsed;
    std::string working = url;
    std::string scheme;
    if (working.compare(0, 8, "https://") == 0) {
        parsed.https = true;
        scheme = "https://";
        parsed.port = 443;
    } else if (working.compare(0, 7, "http://") == 0) {
        parsed.https = false;
        scheme = "http://";
        parsed.port = 80;
    } else {
        return parsed;
    }

    working = working.substr(scheme.size());
    const std::size_t slash_pos = working.find('/');
    std::string host_port = slash_pos == std::string::npos ? working : working.substr(0, slash_pos);
    parsed.base_path = slash_pos == std::string::npos ? "" : working.substr(slash_pos);
    if (parsed.base_path.empty()) {
        parsed.base_path = "";
    }
    if (!parsed.base_path.empty() && parsed.base_path[0] != '/') {
        parsed.base_path = "/" + parsed.base_path;
    }
    if (!parsed.base_path.empty() && parsed.base_path[parsed.base_path.size() - 1] == '/') {
        parsed.base_path.erase(parsed.base_path.size() - 1);
    }

    const std::size_t colon_pos = host_port.rfind(':');
    if (colon_pos != std::string::npos && colon_pos + 1 < host_port.size()) {
        parsed.host = host_port.substr(0, colon_pos);
        const long parsed_port = std::strtol(host_port.substr(colon_pos + 1).c_str(), NULL, 10);
        if (parsed_port <= 0 || parsed_port > 65535) {
            return ParsedHttpUrl();
        }
        parsed.port = static_cast<int>(parsed_port);
    } else {
        parsed.host = host_port;
    }

    if (parsed.host.empty()) {
        return ParsedHttpUrl();
    }
    parsed.valid = true;
    return parsed;
}

} // namespace private_impl
} // namespace sdk
} // namespace editor
