#include "sdk_app.h"

#include <iostream>
#include <utility>

namespace editor {
namespace sdk {

SdkApp::SdkApp(const SdkConfig& config, ProviderBundle providers)
    : config_(config),
      providers_(std::move(providers)),
      admin_http_server_("admin-site", config_.bind_host, config_.admin_http_port, config_.web_root + "/admin"),
      demo_http_server_("demo-site", config_.bind_host, config_.demo_http_port, config_.web_root + "/demo"),
      command_ws_server_(config_.bind_host, config_.command_ws_port),
      video_ws_server_(config_.bind_host, config_.video_ws_port),
      running_(false) {}

bool SdkApp::Start() {
    if (running_.load()) {
        return true;
    }

    std::cout << "[sdk_app] starting..." << std::endl;
    if (!admin_http_server_.Start()) {
        return false;
    }
    if (!demo_http_server_.Start()) {
        admin_http_server_.Stop();
        return false;
    }
    if (!command_ws_server_.Start()) {
        demo_http_server_.Stop();
        admin_http_server_.Stop();
        return false;
    }
    if (!video_ws_server_.Start()) {
        command_ws_server_.Stop();
        demo_http_server_.Stop();
        admin_http_server_.Stop();
        return false;
    }

    if (providers_.device_provider) {
        std::cout << "[sdk_app] device provider: " << providers_.device_provider->ProviderName() << std::endl;
    }
    if (providers_.graphic_provider) {
        std::cout << "[sdk_app] graphic provider: " << providers_.graphic_provider->ProviderName() << std::endl;
    }
    if (providers_.ocr_provider) {
        std::cout << "[sdk_app] ocr provider: " << providers_.ocr_provider->ProviderName() << std::endl;
    }
    if (providers_.ofd_provider) {
        std::cout << "[sdk_app] ofd provider: " << providers_.ofd_provider->ProviderName() << std::endl;
    }

    running_.store(true);
    std::cout << "[sdk_app] started" << std::endl;
    return true;
}

void SdkApp::Stop() {
    if (!running_.load()) {
        return;
    }

    std::cout << "[sdk_app] stopping..." << std::endl;
    video_ws_server_.Stop();
    command_ws_server_.Stop();
    demo_http_server_.Stop();
    admin_http_server_.Stop();
    running_.store(false);
    std::cout << "[sdk_app] stopped" << std::endl;
}

bool SdkApp::IsRunning() const {
    return running_.load();
}

} // namespace sdk
} // namespace editor
