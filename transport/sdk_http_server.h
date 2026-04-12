// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include "sdk_json_utils.h"

namespace httplib {
class Server;
}

namespace editor {
namespace sdk {

class SdkHttpServer {
public:
    using JsonSupplier = std::function<Json()>;

    SdkHttpServer(const std::string& site_name,
                  const std::string& host,
                  int port,
                  const std::string& document_root,
                  const std::string& auth_token);
    ~SdkHttpServer();

    void SetHealthSupplier(JsonSupplier supplier);
    void SetStatusSupplier(JsonSupplier supplier);
    bool Start();
    void Stop();

private:
    bool IsAuthorized(const std::string& authorization) const;
    bool ConfigureRoutes();

    std::string site_name_;
    std::string host_;
    int port_;
    std::string document_root_;
    std::string auth_token_;
    JsonSupplier health_supplier_;
    JsonSupplier status_supplier_;
    std::atomic<bool> running_;
    std::unique_ptr<httplib::Server> server_;
    std::thread server_thread_;
};

} // namespace sdk
} // namespace editor
