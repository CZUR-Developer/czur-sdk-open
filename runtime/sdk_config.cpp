// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_config.h"

#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <limits>

#include "sdk_logger.h"

namespace editor {
namespace sdk {

namespace {

bool TryReadPort(const char* value, int* port) {
    if (value == NULL || port == NULL || value[0] == '\0') {
        return false;
    }
    char* end = NULL;
    errno = 0;
    const long parsed = std::strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed <= 0 || parsed > 65535) {
        return false;
    }
    *port = static_cast<int>(parsed);
    return true;
}

void ApplyPortOverride(const char* key, int* target) {
    const char* value = std::getenv(key);
    if (value == NULL) {
        return;
    }
    if (!TryReadPort(value, target)) {
        SDK_OPEN_LOG_WARN("[sdk_config] ignored invalid port in {}", key);
    }
}

void ApplyStringOverride(const char* key, std::string* target) {
    const char* value = std::getenv(key);
    if (value != NULL && target != NULL) {
        *target = value;
    }
}

bool TryReadBool(const char* value, bool* result) {
    if (value == NULL || result == NULL) {
        return false;
    }
    const std::string normalized(value);
    if (normalized == "1" || normalized == "true" || normalized == "TRUE" || normalized == "on" || normalized == "ON") {
        *result = true;
        return true;
    }
    if (normalized == "0" || normalized == "false" || normalized == "FALSE" || normalized == "off" || normalized == "OFF") {
        *result = false;
        return true;
    }
    return false;
}

std::string PublishedHost(const std::string& host) {
    return host.empty() || host == "0.0.0.0" || host == "::" ? "127.0.0.1" : host;
}

std::string Trim(const std::string& value) {
    std::string::size_type begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }

    std::string::size_type end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

bool ApplyConfigValue(const std::string& key, const std::string& value, SdkConfig* config) {
    if (config == NULL) {
        return false;
    }

    if (key == "SDK_ADMIN_HTTP_PORT") {
        return TryReadPort(value.c_str(), &config->admin_http_port);
    }
    if (key == "SDK_DEMO_HTTP_PORT") {
        return TryReadPort(value.c_str(), &config->demo_http_port);
    }
    if (key == "SDK_ASSET_HTTP_PORT") {
        return TryReadPort(value.c_str(), &config->asset_http_port);
    }
    if (key == "SDK_COMMAND_WS_PORT") {
        return TryReadPort(value.c_str(), &config->command_ws_port);
    }
    if (key == "SDK_VIDEO_WS_PORT") {
        return TryReadPort(value.c_str(), &config->video_ws_port);
    }
    if (key == "SDK_ASSET_HTTPS_PORT") {
        return TryReadPort(value.c_str(), &config->tls.asset_https_port);
    }
    if (key == "SDK_COMMAND_WSS_PORT") {
        return TryReadPort(value.c_str(), &config->tls.command_wss_port);
    }
    if (key == "SDK_VIDEO_WSS_PORT") {
        return TryReadPort(value.c_str(), &config->tls.video_wss_port);
    }
    if (key == "SDK_TLS_ENABLED") {
        return TryReadBool(value.c_str(), &config->tls.enabled);
    }
    if (key == "SDK_ASSET_BASE_URL") {
        config->asset_base_url = value;
        return true;
    }
    if (key == "SDK_AUTH_TOKEN") {
        config->auth_token = value;
        return true;
    }
    if (key == "CZUR_TWAIN_WORK_DIR") {
        config->twain_work_dir = value;
        return true;
    }
    if (key == "SDK_TLS_BIND_HOST") {
        config->tls.bind_host = value;
        return true;
    }
    if (key == "SDK_TLS_CERT_FILE") {
        config->tls.certificate_chain_file = value;
        return true;
    }
    if (key == "SDK_TLS_KEY_FILE") {
        config->tls.private_key_file = value;
        return true;
    }
    if (key == "SDK_TLS_KEY_PASSWORD") {
        config->tls.private_key_password = value;
        return true;
    }
    return false;
}

} // namespace

SdkConfig SdkConfig::FromFile(const std::string& path) {
    SdkConfig config;
    if (path.empty()) {
        return config;
    }

    std::ifstream input(path);
    if (!input.is_open()) {
        SDK_OPEN_LOG_WARN("[sdk_config] cannot open config file: {}, fallback to defaults", path);
        return config;
    }

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        const std::string::size_type delimiter = trimmed.find('=');
        if (delimiter == std::string::npos) {
            SDK_OPEN_LOG_WARN("[sdk_config] ignored invalid config line {} in {}", line_number, path);
            continue;
        }

        const std::string key = Trim(trimmed.substr(0, delimiter));
        const std::string value = Trim(trimmed.substr(delimiter + 1));
        if (key.empty() || !ApplyConfigValue(key, value, &config)) {
            SDK_OPEN_LOG_WARN("[sdk_config] ignored invalid or unsupported config key {} in {}", key, path);
        }
    }

    SDK_OPEN_LOG_INFO("[sdk_config] loaded config file: {}", path);
    return config;
}

void ApplySdkEnvironmentOverrides(SdkConfig* config) {
    if (config == NULL) {
        return;
    }

    ApplyPortOverride("SDK_ADMIN_HTTP_PORT", &config->admin_http_port);
    ApplyPortOverride("SDK_DEMO_HTTP_PORT", &config->demo_http_port);
    ApplyPortOverride("SDK_ASSET_HTTP_PORT", &config->asset_http_port);
    ApplyPortOverride("SDK_COMMAND_WS_PORT", &config->command_ws_port);
    ApplyPortOverride("SDK_VIDEO_WS_PORT", &config->video_ws_port);
    ApplyStringOverride("SDK_ASSET_BASE_URL", &config->asset_base_url);
    ApplyStringOverride("SDK_AUTH_TOKEN", &config->auth_token);

    const char* tls_enabled = std::getenv("SDK_TLS_ENABLED");
    if (tls_enabled != NULL && !TryReadBool(tls_enabled, &config->tls.enabled)) {
        SDK_OPEN_LOG_WARN("[sdk_config] ignored invalid SDK_TLS_ENABLED value");
    }
    ApplyStringOverride("SDK_TLS_BIND_HOST", &config->tls.bind_host);
    ApplyStringOverride("SDK_TLS_CERT_FILE", &config->tls.certificate_chain_file);
    ApplyStringOverride("SDK_TLS_KEY_FILE", &config->tls.private_key_file);
    ApplyStringOverride("SDK_TLS_KEY_PASSWORD", &config->tls.private_key_password);
    ApplyPortOverride("SDK_ASSET_HTTPS_PORT", &config->tls.asset_https_port);
    ApplyPortOverride("SDK_COMMAND_WSS_PORT", &config->tls.command_wss_port);
    ApplyPortOverride("SDK_VIDEO_WSS_PORT", &config->tls.video_wss_port);
}

std::string BuildSdkAssetBaseUrl(const SdkConfig& config) {
    if (!config.asset_base_url.empty()) {
        return config.asset_base_url;
    }
    if (config.tls.enabled) {
        const std::string host = PublishedHost(config.tls.bind_host.empty() ? config.bind_host : config.tls.bind_host);
        return "https://" + host + ":" + std::to_string(config.tls.asset_https_port);
    }
    return "http://" + PublishedHost(config.bind_host) + ":" + std::to_string(config.asset_http_port);
}

} // namespace sdk
} // namespace editor
