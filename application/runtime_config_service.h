// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <mutex>
#include <string>

#include "sdk_json_utils.h"

namespace editor {
namespace sdk {

class RuntimeConfigService {
public:
    struct CentralAuthConfig {
        std::string base_url;
    };

    explicit RuntimeConfigService(const std::string& online_image_enhance_base_url = "",
                                  const std::string& authz_base_url = "");

    std::string OnlineImageEnhanceBaseUrl() const;
    std::string AuthzBaseUrl() const;
    Json BuildConfigJson() const;
    Json UpdateConfigJson(const Json& request);

private:
    std::string AdminOnlineImageEnhanceBaseUrl() const;
    std::string AdminAuthzBaseUrl() const;
    std::string EffectiveOnlineImageEnhanceBaseUrl(std::string* source) const;
    std::string EffectiveAuthzBaseUrl(std::string* source) const;
    CentralAuthConfig AdminCentralAuth() const;

    mutable std::mutex mu_;
    std::string online_image_enhance_base_url_;
    std::string authz_base_url_;
    CentralAuthConfig central_auth_;
};

} // namespace sdk
} // namespace editor
