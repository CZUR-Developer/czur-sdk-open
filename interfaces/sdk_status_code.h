// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace editor {
namespace sdk {

enum class SdkStatusCode : int {
    Ok = 0,
    InvalidRequest = 1000,
    InvalidMethod = 1001,
    InvalidParams = 1002,
    UnsupportedMethod = 1003,
    RateLimited = 1004,
    AuthRequired = 1100,
    TokenInvalid = 1101,
    TokenExpired = 1102,
    SessionTokenInvalid = 1103,
    AccountTypeNotAllowed = 1104,
    DeviceNotInAuthScope = 1105,
    AuthSceneMismatch = 1106,
    CapabilityNotAllowed = 1107,
    StreamNotFound = 1300,
    InternalError = 1900,
    ProviderNotReady = 1901,
    ProviderCallFailed = 1902,
};

enum class SdkHttpStatus : int {
    Ok = 200,
    Unauthorized = 401,
    NotFound = 404,
};

constexpr int ToCode(SdkStatusCode code) {
    return static_cast<int>(code);
}

constexpr bool IsOkStatusCode(SdkStatusCode code) {
    return code == SdkStatusCode::Ok;
}

constexpr bool IsOkStatusCode(int code) {
    return code == ToCode(SdkStatusCode::Ok);
}

constexpr int ToHttpStatus(SdkHttpStatus status) {
    return static_cast<int>(status);
}

constexpr bool IsAuthStatusCode(SdkStatusCode code) {
    return ToCode(code) >= ToCode(SdkStatusCode::AuthRequired) &&
           ToCode(code) < ToCode(SdkStatusCode::InternalError);
}

constexpr bool IsAuthStatusCode(int code) {
    return code >= ToCode(SdkStatusCode::AuthRequired) &&
           code < ToCode(SdkStatusCode::InternalError);
}

} // namespace sdk
} // namespace editor
