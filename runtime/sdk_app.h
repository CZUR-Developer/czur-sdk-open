// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>

#include "../application/admin_application_service.h"
#include "../application/command_application_service.h"
#include "../transport/sdk_http_server.h"
#include "../transport/sdk_ws_command_server.h"
#include "../transport/sdk_ws_video_server.h"
#include "sdk_config.h"
#include "sdk_json_utils.h"
#include "sdk_provider_bundle.h"

namespace editor {
namespace sdk {

class SdkApp {
public:
    SdkApp(const SdkConfig& config, ProviderBundle providers);
    bool Start();
    void Stop();
    bool IsRunning() const;

private:
    Json BuildStatusJson() const;
    uint64_t UptimeSeconds() const;

    SdkConfig config_;
    ProviderBundle providers_;
    std::unique_ptr<CommandApplicationService> command_application_service_;
    AdminApplicationService admin_application_service_;
    SdkHttpServer admin_http_server_;
    SdkHttpServer demo_http_server_;
    SdkWsCommandServer command_ws_server_;
    SdkWsVideoServer video_ws_server_;
    std::atomic<bool> running_;
    std::chrono::steady_clock::time_point start_time_;
};

} // namespace sdk
} // namespace editor
