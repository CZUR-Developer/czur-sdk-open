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

    struct ConnectionAuthResult {
        bool authorized = false;
        int code = ToCode(SdkStatusCode::AuthRequired);
        std::string message = "auth required";
        std::string session_key;
        int expires_in = 0;
        Json auth_context = Json::object();
    };

    using JsonSupplier = std::function<Json()>;
    using RequestHandler = std::function<Json(const Json&)>;
    using ConnectionAuthHandler = std::function<ConnectionAuthResult(const std::string&)>;

    SdkWsCommandServer(const std::string& host, int port);
    ~SdkWsCommandServer();

    void SetConnectionAuthHandler(ConnectionAuthHandler handler);
    void SetRequestHandler(RequestHandler handler);
    void SetStatusJsonSupplier(JsonSupplier supplier);
    void SetCapabilitiesJsonSupplier(JsonSupplier supplier);
    bool Start();
    void Stop();
    Stats GetStats() const;

private:
    class Impl;

    std::string host_;
    int port_;
    std::atomic<bool> running_;
    ConnectionAuthHandler connection_auth_handler_;
    RequestHandler request_handler_;
    JsonSupplier status_json_supplier_;
    JsonSupplier capabilities_json_supplier_;
    std::unique_ptr<Impl> impl_;
};

} // namespace sdk
} // namespace editor
