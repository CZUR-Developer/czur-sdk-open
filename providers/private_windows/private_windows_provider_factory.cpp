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
typedef void (*PrivateProviderTwainSourceEventCallback)(const char*, void*);
typedef void (*PrivateProviderTwainScanEventCallback)(const char*, void*);
typedef void (*PrivateProviderSetDeviceActionEventCallbackFn)(PrivateProviderDeviceActionEventCallback, void*);
typedef void (*PrivateProviderSetDeviceEventCallbackFn)(PrivateProviderDeviceEventCallback, void*);
typedef void (*PrivateProviderSetTwainSourceEventCallbackFn)(PrivateProviderTwainSourceEventCallback, void*);
typedef void (*PrivateProviderSetTwainScanEventCallbackFn)(PrivateProviderTwainScanEventCallback, void*);

struct PrivateProvidersCApi {
    HMODULE module = NULL;
    PrivateProviderJsonFn image_enhance_capabilities = NULL;
    PrivateProviderJsonFn image_enhance_run_step = NULL;
    PrivateProviderJsonFn ocr_recognize = NULL;
    PrivateProviderJsonFn ocr_get = NULL;
    PrivateProviderJsonFn ocr_cancel = NULL;
    PrivateProviderJsonFn ocr_extract_text = NULL;
    PrivateProviderJsonFn storage_cleanup_temp = NULL;
    PrivateProviderJsonFn twain_status = NULL;
    PrivateProviderJsonFn twain_list = NULL;
    PrivateProviderJsonFn twain_watch_start = NULL;
    PrivateProviderJsonFn twain_watch_stop = NULL;
    PrivateProviderJsonFn twain_open = NULL;
    PrivateProviderJsonFn twain_close = NULL;
    PrivateProviderJsonFn twain_get_capabilities = NULL;
    PrivateProviderJsonFn twain_set_capabilities = NULL;
    PrivateProviderJsonFn twain_profile_list = NULL;
    PrivateProviderJsonFn twain_profile_save = NULL;
    PrivateProviderJsonFn twain_profile_apply = NULL;
    PrivateProviderJsonFn twain_profile_delete = NULL;
    PrivateProviderJsonFn twain_scan = NULL;
    PrivateProviderJsonFn twain_scan_get = NULL;
    PrivateProviderJsonFn twain_scan_cancel = NULL;
    PrivateProviderSetDeviceActionEventCallbackFn set_device_action_event_callback = NULL;
    PrivateProviderSetDeviceEventCallbackFn set_device_event_callback = NULL;
    PrivateProviderSetTwainSourceEventCallbackFn set_twain_source_event_callback = NULL;
    PrivateProviderSetTwainScanEventCallbackFn set_twain_scan_event_callback = NULL;
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
    api.storage_cleanup_temp = reinterpret_cast<PrivateProviderJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_storage_cleanup_temp_json"));
    api.twain_status = reinterpret_cast<PrivateProviderJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_twain_status_json"));
    api.twain_list = reinterpret_cast<PrivateProviderJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_twain_list_json"));
    api.twain_watch_start = reinterpret_cast<PrivateProviderJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_twain_watch_start_json"));
    api.twain_watch_stop = reinterpret_cast<PrivateProviderJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_twain_watch_stop_json"));
    api.twain_open = reinterpret_cast<PrivateProviderJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_twain_open_json"));
    api.twain_close = reinterpret_cast<PrivateProviderJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_twain_close_json"));
    api.twain_get_capabilities = reinterpret_cast<PrivateProviderJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_twain_get_capabilities_json"));
    api.twain_set_capabilities = reinterpret_cast<PrivateProviderJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_twain_set_capabilities_json"));
    api.twain_profile_list = reinterpret_cast<PrivateProviderJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_twain_profile_list_json"));
    api.twain_profile_save = reinterpret_cast<PrivateProviderJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_twain_profile_save_json"));
    api.twain_profile_apply = reinterpret_cast<PrivateProviderJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_twain_profile_apply_json"));
    api.twain_profile_delete = reinterpret_cast<PrivateProviderJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_twain_profile_delete_json"));
    api.twain_scan = reinterpret_cast<PrivateProviderJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_twain_scan_json"));
    api.twain_scan_get = reinterpret_cast<PrivateProviderJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_twain_scan_get_json"));
    api.twain_scan_cancel = reinterpret_cast<PrivateProviderJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_twain_scan_cancel_json"));
    api.set_device_action_event_callback = reinterpret_cast<PrivateProviderSetDeviceActionEventCallbackFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_device_action_event_set_callback"));
    api.set_device_event_callback = reinterpret_cast<PrivateProviderSetDeviceEventCallbackFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_device_event_set_callback"));
    api.set_twain_source_event_callback = reinterpret_cast<PrivateProviderSetTwainSourceEventCallbackFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_twain_source_event_set_callback"));
    api.set_twain_scan_event_callback = reinterpret_cast<PrivateProviderSetTwainScanEventCallbackFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_twain_scan_event_set_callback"));
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

uint64_t UInt64Field(const Json& object, const char* key, uint64_t fallback = 0) {
    if (!object.is_object()) {
        return fallback;
    }
    Json::const_iterator it = object.find(key);
    return it != object.end() && it->is_number_unsigned() ? it->get<uint64_t>() : fallback;
}

double DoubleField(const Json& object, const char* key, double fallback = 0.0) {
    if (!object.is_object()) {
        return fallback;
    }
    Json::const_iterator it = object.find(key);
    return it != object.end() && it->is_number() ? it->get<double>() : fallback;
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

Json TwainCapabilitySetItemToJson(const SdkTwainCapabilitySetItem& item) {
    return Json{{"cap", item.cap}, {"name", item.name}, {"value_json", item.value_json}};
}

Json TwainCapabilitiesSetItemsToJson(const std::vector<SdkTwainCapabilitySetItem>& items) {
    Json values = Json::array();
    for (std::vector<SdkTwainCapabilitySetItem>::const_iterator it = items.begin(); it != items.end(); ++it) {
        values.push_back(TwainCapabilitySetItemToJson(*it));
    }
    return values;
}

SdkCaptureAsset TwainAssetFromJson(const Json& value) {
    SdkCaptureAsset asset;
    asset.asset_id = StringField(value, "asset_id");
    asset.kind = StringField(value, "kind");
    asset.path = StringField(value, "path");
    asset.url = StringField(value, "url");
    asset.download_url = StringField(value, "download_url");
    asset.content_type = StringField(value, "content_type");
    asset.width = IntField(value, "width");
    asset.height = IntField(value, "height");
    asset.size = UInt64Field(value, "size");
    return asset;
}

SdkTwainSource TwainSourceFromJson(const Json& value) {
    SdkTwainSource source;
    source.source_id = StringField(value, "source_id");
    source.source_name = StringField(value, "source_name");
    source.manufacturer = StringField(value, "manufacturer");
    source.product_family = StringField(value, "product_family");
    source.product_name = StringField(value, "product_name");
    source.protocol_major = IntField(value, "protocol_major");
    source.protocol_minor = IntField(value, "protocol_minor");
    source.status = StringField(value, "status", source.status);
    source.openable = BoolField(value, "openable", source.openable);
    return source;
}

std::vector<SdkTwainSource> TwainSourcesFromJson(const Json& value) {
    std::vector<SdkTwainSource> sources;
    if (!value.is_array()) {
        return sources;
    }
    for (Json::const_iterator it = value.begin(); it != value.end(); ++it) {
        if (it->is_object()) {
            sources.push_back(TwainSourceFromJson(*it));
        }
    }
    return sources;
}

SdkTwainCapabilityConstraint TwainCapabilityConstraintFromJson(const Json& value) {
    SdkTwainCapabilityConstraint constraint;
    constraint.type = StringField(value, "type", constraint.type);
    constraint.min = DoubleField(value, "min");
    constraint.max = DoubleField(value, "max");
    constraint.quant = DoubleField(value, "quant");
    constraint.values_json = StringArrayField(value, "values_json");
    return constraint;
}

SdkTwainCapability TwainCapabilityFromJson(const Json& value) {
    SdkTwainCapability capability;
    capability.cap = StringField(value, "cap");
    capability.cap_id = IntField(value, "cap_id");
    capability.name = StringField(value, "name");
    capability.title = StringField(value, "title");
    capability.type = StringField(value, "type");
    capability.value_json = StringField(value, "value_json");
    capability.constraint = TwainCapabilityConstraintFromJson(value.value("constraint", Json::object()));
    capability.readonly = BoolField(value, "readonly");
    capability.settable = BoolField(value, "settable", capability.settable);
    capability.advanced = BoolField(value, "advanced");
    return capability;
}

SdkTwainCapabilitySetItem TwainCapabilitySetItemFromJson(const Json& value) {
    SdkTwainCapabilitySetItem item;
    item.cap = StringField(value, "cap");
    item.name = StringField(value, "name");
    item.value_json = StringField(value, "value_json");
    return item;
}

SdkTwainCapabilitySetResultItem TwainCapabilitySetResultItemFromJson(const Json& value) {
    SdkTwainCapabilitySetResultItem item;
    item.cap = StringField(value, "cap");
    item.name = StringField(value, "name");
    item.status = StringField(value, "status");
    item.message = StringField(value, "message");
    item.value_json = StringField(value, "value_json");
    return item;
}

SdkTwainProfile TwainProfileFromJson(const Json& value) {
    SdkTwainProfile profile;
    profile.profile_id = StringField(value, "profile_id");
    profile.source_key = StringField(value, "source_key");
    profile.name = StringField(value, "name");
    profile.created_at = StringField(value, "created_at");
    profile.updated_at = StringField(value, "updated_at");
    Json::const_iterator capabilities_it = value.find("capabilities");
    if (capabilities_it != value.end() && capabilities_it->is_array()) {
        for (Json::const_iterator it = capabilities_it->begin(); it != capabilities_it->end(); ++it) {
            if (it->is_object()) {
                profile.capabilities.push_back(TwainCapabilitySetItemFromJson(*it));
            }
        }
    }
    return profile;
}

SdkTwainScanTask TwainScanTaskFromJson(const Json& value) {
    SdkTwainScanTask task;
    task.task_id = StringField(value, "task_id");
    task.connection_id = StringField(value, "connection_id");
    task.session_id = StringField(value, "session_id");
    task.status = StringField(value, "status", task.status);
    task.phase = StringField(value, "phase", task.phase);
    task.progress = IntField(value, "progress");
    task.page_count = IntField(value, "page_count");
    task.current_page = IntField(value, "current_page");
    task.output_type = StringField(value, "output_type", task.output_type);
    task.output_format = StringField(value, "output_format");
    task.output_dir = StringField(value, "output_dir");
    task.export_type = StringField(value, "export_type", task.export_type);
    task.output_path = StringField(value, "output_path");
    task.output_paths = StringArrayField(value, "output_paths");
    task.last_page_path = StringField(value, "last_page_path");
    task.message = StringField(value, "message", task.message);
    task.error = StringField(value, "error");
    task.started_at = StringField(value, "started_at");
    task.updated_at = StringField(value, "updated_at");
    task.cancel_requested = BoolField(value, "cancel_requested");
    task.ui_required = BoolField(value, "ui_required");
    task.transfer_mechanism = StringField(value, "transfer_mechanism", task.transfer_mechanism);
    Json::const_iterator assets_it = value.find("assets");
    if (assets_it != value.end() && assets_it->is_array()) {
        for (Json::const_iterator it = assets_it->begin(); it != assets_it->end(); ++it) {
            if (it->is_object()) {
                task.assets.push_back(TwainAssetFromJson(*it));
            }
        }
    }
    return task;
}

Json TwainProfileRequestToJson(const SdkTwainProfileRequest& request) {
    return Json{{"session_id", request.session_id},
                {"source_id", request.source_id},
                {"source_key", request.source_key},
                {"profile_id", request.profile_id},
                {"name", request.name},
                {"capabilities", TwainCapabilitiesSetItemsToJson(request.capabilities)}};
}

Json TwainScanRequestToJson(const SdkTwainScanRequest& request) {
    return Json{{"connection_id", request.connection_id},
                {"session_id", request.session_id},
                {"show_ui", request.show_ui},
                {"transfer_mechanism", request.transfer_mechanism},
                {"capabilities", TwainCapabilitiesSetItemsToJson(request.capabilities)},
                {"output_type", request.output_type},
                {"output_format", request.output_format},
                {"output_path", request.output_path},
                {"output_dir", request.output_dir},
                {"export_type", request.export_type}};
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

class WindowsPrivateStorageProvider : public ISdkStorageProvider {
public:
    std::string ProviderName() const override { return "czur-private-storage-provider"; }

    SdkStorageCleanupResult CleanupTemp(const SdkStorageCleanupRequest& request) override {
        // Windows Open 进程不直接递归删除目录，通过 DLL C API 复用 private 层的任务检查和路径防护。
        SdkStorageCleanupResult result;
        PrivateProvidersCApi& api = GetPrivateProvidersCApi();
        Json response;
        std::string error;
        if (!InvokePrivateProviderCApi(api.storage_cleanup_temp,
                                       Json{{"work_dir", request.work_dir}},
                                       &response,
                                       &error)) {
            result.code = ToCode(SdkStatusCode::ProviderNotReady);
            result.message = error;
            return result;
        }
        result.code = IntField(response, "code");
        result.message = StringField(response, "message");
        result.capture_removed = BoolField(response, "capture_removed");
        result.tasks_removed = BoolField(response, "tasks_removed");
        result.cleared_task_count = static_cast<std::size_t>(Int64Field(response, "cleared_task_count"));
        result.cleared_ocr_task_count = static_cast<std::size_t>(Int64Field(response, "cleared_ocr_task_count"));
        result.cleared_twain_task_count = static_cast<std::size_t>(Int64Field(response, "cleared_twain_task_count"));
        const Json active = response.value("active", Json::object());
        result.active.capture = static_cast<std::size_t>(Int64Field(active, "capture"));
        result.active.ocr = static_cast<std::size_t>(Int64Field(active, "ocr"));
        result.active.sane = static_cast<std::size_t>(Int64Field(active, "sane"));
        result.active.twain = static_cast<std::size_t>(Int64Field(active, "twain"));
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

// SDK Open 的 Windows TWAIN provider 只跨过 private DLL 边界：
// 这里不 include/link module/twain，避免把真实 TWAIN 状态机带回开源层。
class WindowsPrivateTwainProvider : public ISdkTwainProvider {
public:
    ~WindowsPrivateTwainProvider() override {
        RegisterSourceEventCallback(false);
        RegisterScanEventCallback(false);
    }

    std::string ProviderName() const override { return "czur-private-windows-twain-provider"; }

    void SetSourceEventSink(SdkTwainSourceEventCallback sink) override {
        {
            std::lock_guard<std::mutex> lock(source_event_sink_mu_);
            source_event_sink_ = sink;
        }
        RegisterSourceEventCallback(static_cast<bool>(sink));
    }

    void SetScanTaskEventSink(SdkTwainScanTaskEventCallback sink) override {
        {
            std::lock_guard<std::mutex> lock(scan_event_sink_mu_);
            scan_event_sink_ = sink;
        }
        RegisterScanEventCallback(static_cast<bool>(sink));
    }

    SdkTwainStatusResult GetStatus() override {
        SdkTwainStatusResult result;
        Json response;
        std::string error;
        if (!InvokePrivateProviderCApi(GetPrivateProvidersCApi().twain_status, Json::object(), &response, &error)) {
            result.code = ToCode(SdkStatusCode::ProviderNotReady);
            result.message = error;
            return result;
        }
        result.code = IntField(response, "code");
        result.message = StringField(response, "message");
        result.available = BoolField(response, "available");
        result.platform = StringField(response, "platform");
        result.supported_platforms = StringArrayField(response, "supported_platforms");
        result.dsm_loaded = BoolField(response, "dsm_loaded");
        result.dsm_path = StringField(response, "dsm_path");
        result.twain_version = StringField(response, "twain_version");
        result.reason = StringField(response, "reason");
        return result;
    }

    SdkTwainListResult ListSources(const SdkTwainListRequest& request) override {
        SdkTwainListResult result;
        Json response;
        std::string error;
        if (!InvokePrivateProviderCApi(GetPrivateProvidersCApi().twain_list,
                                       Json{{"refresh", request.refresh}},
                                       &response,
                                       &error)) {
            result.code = ToCode(SdkStatusCode::ProviderNotReady);
            result.message = error;
            return result;
        }
        result.code = IntField(response, "code");
        result.message = StringField(response, "message");
        result.generation = IntField(response, "generation");
        result.sources = TwainSourcesFromJson(response.value("sources", Json::array()));
        return result;
    }

    SdkTwainWatchResult WatchStart(const SdkTwainWatchRequest& request) override {
        return Watch(GetPrivateProvidersCApi().twain_watch_start, request);
    }

    SdkTwainWatchResult WatchStop(const SdkTwainWatchRequest& request) override {
        return Watch(GetPrivateProvidersCApi().twain_watch_stop, request);
    }

    SdkTwainOpenResult OpenSource(const SdkTwainOpenRequest& request) override {
        SdkTwainOpenResult result;
        Json response;
        std::string error;
        if (!InvokePrivateProviderCApi(GetPrivateProvidersCApi().twain_open,
                                       Json{{"source_id", request.source_id}},
                                       &response,
                                       &error)) {
            result.code = ToCode(SdkStatusCode::ProviderNotReady);
            result.message = error;
            return result;
        }
        result.code = IntField(response, "code");
        result.message = StringField(response, "message");
        result.opened = BoolField(response, "opened");
        result.session_id = StringField(response, "session_id");
        result.source = TwainSourceFromJson(response.value("source", Json::object()));
        return result;
    }

    SdkTwainCloseResult CloseSource(const SdkTwainCloseRequest& request) override {
        SdkTwainCloseResult result;
        Json response;
        std::string error;
        if (!InvokePrivateProviderCApi(GetPrivateProvidersCApi().twain_close,
                                       Json{{"session_id", request.session_id}},
                                       &response,
                                       &error)) {
            result.code = ToCode(SdkStatusCode::ProviderNotReady);
            result.message = error;
            return result;
        }
        result.code = IntField(response, "code");
        result.message = StringField(response, "message");
        result.closed = BoolField(response, "closed");
        result.was_opened = BoolField(response, "was_opened");
        return result;
    }

    SdkTwainGetCapabilitiesResult GetCapabilities(const SdkTwainGetCapabilitiesRequest& request) override {
        SdkTwainGetCapabilitiesResult result;
        Json response;
        std::string error;
        if (!InvokePrivateProviderCApi(GetPrivateProvidersCApi().twain_get_capabilities,
                                       Json{{"session_id", request.session_id}},
                                       &response,
                                       &error)) {
            result.code = ToCode(SdkStatusCode::ProviderNotReady);
            result.message = error;
            return result;
        }
        result.code = IntField(response, "code");
        result.message = StringField(response, "message");
        Json::const_iterator capabilities_it = response.find("capabilities");
        if (capabilities_it != response.end() && capabilities_it->is_array()) {
            for (Json::const_iterator it = capabilities_it->begin(); it != capabilities_it->end(); ++it) {
                if (it->is_object()) {
                    result.capabilities.push_back(TwainCapabilityFromJson(*it));
                }
            }
        }
        return result;
    }

    SdkTwainSetCapabilitiesResult SetCapabilities(const SdkTwainSetCapabilitiesRequest& request) override {
        SdkTwainSetCapabilitiesResult result;
        Json response;
        std::string error;
        Json body{{"session_id", request.session_id},
                  {"capabilities", TwainCapabilitiesSetItemsToJson(request.capabilities)}};
        if (!InvokePrivateProviderCApi(GetPrivateProvidersCApi().twain_set_capabilities, body, &response, &error)) {
            result.code = ToCode(SdkStatusCode::ProviderNotReady);
            result.message = error;
            return result;
        }
        result.code = IntField(response, "code");
        result.message = StringField(response, "message");
        result.applied = BoolField(response, "applied");
        result.requires_reload = BoolField(response, "requires_reload");
        Json::const_iterator results_it = response.find("results");
        if (results_it != response.end() && results_it->is_array()) {
            for (Json::const_iterator it = results_it->begin(); it != results_it->end(); ++it) {
                if (it->is_object()) {
                    result.results.push_back(TwainCapabilitySetResultItemFromJson(*it));
                }
            }
        }
        return result;
    }

    SdkTwainProfileListResult ListProfiles(const SdkTwainProfileRequest& request) override {
        SdkTwainProfileListResult result;
        Json response;
        std::string error;
        if (!InvokePrivateProviderCApi(GetPrivateProvidersCApi().twain_profile_list,
                                       TwainProfileRequestToJson(request),
                                       &response,
                                       &error)) {
            result.code = ToCode(SdkStatusCode::ProviderNotReady);
            result.message = error;
            return result;
        }
        result.code = IntField(response, "code");
        result.message = StringField(response, "message");
        Json::const_iterator profiles_it = response.find("profiles");
        if (profiles_it != response.end() && profiles_it->is_array()) {
            for (Json::const_iterator it = profiles_it->begin(); it != profiles_it->end(); ++it) {
                if (it->is_object()) {
                    result.profiles.push_back(TwainProfileFromJson(*it));
                }
            }
        }
        return result;
    }

    SdkTwainProfileResult SaveProfile(const SdkTwainProfileRequest& request) override {
        return ProfileMutation(GetPrivateProvidersCApi().twain_profile_save, request);
    }

    SdkTwainProfileResult ApplyProfile(const SdkTwainProfileRequest& request) override {
        return ProfileMutation(GetPrivateProvidersCApi().twain_profile_apply, request);
    }

    SdkTwainProfileResult DeleteProfile(const SdkTwainProfileRequest& request) override {
        return ProfileMutation(GetPrivateProvidersCApi().twain_profile_delete, request);
    }

    SdkTwainScanResult Scan(const SdkTwainScanRequest& request) override {
        return ScanMutation(GetPrivateProvidersCApi().twain_scan, TwainScanRequestToJson(request));
    }

    SdkTwainScanResult GetScan(const SdkTwainScanGetRequest& request) override {
        return ScanMutation(GetPrivateProvidersCApi().twain_scan_get, Json{{"task_id", request.task_id}});
    }

    SdkTwainScanResult CancelScan(const SdkTwainScanCancelRequest& request) override {
        return ScanMutation(GetPrivateProvidersCApi().twain_scan_cancel, Json{{"task_id", request.task_id}});
    }

private:
    SdkTwainWatchResult Watch(PrivateProviderJsonFn fn, const SdkTwainWatchRequest& request) {
        SdkTwainWatchResult result;
        Json response;
        std::string error;
        Json body{{"connection_id", request.connection_id}, {"enabled", request.enabled}};
        if (!InvokePrivateProviderCApi(fn, body, &response, &error)) {
            result.code = ToCode(SdkStatusCode::ProviderNotReady);
            result.message = error;
            return result;
        }
        result.code = IntField(response, "code");
        result.message = StringField(response, "message");
        result.watching = BoolField(response, "watching");
        result.generation = IntField(response, "generation");
        return result;
    }

    SdkTwainProfileResult ProfileMutation(PrivateProviderJsonFn fn, const SdkTwainProfileRequest& request) {
        SdkTwainProfileResult result;
        Json response;
        std::string error;
        if (!InvokePrivateProviderCApi(fn, TwainProfileRequestToJson(request), &response, &error)) {
            result.code = ToCode(SdkStatusCode::ProviderNotReady);
            result.message = error;
            return result;
        }
        result.code = IntField(response, "code");
        result.message = StringField(response, "message");
        result.applied = BoolField(response, "applied");
        result.saved = BoolField(response, "saved");
        result.deleted = BoolField(response, "deleted");
        result.profile = TwainProfileFromJson(response.value("profile", Json::object()));
        return result;
    }

    SdkTwainScanResult ScanMutation(PrivateProviderJsonFn fn, const Json& body) {
        SdkTwainScanResult result;
        Json response;
        std::string error;
        if (!InvokePrivateProviderCApi(fn, body, &response, &error)) {
            result.code = ToCode(SdkStatusCode::ProviderNotReady);
            result.message = error;
            return result;
        }
        result.code = IntField(response, "code");
        result.message = StringField(response, "message");
        result.accepted = BoolField(response, "accepted");
        result.task_id = StringField(response, "task_id");
        result.task = TwainScanTaskFromJson(response.value("task", Json::object()));
        return result;
    }

    static void SourceEventThunk(const char* event_json, void* user_data) {
        WindowsPrivateTwainProvider* provider = reinterpret_cast<WindowsPrivateTwainProvider*>(user_data);
        if (provider == NULL || event_json == NULL) {
            return;
        }
        Json event_value;
        std::string parse_error;
        if (!TryParseJson(std::string(event_json), &event_value, &parse_error) || !event_value.is_object()) {
            return;
        }
        SdkTwainSourceEvent event;
        event.code = IntField(event_value, "code");
        event.message = StringField(event_value, "message");
        event.connection_id = StringField(event_value, "connection_id");
        event.event_name = StringField(event_value, "event_name", event.event_name);
        event.generation = IntField(event_value, "generation");
        event.sources = TwainSourcesFromJson(event_value.value("sources", Json::array()));
        event.added_sources = TwainSourcesFromJson(event_value.value("added_sources", Json::array()));
        event.removed_sources = TwainSourcesFromJson(event_value.value("removed_sources", Json::array()));
        provider->PublishSourceEvent(event);
    }

    static void ScanEventThunk(const char* event_json, void* user_data) {
        WindowsPrivateTwainProvider* provider = reinterpret_cast<WindowsPrivateTwainProvider*>(user_data);
        if (provider == NULL || event_json == NULL) {
            return;
        }
        Json event_value;
        std::string parse_error;
        if (!TryParseJson(std::string(event_json), &event_value, &parse_error) || !event_value.is_object()) {
            return;
        }
        SdkTwainScanTaskEvent event;
        event.code = IntField(event_value, "code");
        event.message = StringField(event_value, "message");
        event.connection_id = StringField(event_value, "connection_id");
        event.event_name = StringField(event_value, "event_name", event.event_name);
        event.task = TwainScanTaskFromJson(event_value.value("task", Json::object()));
        provider->PublishScanEvent(event);
    }

    void RegisterSourceEventCallback(bool enabled) {
        PrivateProvidersCApi& api = GetPrivateProvidersCApi();
        if (api.set_twain_source_event_callback == NULL) {
            return;
        }
        if (enabled) {
            api.set_twain_source_event_callback(SourceEventThunk, this);
        } else {
            api.set_twain_source_event_callback(NULL, NULL);
        }
    }

    void RegisterScanEventCallback(bool enabled) {
        PrivateProvidersCApi& api = GetPrivateProvidersCApi();
        if (api.set_twain_scan_event_callback == NULL) {
            return;
        }
        if (enabled) {
            api.set_twain_scan_event_callback(ScanEventThunk, this);
        } else {
            api.set_twain_scan_event_callback(NULL, NULL);
        }
    }

    void PublishSourceEvent(const SdkTwainSourceEvent& event) {
        SdkTwainSourceEventCallback sink;
        {
            std::lock_guard<std::mutex> lock(source_event_sink_mu_);
            sink = source_event_sink_;
        }
        if (sink) {
            sink(event);
        }
    }

    void PublishScanEvent(const SdkTwainScanTaskEvent& event) {
        SdkTwainScanTaskEventCallback sink;
        {
            std::lock_guard<std::mutex> lock(scan_event_sink_mu_);
            sink = scan_event_sink_;
        }
        if (sink) {
            sink(event);
        }
    }

    std::mutex source_event_sink_mu_;
    std::mutex scan_event_sink_mu_;
    SdkTwainSourceEventCallback source_event_sink_;
    SdkTwainScanTaskEventCallback scan_event_sink_;
};

#endif

} // namespace

ProviderBundle CreateProviderBundle() {
    ProviderBundle bundle;
#if defined(_WIN32)
    bundle.device_provider = std::make_shared<WindowsPrivateDeviceProvider>();
    bundle.image_enhance_provider = std::make_shared<WindowsPrivateImageEnhanceProvider>();
    bundle.ocr_provider = std::make_shared<WindowsPrivateOcrProvider>();
    bundle.storage_provider = std::make_shared<WindowsPrivateStorageProvider>();
    bundle.twain_provider = std::make_shared<WindowsPrivateTwainProvider>();
#endif
    return bundle;
}

} // namespace private_windows
} // namespace sdk
} // namespace editor
