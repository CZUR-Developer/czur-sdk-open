// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include <string>
#include <iostream>
#include <utility>
#include <unistd.h>
#include <cstdlib>
#include <csignal>
#include <cerrno>
#include <cstring>

#ifdef SDK_USE_PRIVATE_PROVIDER
#include "private_provider_factory.h"
#else
#include "mock_provider_factory.h"
#endif
#include "sdk_app.h"
#include "sdk_config.h"
#include "sdk_logger.h"
#include "sdk_runtime_paths.h"

namespace {

std::string GetExecutableDir() {
    char buffer[PATH_MAX];
    const ssize_t len = ::readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len <= 0) {
        return ".";
    }
    buffer[len] = '\0';
    std::string exe_path(buffer);
    const size_t pos = exe_path.find_last_of('/');
    if (pos == std::string::npos) {
        return ".";
    }
    return exe_path.substr(0, pos);
}

bool BlockShutdownSignals(sigset_t* signal_set) {
    if (signal_set == nullptr) {
        return false;
    }
    ::sigemptyset(signal_set);
    ::sigaddset(signal_set, SIGINT);
    ::sigaddset(signal_set, SIGTERM);
    return ::pthread_sigmask(SIG_BLOCK, signal_set, nullptr) == 0;
}

int WaitForShutdownSignal(const sigset_t& signal_set) {
    int signal_number = 0;
    const int result = ::sigwait(&signal_set, &signal_number);
    if (result != 0) {
        return 0;
    }
    return signal_number;
}

} // namespace

void ApplyEnvPortOverride(const char* env_key, int& target_port) {
    const char* val = std::getenv(env_key);
    if (val == nullptr) {
        return;
    }
    const int parsed = std::atoi(val);
    if (parsed > 0 && parsed <= 65535) {
        target_port = parsed;
    }
}

void ApplyEnvStringOverride(const char* env_key, std::string& target_value) {
    const char* val = std::getenv(env_key);
    if (val == nullptr) {
        return;
    }
    target_value = val;
}

void ApplyDefaultSaneConfigDir() {
    const char* sane_config_dir = std::getenv("CZUR_SANE_CONFIG_DIR");
    if (sane_config_dir != nullptr && sane_config_dir[0] != '\0') {
        return;
    }
    const std::string default_dir = editor::sdk::JoinPath(
        editor::sdk::JoinPath(editor::sdk::GetSdkOpenWorkDir(), "sane"), "config");
    if (::setenv("CZUR_SANE_CONFIG_DIR", default_dir.c_str(), 0) != 0) {
        SDK_OPEN_LOG_WARN("[sdk_open_app] failed to set default CZUR_SANE_CONFIG_DIR: {}", std::strerror(errno));
    }
}

int main(int argc, char* argv[]) {
    sigset_t shutdown_signals;
    if (!BlockShutdownSignals(&shutdown_signals)) {
        std::cerr << "failed to initialize shutdown signal handling" << std::endl;
        return 1;
    }

    std::string config_path;
    if (argc > 1 && argv[1] != nullptr) {
        config_path = argv[1];
    }

    editor::sdk::InitializeSdkOpenLogger();
    editor::sdk::SdkConfig config = editor::sdk::SdkConfig::FromFile(config_path);
    config.web_root = GetExecutableDir() + "/web";
    ApplyEnvPortOverride("SDK_ADMIN_HTTP_PORT", config.admin_http_port);
    ApplyEnvPortOverride("SDK_DEMO_HTTP_PORT", config.demo_http_port);
    ApplyEnvPortOverride("SDK_ASSET_HTTP_PORT", config.asset_http_port);
    ApplyEnvPortOverride("SDK_COMMAND_WS_PORT", config.command_ws_port);
    ApplyEnvPortOverride("SDK_VIDEO_WS_PORT", config.video_ws_port);
    ApplyEnvStringOverride("SDK_ASSET_BASE_URL", config.asset_base_url);
    ApplyEnvStringOverride("SDK_AUTH_TOKEN", config.auth_token);

#ifdef SDK_USE_PRIVATE_PROVIDER
    ApplyDefaultSaneConfigDir();
    editor::sdk::ProviderBundle providers = editor::sdk::private_impl::CreateProviderBundle();
    SDK_OPEN_LOG_INFO("[sdk_open_app] provider mode: private");
#else
    editor::sdk::ProviderBundle providers = editor::sdk::mock::CreateProviderBundle();
    SDK_OPEN_LOG_INFO("[sdk_open_app] provider mode: mock");
#endif
    editor::sdk::SdkApp app(config, std::move(providers));
    if (!app.Start()) {
        SDK_OPEN_LOG_ERROR("[sdk_open_app] failed to start");
        return 1;
    }

    SDK_OPEN_LOG_INFO("[sdk_open_app] running. waiting for SIGINT/SIGTERM...");
    const int signal_number = WaitForShutdownSignal(shutdown_signals);
    SDK_OPEN_LOG_INFO("[sdk_open_app] shutdown requested, signal={}", signal_number);
    app.Stop();
    return 0;
}
