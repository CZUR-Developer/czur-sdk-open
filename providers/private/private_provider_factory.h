#pragma once

#include "sdk_provider_bundle.h"

#if defined(_WIN32)
#if defined(CZUR_SDK_PRIVATE_PROVIDERS_BUILD)
#define CZUR_SDK_PRIVATE_PROVIDERS_API __declspec(dllexport)
#else
#define CZUR_SDK_PRIVATE_PROVIDERS_API __declspec(dllimport)
#endif
#else
#if defined(CZUR_SDK_PRIVATE_PROVIDERS_BUILD)
#define CZUR_SDK_PRIVATE_PROVIDERS_API __attribute__((visibility("default")))
#else
#define CZUR_SDK_PRIVATE_PROVIDERS_API
#endif
#endif

namespace editor {
namespace sdk {
namespace private_impl {

// 仅 SDK Open POSIX runtime 请求挂载 storage provider，其他 private bundle 调用保持既有能力集合。
CZUR_SDK_PRIVATE_PROVIDERS_API ProviderBundle CreateProviderBundle(bool enable_sdk_open_storage_provider = false);

// Runtime 停止接收请求后调用，在三方库静态析构前排空 OCR 任务并释放缓存 handler。
CZUR_SDK_PRIVATE_PROVIDERS_API void ShutdownOcrRuntime();

} // namespace private_impl
} // namespace sdk
} // namespace editor

extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_ocr_recognize_json(
    const char* request_json);
extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API const char* czur_sdk_private_ocr_extract_text_json(
    const char* request_json);
extern "C" CZUR_SDK_PRIVATE_PROVIDERS_API void czur_sdk_private_providers_free_string(const char* value);
