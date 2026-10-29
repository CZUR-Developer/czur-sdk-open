// Copyright (c) 2026 CZUR Tech. All rights reserved.

#pragma once

#include <string>

#include <nlohmann/json.hpp>

namespace httplib {
class Client;
}

namespace editor {
namespace sdk {
namespace private_impl {

struct ParsedHttpUrl {
    bool valid = false;
    bool https = false;
    std::string host;
    int port = 0;
    std::string base_path;
};

struct SdkHttpsTrustStoreConfig {
    bool set_ca_cert_path = false;
    std::string ca_cert_file_path;
    std::string ca_cert_dir_path;
    std::string source;
};

std::string GetSdkRuntimeDir();
std::string GetSdkRuntimeLogDir();
std::string GetSdkDeviceRuntimeDir(const std::string& device_id);
std::string GetSdkDeviceLaserConfigPath(const std::string& device_id);
bool EnsureDirectoryRecursive(const std::string& path);
std::string Sha256HexString(const std::string& value);
bool LoadJsonFile(const std::string& path, nlohmann::json* json_value);
bool SaveJsonFile(const std::string& path, const nlohmann::json& json_value);
std::string DumpJsonUtf8ReplacingInvalidBytes(const nlohmann::json& json_value,
                                              bool* replaced_invalid_utf8);
int GetIntEnvOrDefault(const char* name, int default_value);
SdkHttpsTrustStoreConfig ResolveSdkHttpsTrustStore();
SdkHttpsTrustStoreConfig ConfigureSdkHttpsClientTrustStore(httplib::Client* client);
ParsedHttpUrl ParseHttpUrl(const std::string& url);

} // namespace private_impl
} // namespace sdk
} // namespace editor
