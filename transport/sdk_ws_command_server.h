// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "sdk_json_utils.h"

namespace editor {
namespace sdk {

class SdkWsCommandServer {
public:
    struct Stats {
        uint64_t active_connections = 0;
        uint64_t auth_failed = 0;
        uint64_t request_count = 0;
    };

    using RequestHandler = std::function<Json(const std::string&, const Json&)>;
    using ConnectionHandler = std::function<void(const std::string&)>;

    SdkWsCommandServer(const std::string& host, int port);
    ~SdkWsCommandServer();

    void SetRequestHandler(RequestHandler handler);
    void SetOpenHandler(ConnectionHandler handler);
    void SetCloseHandler(ConnectionHandler handler);
    bool SendEvent(const std::string& connection_id, const Json& event);
    void RunOnIoThreadSync(const std::function<void()>& task);
    bool Start();
    void Stop();
    Stats GetStats() const;

private:
    class Impl;

    std::string host_;
    int port_;
    std::atomic<bool> running_;
    RequestHandler request_handler_;
    ConnectionHandler open_handler_;
    ConnectionHandler close_handler_;
    std::unique_ptr<Impl> impl_;
};

} // namespace sdk
} // namespace editor
