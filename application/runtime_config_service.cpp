// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "runtime_config_service.h"

#include <cstdlib>

namespace editor {
namespace sdk {

namespace {

const char kOnlineImageEnhanceBaseUrlEnv[] = "CZUR_SDK_IMAGE_ENHANCE_BASE_URL";
const char kAuthzBaseUrlEnv[] = "CZUR_SDK_AUTHZ_BASE_URL";
const char kDefaultOnlineImageEnhanceBaseUrl[] = "https://gateway-cn.czur.com";
const char kDefaultAuthzBaseUrl[] = "https://gateway-cn.czur.com";

std::string Trim(std::string value) {
    const std::string whitespace = " \t\r\n";
    const std::string::size_type begin = value.find_first_not_of(whitespace);
    if (begin == std::string::npos) {
        return "";
    }
    const std::string::size_type end = value.find_last_not_of(whitespace);
    return value.substr(begin, end - begin + 1);
}

std::string GetEnvString(const char* name) {
    const char* value = std::getenv(name);
    return value == NULL ? std::string() : Trim(value);
}

std::string OptionalString(const Json& obj, const char* key) {
    const Json::const_iterator it = obj.find(key);
    if (it != obj.end() && it->is_string()) {
        return Trim(it->get<std::string>());
    }
    return "";
}

} // namespace

RuntimeConfigService::RuntimeConfigService(const std::string& online_image_enhance_base_url,
                                           const std::string& authz_base_url)
    : online_image_enhance_base_url_(Trim(online_image_enhance_base_url)),
      authz_base_url_(Trim(authz_base_url)) {}

std::string RuntimeConfigService::AdminOnlineImageEnhanceBaseUrl() const {
    std::lock_guard<std::mutex> lock(mu_);
    return online_image_enhance_base_url_;
}

std::string RuntimeConfigService::AdminAuthzBaseUrl() const {
    std::lock_guard<std::mutex> lock(mu_);
    return authz_base_url_;
}

std::string RuntimeConfigService::EffectiveOnlineImageEnhanceBaseUrl(std::string* source) const {
    const std::string env_value = GetEnvString(kOnlineImageEnhanceBaseUrlEnv);
    if (!env_value.empty()) {
        if (source != NULL) {
            *source = "env";
        }
        return env_value;
    }
    const std::string admin_value = AdminOnlineImageEnhanceBaseUrl();
    if (!admin_value.empty()) {
        if (source != NULL) {
            *source = "admin";
        }
        return admin_value;
    }
    if (source != NULL) {
        *source = "default";
    }
    return kDefaultOnlineImageEnhanceBaseUrl;
}

std::string RuntimeConfigService::EffectiveAuthzBaseUrl(std::string* source) const {
    const std::string env_value = GetEnvString(kAuthzBaseUrlEnv);
    if (!env_value.empty()) {
        if (source != NULL) {
            *source = "env";
        }
        return env_value;
    }
    const std::string admin_value = AdminAuthzBaseUrl();
    if (!admin_value.empty()) {
        if (source != NULL) {
            *source = "admin";
        }
        return admin_value;
    }
    if (source != NULL) {
        *source = "default";
    }
    return kDefaultAuthzBaseUrl;
}

std::string RuntimeConfigService::OnlineImageEnhanceBaseUrl() const {
    return EffectiveOnlineImageEnhanceBaseUrl(NULL);
}

std::string RuntimeConfigService::AuthzBaseUrl() const {
    return EffectiveAuthzBaseUrl(NULL);
}

Json RuntimeConfigService::BuildConfigJson() const {
    std::string online_source;
    const std::string online_effective = EffectiveOnlineImageEnhanceBaseUrl(&online_source);
    std::string authz_source;
    const std::string authz_effective = EffectiveAuthzBaseUrl(&authz_source);
    return Json{
        {"online_image_enhance",
         Json{{"base_url", AdminOnlineImageEnhanceBaseUrl()},
              {"effective_base_url", online_effective},
              {"source", online_source}}},
        {"authz",
         Json{{"base_url", AdminAuthzBaseUrl()},
              {"effective_base_url", authz_effective},
              {"source", authz_source}}},
    };
}

Json RuntimeConfigService::UpdateConfigJson(const Json& request) {
    std::string online_base_url;
    const Json::const_iterator online_it = request.find("online_image_enhance");
    if (online_it != request.end() && online_it->is_object()) {
        online_base_url = OptionalString(*online_it, "base_url");
    }
    std::string authz_base_url;
    const Json::const_iterator authz_it = request.find("authz");
    if (authz_it != request.end() && authz_it->is_object()) {
        authz_base_url = OptionalString(*authz_it, "base_url");
    }
    {
        std::lock_guard<std::mutex> lock(mu_);
        online_image_enhance_base_url_ = online_base_url;
        authz_base_url_ = authz_base_url;
    }
    return BuildConfigJson();
}

} // namespace sdk
} // namespace editor
