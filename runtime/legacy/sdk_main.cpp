#include <iostream>
#include <string>
#include <utility>
#include <unistd.h>
#include <limits.h>
#include <cstdlib>

#include "sdk_app.h"
#include "sdk_config.h"
#include "sdk_logger.h"

#ifdef SDK_USE_PRIVATE_PROVIDER
#include "private_provider_factory.h"
#else
#include "mock_provider_factory.h"
#endif

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

int main(int argc, char* argv[]) {
    std::string config_path;
    if (argc > 1 && argv[1] != nullptr) {
        config_path = argv[1];
    }

    editor::sdk::InitializeSdkOpenLogger();
    editor::sdk::SdkConfig config = editor::sdk::SdkConfig::FromFile(config_path);
    config.web_root = GetExecutableDir() + "/web";
    editor::sdk::ApplySdkEnvironmentOverrides(&config);

#ifdef SDK_USE_PRIVATE_PROVIDER
    editor::sdk::ProviderBundle providers = editor::sdk::private_impl::CreateProviderBundle();
    std::cout << "[sdk_app] provider mode: private" << std::endl;
#else
    editor::sdk::ProviderBundle providers = editor::sdk::mock::CreateProviderBundle();
    std::cout << "[sdk_app] provider mode: mock" << std::endl;
#endif

    editor::sdk::SdkApp app(config, std::move(providers));
    if (!app.Start()) {
        std::cerr << "[sdk_app] failed to start" << std::endl;
        return 1;
    }

    std::cout << "[sdk_app] running. press Enter to exit..." << std::endl;
    std::string line;
    std::getline(std::cin, line);
    app.Stop();
    return 0;
}
