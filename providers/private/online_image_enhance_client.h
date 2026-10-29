// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

#include "sdk_status_code.h"

namespace editor {
namespace sdk {
namespace private_impl {

struct OnlineImageEnhanceOptions {
    std::string capability;
    std::string input_path;
    std::string output_path;
    std::string api_key;
    std::string base_url;
};

struct OnlineImageEnhanceResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool processed = false;
    std::string metadata_json = "{}";
};

class OnlineImageEnhanceClient {
public:
    static bool IsConfigured();
    static OnlineImageEnhanceResult EnhanceImage(const OnlineImageEnhanceOptions& options);
};

} // namespace private_impl
} // namespace sdk
} // namespace editor
