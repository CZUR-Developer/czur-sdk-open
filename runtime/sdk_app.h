// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <chrono>
#include <string>

#include "sdk_config.h"
#include "sdk_http_server.h"
#include "sdk_json_utils.h"
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
    Json BuildStatusJson() const;
    Json BuildCapabilitiesJson() const;
    uint64_t UptimeSeconds() const;

    SdkConfig config_;
    ProviderBundle providers_;
    SdkHttpServer admin_http_server_;
    SdkHttpServer demo_http_server_;
    SdkWsCommandServer command_ws_server_;
    SdkWsVideoServer video_ws_server_;
    std::atomic<bool> running_;
    std::chrono::steady_clock::time_point start_time_;
};

} // namespace sdk
} // namespace editor
