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

class SdkWsCommandServer {
public:
    struct Stats {
        uint64_t active_connections = 0;
        uint64_t auth_failed = 0;
        uint64_t request_count = 0;
    };

    using JsonSupplier = std::function<std::string()>;

    SdkWsCommandServer(const std::string& host, int port, const std::string& auth_token);
    ~SdkWsCommandServer();

    void SetStatusJsonSupplier(JsonSupplier supplier);
    void SetCapabilitiesJsonSupplier(JsonSupplier supplier);
    bool Start();
    void Stop();
    Stats GetStats() const;

private:
    class Impl;

    std::string host_;
    int port_;
    std::string auth_token_;
    std::atomic<bool> running_;
    JsonSupplier status_json_supplier_;
    JsonSupplier capabilities_json_supplier_;
    std::unique_ptr<Impl> impl_;
};

} // namespace sdk
} // namespace editor
