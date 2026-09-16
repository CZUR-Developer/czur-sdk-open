// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#define SDK_OPEN_MAIN_TESTING
#include "../runtime/sdk_open_main.cpp"

#include <fstream>
#include <stdexcept>

namespace editor {
namespace sdk {
std::shared_ptr<spdlog::logger> GetSdkOpenLogger() {
    return spdlog::default_logger();
}
} // namespace sdk
} // namespace editor

void ExpectUrl(const editor::sdk::SdkConfig& config, const std::string& expected) {
    const std::string actual = editor::sdk::BuildSdkAssetBaseUrl(config);
    if (actual != expected) {
        throw std::runtime_error("expected " + expected + ", got " + actual);
    }
}

int main(int argc, char* argv[]) {
    if (argc != 2) return 2;
    // The runner supplies isolated state/log roots and fake packaged TLS files.
    if (!PrepareMacRuntimeEnvironment()) return 1;
    ExpectUrl(LoadSdkOpenConfig("", true), "https://sdk-runtime.localhost:18082");
    ExpectUrl(LoadSdkOpenConfig("", false), "https://127.0.0.1:18082");

    ::setenv("SDK_ASSET_HTTPS_PORT", "19082", 1);
    ExpectUrl(LoadSdkOpenConfig("", true), "https://sdk-runtime.localhost:19082");

    ::setenv("SDK_ASSET_BASE_URL", "https://custom.example:20443", 1);
    ExpectUrl(LoadSdkOpenConfig("", true), "https://custom.example:20443");
    ::unsetenv("SDK_ASSET_BASE_URL");
    ::unsetenv("SDK_ASSET_HTTPS_PORT");

    const std::string config_path = argv[1];
    {
        std::ofstream config(config_path.c_str());
        config << "SDK_ASSET_BASE_URL=https://config.example:21443\n";
        config << "SDK_ASSET_HTTPS_PORT=21443\n";
        if (!config) return 1;
    }
    ExpectUrl(LoadSdkOpenConfig(config_path, true), "https://config.example:21443");
    ::setenv("SDK_ASSET_BASE_URL", "https://env.example:22443", 1);
    ExpectUrl(LoadSdkOpenConfig(config_path, true), "https://env.example:22443");
    ::unsetenv("SDK_ASSET_BASE_URL");
    {
        std::ofstream config(config_path.c_str());
        config << "SDK_ASSET_HTTPS_PORT=21443\n";
        if (!config) return 1;
    }
    ExpectUrl(LoadSdkOpenConfig(config_path, true), "https://sdk-runtime.localhost:21443");

    ::setenv("SDK_TLS_ENABLED", "0", 1);
    ExpectUrl(LoadSdkOpenConfig("", true), "http://127.0.0.1:17082");
    std::cout << "PASS: 8 macOS runtime URL configuration scenarios" << std::endl;
}
