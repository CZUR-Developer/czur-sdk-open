// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

#include "sdk_config.h"

namespace editor {
namespace sdk {

// 验证 TLS 配置、证书链和私钥。失败信息不包含私钥或口令。
bool ValidateSdkTlsConfig(const SdkTlsConfig& config, std::string* error_message);

// 将统一安全策略应用到一个已创建的 OpenSSL server context。
// 仅在 SDK_OPEN_ENABLE_TLS=1 时使用；未启用 TLS 的构建会返回失败说明。
bool ConfigureSdkTlsServerContext(void* native_ssl_ctx,
                                  const SdkTlsConfig& config,
                                  std::string* error_message);

} // namespace sdk
} // namespace editor
