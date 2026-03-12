// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

namespace editor {
namespace sdk {

class SdkWsVideoServer {
public:
    SdkWsVideoServer(const std::string& host, int port);
    bool Start();
    void Stop();

private:
    std::string host_;
    int port_;
    bool running_;
};

} // namespace sdk
} // namespace editor

