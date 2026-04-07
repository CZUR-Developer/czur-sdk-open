// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_config.h"

#include <fstream>

#include "sdk_logger.h"

namespace editor {
namespace sdk {

SdkConfig SdkConfig::FromFile(const std::string& path) {
    SdkConfig config;
    if (path.empty()) {
        return config;
    }

    std::ifstream input(path);
    if (!input.is_open()) {
        SDK_OPEN_LOG_WARN("[sdk_config] cannot open config file: {}, fallback to defaults", path);
        return config;
    }

    // Skeleton parser placeholder: keeping default values for now.
    SDK_OPEN_LOG_INFO("[sdk_config] loaded config skeleton from: {}", path);
    return config;
}

} // namespace sdk
} // namespace editor
