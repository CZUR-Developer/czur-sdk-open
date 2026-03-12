// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_config.h"

#include <fstream>
#include <iostream>

namespace editor {
namespace sdk {

SdkConfig SdkConfig::FromFile(const std::string& path) {
    SdkConfig config;
    if (path.empty()) {
        return config;
    }

    std::ifstream input(path);
    if (!input.is_open()) {
        std::cerr << "[sdk_config] cannot open config file: " << path
                  << ", fallback to defaults" << std::endl;
        return config;
    }

    // Skeleton parser placeholder: keeping default values for now.
    std::cout << "[sdk_config] loaded config skeleton from: " << path << std::endl;
    return config;
}

} // namespace sdk
} // namespace editor

