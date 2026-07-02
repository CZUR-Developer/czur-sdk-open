// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "private_windows_provider_factory.h"

#include <memory>
#include <mutex>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "sdk_json_utils.h"

namespace editor {
namespace sdk {
namespace private_windows {

namespace {

#if defined(_WIN32)

typedef const char* (*PrivateProviderJsonFn)(const char*);
typedef void (*PrivateProviderFreeStringFn)(const char*);
typedef void (*PrivateProviderDeviceActionEventCallback)(const char*, void*);
typedef void (*PrivateProviderDeviceEventCallback)(const char*, void*);
typedef void (*PrivateProviderSetDeviceActionEventCallbackFn)(PrivateProviderDeviceActionEventCallback, void*);
typedef void (*PrivateProviderSetDeviceEventCallbackFn)(PrivateProviderDeviceEventCallback, void*);

struct PrivateProvidersCApi {
    HMODULE module = NULL;
    PrivateProviderJsonFn image_enhance_capabilities = NULL;
    PrivateProviderJsonFn image_enhance_run_step = NULL;
    PrivateProviderJsonFn ocr_recognize = NULL;
    PrivateProviderJsonFn ocr_get = NULL;
    PrivateProviderJsonFn ocr_cancel = NULL;
    PrivateProviderJsonFn ocr_extract_text = NULL;
    PrivateProviderSetDeviceActionEventCallbackFn set_device_action_event_callback = NULL;
    PrivateProviderSetDeviceEventCallbackFn set_device_event_callback = NULL;
    PrivateProviderFreeStringFn free_string = NULL;
};

PrivateProvidersCApi& GetPrivateProvidersCApi() {
    static PrivateProvidersCApi api;
    static bool loaded = false;
    if (loaded) {
        return api;
    }
    loaded = true;
    api.module = ::LoadLibraryA("sdk_private_providers.dll");
    if (api.module == NULL) {
        return api;
    }
    api.image_enhance_capabilities = reinterpret_cast<PrivateProviderJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_image_enhance_capabilities_json"));
    api.image_enhance_run_step = reinterpret_cast<PrivateProviderJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_image_enhance_run_step_json"));
    api.ocr_recognize = reinterpret_cast<PrivateProviderJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_ocr_recognize_json"));
    api.ocr_get = reinterpret_cast<PrivateProviderJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_ocr_get_json"));
    api.ocr_cancel = reinterpret_cast<PrivateProviderJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_ocr_cancel_json"));
    api.ocr_extract_text = reinterpret_cast<PrivateProviderJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_ocr_extract_text_json"));
    api.set_device_action_event_callback = reinterpret_cast<PrivateProviderSetDeviceActionEventCallbackFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_device_action_event_set_callback"));
    api.set_device_event_callback = reinterpret_cast<PrivateProviderSetDeviceEventCallbackFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_device_event_set_callback"));
    api.free_string = reinterpret_cast<PrivateProviderFreeStringFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_providers_free_string"));
    return api;
}

std::string StringField(const Json& object, const char* key, const std::string& fallback = "") {
    if (!object.is_object()) {
        return fallback;
    }
    Json::const_iterator it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : fallback;
}

int IntField(const Json& object, const char* key, int fallback = 0) {
    if (!object.is_object()) {
        return fallback;
    }
    Json::const_iterator it = object.find(key);
    return it != object.end() && it->is_number_integer() ? it->get<int>() : fallback;
}

bool BoolField(const Json& object, const char* key, bool fallback = false) {
    if (!object.is_object()) {
        return fallback;
    }
    Json::const_iterator it = object.find(key);
    return it != object.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

int64_t Int64Field(const Json& object, const char* key, int64_t fallback = 0) {
    if (!object.is_object()) {
        return fallback;
    }
    Json::const_iterator it = object.find(key);
    return it != object.end() && it->is_number_integer() ? it->get<int64_t>() : fallback;
}

std::vector<std::string> StringArrayField(const Json& object, const char* key) {
    std::vector<std::string> values;
    if (!object.is_object()) {
        return values;
    }
    Json::const_iterator array_it = object.find(key);
    if (array_it == object.end() || !array_it->is_array()) {
        return values;
    }
    for (Json::const_iterator it = array_it->begin(); it != array_it->end(); ++it) {
        if (it->is_string()) {
            values.push_back(it->get<std::string>());
        }
    }
    return values;
}

int Base64DecodeValue(char ch) {
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 26;
    }
    if (ch >= '0' && ch <= '9') {
        return ch - '0' + 52;
    }
    if (ch == '+') {
        return 62;
    }
    if (ch == '/') {
        return 63;
    }
    return -1;
}

std::vector<uint8_t> Base64Decode(const std::string& input) {
    std::vector<uint8_t> output;
    int value = 0;
    int bits = -8;
    for (std::string::const_iterator it = input.begin(); it != input.end(); ++it) {
        const char ch = *it;
        if (ch == '=') {
            break;
        }
        if (ch == '\r' || ch == '\n' || ch == '\t' || ch == ' ') {
            continue;
        }
        const int decoded = Base64DecodeValue(ch);
        if (decoded < 0) {
            output.clear();
            return output;
        }
        value = (value << 6) | decoded;
        bits += 6;
        if (bits >= 0) {
            output.push_back(static_cast<uint8_t>((value >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return output;
}

Json ImageEnhancePageToJson(const SdkImageEnhancePage& page) {
    return Json{{"source_index", page.source_index},
                {"output_index", page.output_index},
                {"path", page.path},
                {"dropped", page.dropped},
                {"metadata_json", page.metadata_json}};
}

SdkImageEnhancePage ImageEnhancePageFromJson(const Json& value) {
    SdkImageEnhancePage page;
    page.source_index = IntField(value, "source_index");
    page.output_index = IntField(value, "output_index");
    page.path = StringField(value, "path");
    page.dropped = BoolField(value, "dropped");
    page.metadata_json = StringField(value, "metadata_json", "{}");
    return page;
}

SdkCaptureResult CaptureResultFromJson(const Json& value) {
    SdkCaptureResult result;
    if (!value.is_object()) {
        return result;
    }
    result.code = IntField(value, "code");
    result.message = StringField(value, "message", result.message);
    result.captured = BoolField(value, "captured");
    result.output_path = StringField(value, "output_path");
    result.original_path = StringField(value, "original_path");
    result.laser_path = StringField(value, "laser_path");
    result.content_type = StringField(value, "content_type");
    result.payload = StringField(value, "payload");
    result.width = IntField(value, "width");
    result.height = IntField(value, "height");
    result.dpi = IntField(value, "dpi");
    Json::const_iterator size_it = value.find("size");
    if (size_it != value.end() && size_it->is_number_unsigned()) {
        result.size = size_it->get<uint64_t>();
    }
    result.detected_rects_source_width = IntField(value, "detected_rects_source_width");
    result.detected_rects_source_height = IntField(value, "detected_rects_source_height");
    result.scan_device_type = IntField(value, "scan_device_type");
    result.raw_payload = Base64Decode(StringField(value, "raw_payload_base64"));
    result.raw_laser_payload = Base64Decode(StringField(value, "raw_laser_payload_base64"));
    return result;
}

SdkDeviceActionEvent DeviceActionEventFromJson(const Json& value) {
    SdkDeviceActionEvent event;
    event.device_id = StringField(value, "device_id");
    const std::string type = StringField(value, "type", "turn_detect");
    event.type = type == "hardgrab" ? SdkDeviceActionType::HardGrab : SdkDeviceActionType::PageTurn;
    event.auto_capture = BoolField(value, "auto_capture");
    event.timestamp_ms = Int64Field(value, "timestamp_ms");
    Json::const_iterator capture_it = value.find("capture");
    if (capture_it != value.end()) {
        event.capture = CaptureResultFromJson(*capture_it);
    }
    return event;
}

SdkDeviceEvent DeviceEventFromJson(const Json& value) {
    SdkDeviceEvent event;
    event.device_id = StringField(value, "device_id");
    event.type = StringField(value, "type", event.type);
    event.reason = StringField(value, "reason");
    event.was_opened = BoolField(value, "was_opened");
    event.was_streaming = BoolField(value, "was_streaming");
    event.timestamp_ms = Int64Field(value, "timestamp_ms");
    return event;
}

Json OcrRecognizeRequestToJson(const SdkOcrRecognizeRequest& request) {
    return Json{{"input_upload_ids", request.input_upload_ids},
                {"input_files", request.input_files},
                {"output_path", request.output_path},
                {"output_dir", request.output_dir},
                {"format", request.format},
                {"export_type", request.export_type},
                {"ext_params_json", request.ext_params_json}};
}

SdkOcrTaskSnapshot OcrTaskFromJson(const Json& value) {
    SdkOcrTaskSnapshot task;
    task.task_id = StringField(value, "task_id");
    task.status = StringField(value, "status", task.status);
    task.progress = IntField(value, "progress");
    task.output_path = StringField(value, "output_path");
    task.output_paths = StringArrayField(value, "output_paths");
    task.format = StringField(value, "format");
    task.export_type = StringField(value, "export_type", task.export_type);
    task.message = StringField(value, "message", task.message);
    task.error = StringField(value, "error");
    return task;
}

SdkOcrTextBlock OcrTextBlockFromJson(const Json& value) {
    SdkOcrTextBlock block;
    block.text = StringField(value, "text");
    Json::const_iterator x_it = value.find("x");
    Json::const_iterator y_it = value.find("y");
    Json::const_iterator width_it = value.find("width");
    Json::const_iterator height_it = value.find("height");
    Json::const_iterator confidence_it = value.find("confidence");
    Json::const_iterator font_size_it = value.find("font_size");
    block.x = x_it != value.end() && x_it->is_number() ? x_it->get<float>() : 0.0f;
    block.y = y_it != value.end() && y_it->is_number() ? y_it->get<float>() : 0.0f;
    block.width = width_it != value.end() && width_it->is_number() ? width_it->get<float>() : 0.0f;
    block.height = height_it != value.end() && height_it->is_number() ? height_it->get<float>() : 0.0f;
    block.confidence = confidence_it != value.end() && confidence_it->is_number() ? confidence_it->get<float>() : 0.0f;
    block.font_size = font_size_it != value.end() && font_size_it->is_number() ? font_size_it->get<float>() : 0.0f;
    return block;
}

Json ImageEnhanceStepToJson(const SdkImageEnhanceStep& step) {
    return Json{{"id", step.id},
                {"type", step.type},
                {"provider", step.provider},
                {"enabled", step.enabled},
                {"on_error", step.on_error},
                {"params_json", step.params_json}};
}

Json ImageEnhanceStepRequestToJson(const SdkImageEnhanceStepRequest& request) {
    Json pages = Json::array();
    for (std::vector<SdkImageEnhancePage>::const_iterator it = request.pages.begin();
         it != request.pages.end();
         ++it) {
        pages.push_back(ImageEnhancePageToJson(*it));
    }
    return Json{{"task_id", request.task_id},
                {"step", ImageEnhanceStepToJson(request.step)},
                {"pages", pages},
                {"output_dir", request.output_dir},
                {"online_api_key", request.online_api_key},
                {"online_base_url", request.online_base_url}};
}

bool InvokePrivateProviderCApi(PrivateProviderJsonFn fn,
                               const Json& request,
                               Json* response,
                               std::string* message) {
    PrivateProvidersCApi& api = GetPrivateProvidersCApi();
    if (fn == NULL || api.free_string == NULL) {
        if (message != NULL) {
            *message = "private provider c api not ready";
        }
        return false;
    }
    const char* response_ptr = fn(request.dump().c_str());
    if (response_ptr == NULL) {
        if (message != NULL) {
            *message = "private provider c api returned null";
        }
        return false;
    }
    const std::string response_text(response_ptr);
    api.free_string(response_ptr);

    std::string parse_error;
    if (!TryParseJson(response_text, response, &parse_error) || response == NULL || !response->is_object()) {
        if (message != NULL) {
            *message = "private provider c api returned invalid json";
        }
        return false;
    }
    return true;
}

SdkImageEnhanceCapability CapabilityFromJson(const Json& value) {
    SdkImageEnhanceCapability capability;
    capability.type = StringField(value, "type");
    capability.title = StringField(value, "title");
    capability.description = StringField(value, "description");
    capability.i18n_key = StringField(value, "i18n_key");
    capability.title_zh_cn = StringField(value, "title_zh_cn");
    capability.description_zh_cn = StringField(value, "description_zh_cn");
    capability.category = StringField(value, "category");
    capability.runtime = StringField(value, "runtime", capability.runtime);
    capability.available = BoolField(value, "available", capability.available);
    capability.unavailable_reason = StringField(value, "unavailable_reason");
    capability.unavailable_reason_zh_cn = StringField(value, "unavailable_reason_zh_cn");
    capability.requires_capability = StringField(value, "requires_capability");
    capability.quota_unit = StringField(value, "quota_unit", capability.quota_unit);
    capability.source_types = StringArrayField(value, "source_types");
    capability.min_pages = IntField(value, "min_pages", capability.min_pages);
    capability.max_pages = IntField(value, "max_pages", capability.max_pages);
    capability.page_effect = StringField(value, "page_effect", capability.page_effect);
    capability.metadata = BoolField(value, "metadata", capability.metadata);
    capability.order_hint = IntField(value, "order_hint", capability.order_hint);
    capability.version = StringField(value, "version", capability.version);
    capability.defaults_json = StringField(value, "defaults_json", capability.defaults_json);
    capability.schema_json = StringField(value, "schema_json", capability.schema_json);
    return capability;
}

class WindowsPrivateImageEnhanceProvider : public ISdkImageEnhanceProvider {
public:
    std::string ProviderName() const override { return "czur-image-enhance-provider"; }

    SdkImageEnhanceCapabilityResult ListCapabilities() override {
        SdkImageEnhanceCapabilityResult result;
        PrivateProvidersCApi& api = GetPrivateProvidersCApi();
        Json response;
        std::string error;
        if (!InvokePrivateProviderCApi(api.image_enhance_capabilities, Json::object(), &response, &error)) {
            result.code = ToCode(SdkStatusCode::ProviderNotReady);
            result.message = error;
            return result;
        }
        result.code = IntField(response, "code");
        result.message = StringField(response, "message");
        result.provider = StringField(response, "provider", ProviderName());
        result.kind = StringField(response, "kind", result.kind);
        result.available = BoolField(response, "available", result.available);
        Json::const_iterator capabilities_it = response.find("capabilities");
        if (capabilities_it != response.end() && capabilities_it->is_array()) {
            for (Json::const_iterator it = capabilities_it->begin(); it != capabilities_it->end(); ++it) {
                result.capabilities.push_back(CapabilityFromJson(*it));
            }
        }
        return result;
    }

    SdkImageEnhanceStepResult RunStep(const SdkImageEnhanceStepRequest& request) override {
        SdkImageEnhanceStepResult result;
        PrivateProvidersCApi& api = GetPrivateProvidersCApi();
        Json response;
        std::string error;
        if (!InvokePrivateProviderCApi(api.image_enhance_run_step, ImageEnhanceStepRequestToJson(request), &response, &error)) {
            result.code = ToCode(SdkStatusCode::ProviderNotReady);
            result.message = error;
            return result;
        }
        result.code = IntField(response, "code");
        result.message = StringField(response, "message");
        result.processed = BoolField(response, "processed");
        result.metadata_json = StringField(response, "metadata_json", "{}");
        result.warnings = StringArrayField(response, "warnings");
        Json::const_iterator pages_it = response.find("pages");
        if (pages_it != response.end() && pages_it->is_array()) {
            for (Json::const_iterator it = pages_it->begin(); it != pages_it->end(); ++it) {
                result.pages.push_back(ImageEnhancePageFromJson(*it));
            }
        }
        return result;
    }
};

class WindowsPrivateOcrProvider : public ISdkOcrProvider {
public:
    std::string ProviderName() const override { return "czur-ocr-provider"; }

    SdkOcrRecognizeResult Recognize(const SdkOcrRecognizeRequest& request) override {
        SdkOcrRecognizeResult result;
        PrivateProvidersCApi& api = GetPrivateProvidersCApi();
        Json response;
        std::string error;
        if (!InvokePrivateProviderCApi(api.ocr_recognize, OcrRecognizeRequestToJson(request), &response, &error)) {
            result.code = ToCode(SdkStatusCode::ProviderNotReady);
            result.message = error;
            return result;
        }
        result.code = IntField(response, "code");
        result.message = StringField(response, "message");
        result.task_id = StringField(response, "task_id");
        Json::const_iterator task_it = response.find("task");
        if (task_it != response.end()) {
            result.task = OcrTaskFromJson(*task_it);
        }
        return result;
    }

    SdkOcrGetResult GetTask(const SdkOcrGetRequest& request) override {
        SdkOcrGetResult result;
        PrivateProvidersCApi& api = GetPrivateProvidersCApi();
        Json response;
        std::string error;
        if (!InvokePrivateProviderCApi(api.ocr_get, Json{{"task_id", request.task_id}}, &response, &error)) {
            result.code = ToCode(SdkStatusCode::ProviderNotReady);
            result.message = error;
            return result;
        }
        result.code = IntField(response, "code");
        result.message = StringField(response, "message");
        Json::const_iterator task_it = response.find("task");
        if (task_it != response.end()) {
            result.task = OcrTaskFromJson(*task_it);
        }
        return result;
    }

    SdkOcrCancelResult Cancel(const SdkOcrCancelRequest& request) override {
        SdkOcrCancelResult result;
        PrivateProvidersCApi& api = GetPrivateProvidersCApi();
        Json response;
        std::string error;
        if (!InvokePrivateProviderCApi(api.ocr_cancel, Json{{"task_id", request.task_id}}, &response, &error)) {
            result.code = ToCode(SdkStatusCode::ProviderNotReady);
            result.message = error;
            return result;
        }
        result.code = IntField(response, "code");
        result.message = StringField(response, "message");
        result.cancelled = BoolField(response, "cancelled");
        Json::const_iterator task_it = response.find("task");
        if (task_it != response.end()) {
            result.task = OcrTaskFromJson(*task_it);
        }
        return result;
    }

    SdkOcrExtractTextResult ExtractText(const SdkOcrExtractTextRequest& request) override {
        SdkOcrExtractTextResult result;
        PrivateProvidersCApi& api = GetPrivateProvidersCApi();
        Json response;
        std::string error;
        if (!InvokePrivateProviderCApi(api.ocr_extract_text,
                                       Json{{"input_upload_id", request.input_upload_id}, {"input_path", request.input_path}},
                                       &response,
                                       &error)) {
            result.code = ToCode(SdkStatusCode::ProviderNotReady);
            result.message = error;
            return result;
        }
        result.code = IntField(response, "code");
        result.message = StringField(response, "message");
        result.recognized = BoolField(response, "recognized");
        result.input_path = StringField(response, "input_path");
        result.width = IntField(response, "width");
        result.height = IntField(response, "height");
        Json::const_iterator blocks_it = response.find("blocks");
        if (blocks_it != response.end() && blocks_it->is_array()) {
            for (Json::const_iterator it = blocks_it->begin(); it != blocks_it->end(); ++it) {
                result.blocks.push_back(OcrTextBlockFromJson(*it));
            }
        }
        return result;
    }
};

// Windows private 分支的设备命令已经由 DeviceFacade 直接桥接 private C API。
// 这个 provider 只挂到 ProviderBundle 上，用于承接 private 层异步上报的设备动作和插拔事件。
class WindowsPrivateDeviceProvider : public ISdkDeviceProvider {
public:
    ~WindowsPrivateDeviceProvider() override {
        RegisterDeviceActionEventCallback(false);
        RegisterDeviceEventCallback(false);
    }

    std::string ProviderName() const override { return "czur-private-windows-device-provider"; }

    std::vector<SdkDeviceDescriptor> ListDevices() const override {
        return std::vector<SdkDeviceDescriptor>();
    }

    SdkDeviceOpenResult GetDevice(const SdkDeviceOpenRequest&) override {
        SdkDeviceOpenResult result;
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "private windows device commands are handled by DeviceFacade C API";
        return result;
    }

    SdkDeviceOpenResult OpenDevice(const SdkDeviceOpenRequest&) override {
        SdkDeviceOpenResult result;
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "private windows device commands are handled by DeviceFacade C API";
        return result;
    }

    SdkDeviceCloseResult CloseDevice(const SdkDeviceCloseRequest&) override {
        // Windows private 的命令型设备操作统一由 DeviceFacade 走 private C API。
        // 这里的 provider 只承担事件桥接职责，避免 close/stop 出现两套调用入口。
        SdkDeviceCloseResult result;
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "private windows device commands are handled by DeviceFacade C API";
        return result;
    }

    void CaptureStill(const SdkCaptureRequest&, SdkCaptureCallback callback) override {
        SdkCaptureResult result;
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "private windows capture commands are handled by DeviceFacade C API";
        if (callback) {
            callback(result);
        }
    }

    SdkVideoStartResult StartVideo(const SdkVideoStartRequest&, SdkVideoFrameCallback) override {
        SdkVideoStartResult result;
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "private windows video commands are handled by DeviceFacade C API";
        return result;
    }

    SdkVideoStopResult StopVideo(const SdkVideoStopRequest&) override {
        // stop/video 生命周期同样收口在 DeviceFacade，避免 provider adapter 重复转发 private C API。
        SdkVideoStopResult result;
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "private windows video commands are handled by DeviceFacade C API";
        return result;
    }

    SdkVideoFormatResult SetVideoFormat(const SdkVideoFormatRequest&) override {
        SdkVideoFormatResult result;
        result.code = ToCode(SdkStatusCode::UnsupportedMethod);
        result.message = "private windows video format commands are handled by DeviceFacade C API";
        return result;
    }

    SdkVideoProfileResult SetVideoProfile(const SdkVideoProfileRequest&) override {
        SdkVideoProfileResult result;
        result.code = ToCode(SdkStatusCode::UnsupportedMethod);
        result.message = "private windows video profile commands are handled by DeviceFacade C API";
        return result;
    }

    void SetDeviceActionEventSink(SdkDeviceActionEventCallback sink) override {
        // 翻页/硬拍事件由 private 层产生，这里只做 JSON 事件桥接并转发给 sdk_open command event。
        {
            std::lock_guard<std::mutex> lock(action_event_sink_mu_);
            action_event_sink_ = sink;
        }
        RegisterDeviceActionEventCallback(static_cast<bool>(sink));
    }

    void SetDeviceEventSink(SdkDeviceEventCallback sink) override {
        // 设备插拔生命周期事件也来自 private 层；Windows 分支必须显式桥接，否则接口默认空实现会吞掉 device.removed。
        {
            std::lock_guard<std::mutex> lock(device_event_sink_mu_);
            device_event_sink_ = sink;
        }
        RegisterDeviceEventCallback(static_cast<bool>(sink));
    }

private:
    static void DeviceActionEventThunk(const char* event_json, void* user_data) {
        WindowsPrivateDeviceProvider* provider = reinterpret_cast<WindowsPrivateDeviceProvider*>(user_data);
        if (provider == NULL || event_json == NULL) {
            return;
        }
        Json event_value;
        std::string parse_error;
        if (!TryParseJson(std::string(event_json), &event_value, &parse_error) || !event_value.is_object()) {
            return;
        }
        provider->PublishDeviceActionEvent(DeviceActionEventFromJson(event_value));
    }

    static void DeviceEventThunk(const char* event_json, void* user_data) {
        WindowsPrivateDeviceProvider* provider = reinterpret_cast<WindowsPrivateDeviceProvider*>(user_data);
        if (provider == NULL || event_json == NULL) {
            return;
        }
        Json event_value;
        std::string parse_error;
        if (!TryParseJson(std::string(event_json), &event_value, &parse_error) || !event_value.is_object()) {
            return;
        }
        provider->PublishDeviceEvent(DeviceEventFromJson(event_value));
    }

    void RegisterDeviceActionEventCallback(bool enabled) {
        PrivateProvidersCApi& api = GetPrivateProvidersCApi();
        if (api.set_device_action_event_callback == NULL) {
            return;
        }
        if (enabled) {
            api.set_device_action_event_callback(DeviceActionEventThunk, this);
        } else {
            api.set_device_action_event_callback(NULL, NULL);
        }
    }

    void RegisterDeviceEventCallback(bool enabled) {
        PrivateProvidersCApi& api = GetPrivateProvidersCApi();
        if (api.set_device_event_callback == NULL) {
            return;
        }
        if (enabled) {
            api.set_device_event_callback(DeviceEventThunk, this);
        } else {
            api.set_device_event_callback(NULL, NULL);
        }
    }

    void PublishDeviceActionEvent(const SdkDeviceActionEvent& event) {
        SdkDeviceActionEventCallback sink;
        {
            std::lock_guard<std::mutex> lock(action_event_sink_mu_);
            sink = action_event_sink_;
        }
        if (sink) {
            sink(event);
        }
    }

    void PublishDeviceEvent(const SdkDeviceEvent& event) {
        SdkDeviceEventCallback sink;
        {
            std::lock_guard<std::mutex> lock(device_event_sink_mu_);
            sink = device_event_sink_;
        }
        if (sink) {
            sink(event);
        }
    }

    std::mutex action_event_sink_mu_;
    std::mutex device_event_sink_mu_;
    SdkDeviceActionEventCallback action_event_sink_;
    SdkDeviceEventCallback device_event_sink_;
};

#endif

} // namespace

ProviderBundle CreateProviderBundle() {
    ProviderBundle bundle;
#if defined(_WIN32)
    bundle.device_provider = std::make_shared<WindowsPrivateDeviceProvider>();
    bundle.image_enhance_provider = std::make_shared<WindowsPrivateImageEnhanceProvider>();
    bundle.ocr_provider = std::make_shared<WindowsPrivateOcrProvider>();
#endif
    return bundle;
}

} // namespace private_windows
} // namespace sdk
} // namespace editor
