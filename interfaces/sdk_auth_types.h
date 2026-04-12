// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sdk_status_code.h"

namespace editor {
namespace sdk {

enum class SdkAccountType {
    Unknown = -1,
    Vip = 0,
    Svip = 1,
    SvipPlus = 2,
    Custom = 3,
};

struct SdkDeviceGrant {
    int vid = 0;
    int pid = 0;
};

struct AuthContext {
    bool is_valid = false;
    SdkAccountType account_type = SdkAccountType::Unknown;
    int account_type_code = -1;
    std::string auth_scene;
    std::string license_mode;
    std::vector<SdkDeviceGrant> device_scope;
    std::int64_t expires_at = 0;
    std::vector<std::string> capabilities;
};

struct AuthValidateRequest {
    std::string token;
    std::int64_t now_ts = 0;
};

struct AuthValidateResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    AuthContext auth_context;
};

struct AuthRefreshRequest {
    std::string session_token;
    std::int64_t now_ts = 0;
};

struct AuthRefreshResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    std::string session_token;
    int expires_in = 0;
    AuthContext auth_context;
};

struct SessionValidateRequest {
    std::string session_token;
    std::int64_t now_ts = 0;
};

struct SessionValidateResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    AuthContext auth_context;
};

struct AuthLookupRequest {
    std::string session_token;
    std::int64_t now_ts = 0;
};

struct AuthContextResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool via_session = false;
    AuthContext auth_context;
};

inline int AccountTypeCode(SdkAccountType account_type) {
    return static_cast<int>(account_type);
}

inline const char* ToAccountTypeString(SdkAccountType account_type) {
    switch (account_type) {
        case SdkAccountType::Vip:
            return "vip";
        case SdkAccountType::Svip:
            return "svip";
        case SdkAccountType::SvipPlus:
            return "svip_plus";
        case SdkAccountType::Custom:
            return "custom";
        case SdkAccountType::Unknown:
        default:
            return "unknown";
    }
}

inline SdkAccountType AccountTypeFromCode(int code) {
    switch (code) {
        case 0:
            return SdkAccountType::Vip;
        case 1:
            return SdkAccountType::Svip;
        case 2:
            return SdkAccountType::SvipPlus;
        case 3:
            return SdkAccountType::Custom;
        default:
            return SdkAccountType::Unknown;
    }
}

inline SdkAccountType AccountTypeFromString(const std::string& value) {
    if (value == "vip") {
        return SdkAccountType::Vip;
    }
    if (value == "svip") {
        return SdkAccountType::Svip;
    }
    if (value == "svip_plus") {
        return SdkAccountType::SvipPlus;
    }
    if (value == "custom") {
        return SdkAccountType::Custom;
    }
    return SdkAccountType::Unknown;
}

} // namespace sdk
} // namespace editor
