// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace editor {
namespace sdk {

class SdkWsVideoServer {
public:
    struct Stats {
        uint64_t active_connections = 0;
        uint64_t auth_failed = 0;
        uint64_t binary_frames = 0;
        uint64_t binary_bytes = 0;
    };

    SdkWsVideoServer(const std::string& host, int port, const std::string& auth_token);
    ~SdkWsVideoServer();

    bool Start();
    void Stop();
    Stats GetStats() const;

private:
    class Impl;

    std::string host_;
    int port_;
    std::string auth_token_;
    std::atomic<bool> running_;
    std::unique_ptr<Impl> impl_;
};

} // namespace sdk
} // namespace editor
