#pragma once

#include <string>

namespace editor {
namespace sdk {

struct SdkConfig {
    std::string bind_host = "127.0.0.1";
    int admin_http_port = 17080;
    int demo_http_port = 17081;
    int command_ws_port = 17090;
    int video_ws_port = 17091;
    std::string web_root = "web";
    std::string auth_token = "dev-token";

    static SdkConfig FromFile(const std::string& path);
};

} // namespace sdk
} // namespace editor
