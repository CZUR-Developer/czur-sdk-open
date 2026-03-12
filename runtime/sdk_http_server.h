// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <string>
#include <thread>

namespace editor {
namespace sdk {

class SdkHttpServer {
public:
    SdkHttpServer(const std::string& site_name,
                  const std::string& host,
                  int port,
                  const std::string& document_root);
    bool Start();
    void Stop();

private:
    void ServeLoop();
    void HandleClient(int client_fd);
    std::string ResolveRequestPath(const std::string& request_path) const;
    static bool SendAll(int fd, const char* data, size_t size);
    static std::string GuessContentType(const std::string& path);

    std::string site_name_;
    std::string host_;
    int port_;
    std::string document_root_;
    std::atomic<bool> running_;
    int server_fd_;
    std::thread server_thread_;
};

} // namespace sdk
} // namespace editor
