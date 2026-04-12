// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
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

    struct ConnectionAuthResult {
        bool authorized = false;
        int code = 1100;
        std::string message = "auth required";
        std::string connection_id;
        std::string stream_id;
    };

    using ConnectionAuthHandler = std::function<ConnectionAuthResult(const std::string&, const std::string&)>;

    SdkWsVideoServer(const std::string& host, int port);
    ~SdkWsVideoServer();

    void SetConnectionAuthHandler(ConnectionAuthHandler handler);
    bool Start();
    void Stop();
    Stats GetStats() const;

private:
    class Impl;

    std::string host_;
    int port_;
    std::atomic<bool> running_;
    ConnectionAuthHandler connection_auth_handler_;
    std::unique_ptr<Impl> impl_;
};

} // namespace sdk
} // namespace editor
