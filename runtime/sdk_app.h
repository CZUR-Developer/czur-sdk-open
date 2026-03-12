#pragma once

#include <atomic>

#include "sdk_config.h"
#include "sdk_http_server.h"
#include "sdk_provider_bundle.h"
#include "sdk_ws_command_server.h"
#include "sdk_ws_video_server.h"

namespace editor {
namespace sdk {

class SdkApp {
public:
    SdkApp(const SdkConfig& config, ProviderBundle providers);
    bool Start();
    void Stop();
    bool IsRunning() const;

private:
    SdkConfig config_;
    ProviderBundle providers_;
    SdkHttpServer admin_http_server_;
    SdkHttpServer demo_http_server_;
    SdkWsCommandServer command_ws_server_;
    SdkWsVideoServer video_ws_server_;
    std::atomic<bool> running_;
};

} // namespace sdk
} // namespace editor

