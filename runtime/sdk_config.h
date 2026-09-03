// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

namespace editor {
namespace sdk {

struct SdkTlsConfig {
    bool enabled = false;
    std::string bind_host;
    std::string certificate_chain_file;
    std::string private_key_file;
    std::string private_key_password;
    int asset_https_port = 18082;
    int command_wss_port = 18090;
    int video_wss_port = 18091;
};

struct SdkConfig {
    std::string bind_host = "127.0.0.1";
    int admin_http_port = 17080;
    int demo_http_port = 17081;
    int asset_http_port = 17082;
    int command_ws_port = 17090;
    int video_ws_port = 17091;
    std::string web_root = "web";
    std::string asset_base_url;
    std::string auth_token;
    std::string online_image_enhance_base_url;
    std::string authz_base_url;
    std::string twain_work_dir;
    SdkTlsConfig tls;

    static SdkConfig FromFile(const std::string& path);
};

// 运行时入口共用的环境变量覆盖逻辑。配置文件使用与环境变量相同的 KEY=VALUE 格式，
// 环境变量具有更高优先级。
void ApplySdkEnvironmentOverrides(SdkConfig* config);

// 统一生成 capture、image enhance 和 upload 响应中使用的资源公开地址。
std::string BuildSdkAssetBaseUrl(const SdkConfig& config);

} // namespace sdk
} // namespace editor
