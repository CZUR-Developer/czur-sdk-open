// Copyright (c) 2026 CZUR Tech. All rights reserved.

#pragma once

#if defined(_WIN32)
#if defined(CZUR_SDK_PRIVATE_C_API_BUILD)
#define CZUR_SDK_PRIVATE_C_API __declspec(dllexport)
#else
#define CZUR_SDK_PRIVATE_C_API __declspec(dllimport)
#endif
#else
#define CZUR_SDK_PRIVATE_C_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

CZUR_SDK_PRIVATE_C_API const char* czur_sdk_private_generate_api_key_json(const char* request_json);
CZUR_SDK_PRIVATE_C_API const char* czur_sdk_private_parse_api_key_json(const char* request_json);
CZUR_SDK_PRIVATE_C_API const char* czur_sdk_private_generate_auth_code_json(const char* request_json);
CZUR_SDK_PRIVATE_C_API const char* czur_sdk_private_create_session_json(const char* request_json);
CZUR_SDK_PRIVATE_C_API const char* czur_sdk_private_activate_offline_json(const char* request_json);
CZUR_SDK_PRIVATE_C_API const char* czur_sdk_private_consume_quota_json(const char* request_json);
CZUR_SDK_PRIVATE_C_API const char* czur_sdk_private_version_json(void);
CZUR_SDK_PRIVATE_C_API void czur_sdk_private_free_string(const char* value);

#ifdef __cplusplus
} // extern "C"
#endif
