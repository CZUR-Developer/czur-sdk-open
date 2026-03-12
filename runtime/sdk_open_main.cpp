#include <iostream>
#include <string>
#include <utility>
#include <unistd.h>
#include <limits.h>
#include <cstdlib>

#include "mock_provider_factory.h"
#include "sdk_app.h"
#include "sdk_config.h"

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

int main(int argc, char* argv[]) {
    std::string config_path;
    if (argc > 1 && argv[1] != nullptr) {
        config_path = argv[1];
    }

    editor::sdk::SdkConfig config = editor::sdk::SdkConfig::FromFile(config_path);
    config.web_root = GetExecutableDir() + "/web";
    ApplyEnvPortOverride("SDK_ADMIN_HTTP_PORT", config.admin_http_port);
    ApplyEnvPortOverride("SDK_DEMO_HTTP_PORT", config.demo_http_port);
    ApplyEnvPortOverride("SDK_COMMAND_WS_PORT", config.command_ws_port);
    ApplyEnvPortOverride("SDK_VIDEO_WS_PORT", config.video_ws_port);
    editor::sdk::ProviderBundle providers = editor::sdk::mock::CreateProviderBundle();

    std::cout << "[sdk_open_app] provider mode: mock" << std::endl;
    editor::sdk::SdkApp app(config, std::move(providers));
    if (!app.Start()) {
        std::cerr << "[sdk_open_app] failed to start" << std::endl;
        return 1;
    }

    std::cout << "[sdk_open_app] running. press Enter to exit..." << std::endl;
    std::string line;
    std::getline(std::cin, line);
    app.Stop();
    return 0;
}
