// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "graphic_facade.h"

#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
#include <windows.h>

#include "sdk_json_utils.h"
#endif

namespace editor {
namespace sdk {

namespace {

#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)

typedef const char* (*PrivateGraphicJsonFn)(const char*);
typedef void (*PrivateGraphicFreeStringFn)(const char*);

struct PrivateGraphicCApi {
    HMODULE module = NULL;
    PrivateGraphicJsonFn process = NULL;
    PrivateGraphicJsonFn process_page = NULL;
    PrivateGraphicJsonFn apply_color_mode = NULL;
    PrivateGraphicJsonFn convert_image_format = NULL;
    PrivateGraphicFreeStringFn free_string = NULL;
};

PrivateGraphicCApi& GetPrivateGraphicCApi() {
    static PrivateGraphicCApi api;
    static bool loaded = false;
    if (loaded) {
        return api;
    }
    loaded = true;
    api.module = ::LoadLibraryA("sdk_private_providers.dll");
    if (api.module == NULL) {
        return api;
    }
    api.process = reinterpret_cast<PrivateGraphicJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_graphic_process_json"));
    api.process_page = reinterpret_cast<PrivateGraphicJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_graphic_process_page_json"));
    api.apply_color_mode = reinterpret_cast<PrivateGraphicJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_graphic_apply_color_mode_json"));
    api.convert_image_format = reinterpret_cast<PrivateGraphicJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_graphic_convert_image_format_json"));
    api.free_string = reinterpret_cast<PrivateGraphicFreeStringFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_providers_free_string"));
    return api;
}

std::string StringField(const Json& object, const char* key) {
    if (!object.is_object()) {
        return "";
    }
    Json::const_iterator it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

int IntField(const Json& object, const char* key, int fallback = 0) {
    if (!object.is_object()) {
        return fallback;
    }
    Json::const_iterator it = object.find(key);
    return it != object.end() && it->is_number_integer() ? it->get<int>() : fallback;
}

uint64_t UInt64Field(const Json& object, const char* key, uint64_t fallback = 0) {
    if (!object.is_object()) {
        return fallback;
    }
    Json::const_iterator it = object.find(key);
    return it != object.end() && it->is_number_unsigned() ? it->get<uint64_t>() : fallback;
}

bool BoolField(const Json& object, const char* key, bool fallback = false) {
    if (!object.is_object()) {
        return fallback;
    }
    Json::const_iterator it = object.find(key);
    return it != object.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

Json PointToJson(const SdkPoint2f& point) {
    return Json{{"x", point.x}, {"y", point.y}};
}

Json RectToJson(const SdkRect4P& rect) {
    return Json{{"left_top", PointToJson(rect.left_top)},
                {"right_top", PointToJson(rect.right_top)},
                {"right_down", PointToJson(rect.right_down)},
                {"left_down", PointToJson(rect.left_down)}};
}

Json CropBorderToJson(const SdkCropBorderOptions& options) {
    return Json{{"enabled", options.enabled}, {"width", options.width}, {"height", options.height}};
}

Json SinglePageToJson(const SdkSinglePageOptions& options) {
    return Json{{"realtime_detect_rects", options.realtime_detect_rects},
                {"crop_border", CropBorderToJson(options.crop_border)},
                {"id_card_round_corner", options.id_card_round_corner},
                {"auto_rotate", options.auto_rotate},
                {"smart_black_edge_optimize", options.smart_black_edge_optimize},
                {"multi_target_paging", options.multi_target_paging}};
}

Json CurvedBookToJson(const SdkCurvedBookOptions& options) {
    return Json{{"remove_finger", options.remove_finger},
                {"finger_type", options.finger_type},
                {"smart_paging", options.smart_paging},
                {"crop_border", CropBorderToJson(options.crop_border)},
                {"auto_complete", options.auto_complete}};
}

Json ImageProcessRequestToJson(const SdkImageProcessRequest& request) {
    return Json{{"input_upload_id", request.input_upload_id},
                {"input_path", request.input_path},
                {"output_path", request.output_path},
                {"output_dir", request.output_dir},
                {"page_processing", request.page_processing},
                {"color_mode", request.color_mode},
                {"output_format", request.output_format},
                {"dpi", request.dpi},
                {"single_page", SinglePageToJson(request.single_page)},
                {"curved_book", CurvedBookToJson(request.curved_book)},
                {"selected_area_rect", RectToJson(request.selected_area_rect)},
                {"selected_area_source_width", request.selected_area_source_width},
                {"selected_area_source_height", request.selected_area_source_height},
                {"scan_device_type", request.scan_device_type}};
}

Json PageProcessRequestToJson(const SdkPageProcessRequest& request) {
    Json rects = Json::array();
    for (std::vector<SdkRect4P>::const_iterator it = request.detected_rects.begin();
         it != request.detected_rects.end();
         ++it) {
        rects.push_back(RectToJson(*it));
    }
    return Json{{"device_id", request.device_id},
                {"input_path", request.input_path},
                {"laser_path", request.laser_path},
                {"output_dir", request.output_dir},
                {"output_path", request.output_path},
                {"page_processing", request.page_processing},
                {"width", request.width},
                {"height", request.height},
                {"dpi", request.dpi},
                {"single_page_realtime_detect_rects", request.single_page_realtime_detect_rects},
                {"single_page", SinglePageToJson(request.single_page)},
                {"curved_book", CurvedBookToJson(request.curved_book)},
                {"detected_rects", rects},
                {"detected_rects_source_width", request.detected_rects_source_width},
                {"detected_rects_source_height", request.detected_rects_source_height},
                {"selected_area_rect", RectToJson(request.selected_area_rect)},
                {"selected_area_source_width", request.selected_area_source_width},
                {"selected_area_source_height", request.selected_area_source_height},
                {"scan_device_type", request.scan_device_type}};
}

Json ColorModeRequestToJson(const SdkColorModeRequest& request) {
    return Json{{"input_path", request.input_path},
                {"output_path", request.output_path},
                {"color_mode", request.color_mode},
                {"dpi", request.dpi}};
}

Json FormatConvertRequestToJson(const SdkFormatConvertRequest& request) {
    return Json{{"input_path", request.input_path},
                {"output_path", request.output_path},
                {"output_format", request.output_format}};
}

bool InvokePrivateGraphicCApi(PrivateGraphicJsonFn fn,
                              const Json& request,
                              Json* response,
                              std::string* message) {
    PrivateGraphicCApi& api = GetPrivateGraphicCApi();
    if (fn == NULL || api.free_string == NULL) {
        if (message != NULL) {
            *message = "private graphic c api not ready";
        }
        return false;
    }

    const char* response_ptr = fn(request.dump().c_str());
    if (response_ptr == NULL) {
        if (message != NULL) {
            *message = "private graphic c api returned null";
        }
        return false;
    }
    const std::string response_text(response_ptr);
    api.free_string(response_ptr);

    std::string parse_error;
    if (!TryParseJson(response_text, response, &parse_error) || response == NULL || !response->is_object()) {
        if (message != NULL) {
            *message = "private graphic c api returned invalid json";
        }
        return false;
    }
    return true;
}

SdkImageProcessOutput ImageProcessOutputFromJson(const Json& value) {
    SdkImageProcessOutput output;
    output.asset_id = StringField(value, "asset_id");
    output.output_id = StringField(value, "output_id");
    output.role = StringField(value, "role");
    output.index = IntField(value, "index");
    output.path = StringField(value, "path");
    output.url = StringField(value, "url");
    output.download_url = StringField(value, "download_url");
    output.content_type = StringField(value, "content_type");
    if (output.content_type.empty()) {
        output.content_type = "image/jpeg";
    }
    output.width = IntField(value, "width");
    output.height = IntField(value, "height");
    output.size = UInt64Field(value, "size");
    return output;
}

SdkPageOutput PageOutputFromJson(const Json& value) {
    SdkPageOutput output;
    output.output_id = StringField(value, "output_id");
    output.role = StringField(value, "role");
    output.index = IntField(value, "index");
    output.path = StringField(value, "path");
    output.content_type = StringField(value, "content_type");
    if (output.content_type.empty()) {
        output.content_type = "image/jpeg";
    }
    output.width = IntField(value, "width");
    output.height = IntField(value, "height");
    output.dpi = IntField(value, "dpi");
    output.size = UInt64Field(value, "size");
    return output;
}

#endif

} // namespace

GraphicFacade::GraphicFacade(const ProviderBundle& providers)
    : providers_(providers) {}

SdkImageProcessResult GraphicFacade::Process(const SdkImageProcessRequest& request) const {
#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
    SdkImageProcessResult result;
    PrivateGraphicCApi& api = GetPrivateGraphicCApi();
    Json response;
    std::string error;
    if (!InvokePrivateGraphicCApi(api.process, ImageProcessRequestToJson(request), &response, &error)) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = error;
        return result;
    }
    result.code = IntField(response, "code");
    result.message = StringField(response, "message");
    result.processed = BoolField(response, "processed");
    result.output_path = StringField(response, "output_path");
    Json::const_iterator outputs_it = response.find("outputs");
    if (outputs_it != response.end() && outputs_it->is_array()) {
        for (Json::const_iterator it = outputs_it->begin(); it != outputs_it->end(); ++it) {
            result.outputs.push_back(ImageProcessOutputFromJson(*it));
        }
    }
    return result;
#else
    if (!providers_.graphic_provider) {
        SdkImageProcessResult result;
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "provider not ready";
        return result;
    }
    return providers_.graphic_provider->Process(request);
#endif
}

SdkPageProcessResult GraphicFacade::ProcessPage(const SdkPageProcessRequest& request) const {
#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
    SdkPageProcessResult result;
    PrivateGraphicCApi& api = GetPrivateGraphicCApi();
    Json response;
    std::string error;
    if (!InvokePrivateGraphicCApi(api.process_page, PageProcessRequestToJson(request), &response, &error)) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = error;
        return result;
    }
    result.code = IntField(response, "code");
    result.message = StringField(response, "message");
    result.processed = BoolField(response, "processed");
    result.unsupported = BoolField(response, "unsupported");
    result.output_path = StringField(response, "output_path");
    Json::const_iterator outputs_it = response.find("outputs");
    if (outputs_it != response.end() && outputs_it->is_array()) {
        for (Json::const_iterator it = outputs_it->begin(); it != outputs_it->end(); ++it) {
            result.outputs.push_back(PageOutputFromJson(*it));
        }
    }
    return result;
#else
    if (!providers_.graphic_provider) {
        SdkPageProcessResult result;
        result.code = ToCode(SdkStatusCode::UnsupportedMethod);
        result.message = "page processing provider not ready";
        result.unsupported = true;
        return result;
    }
    return providers_.graphic_provider->ProcessPage(request);
#endif
}

SdkColorModeResult GraphicFacade::ApplyColorMode(const SdkColorModeRequest& request) const {
#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
    SdkColorModeResult result;
    PrivateGraphicCApi& api = GetPrivateGraphicCApi();
    Json response;
    std::string error;
    if (!InvokePrivateGraphicCApi(api.apply_color_mode, ColorModeRequestToJson(request), &response, &error)) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = error;
        return result;
    }
    result.code = IntField(response, "code");
    result.message = StringField(response, "message");
    result.processed = BoolField(response, "processed");
    result.unsupported = BoolField(response, "unsupported");
    result.output_path = StringField(response, "output_path");
    return result;
#else
    if (!providers_.graphic_provider) {
        SdkColorModeResult result;
        result.code = ToCode(SdkStatusCode::UnsupportedMethod);
        result.message = "color provider not ready";
        result.unsupported = true;
        return result;
    }
    return providers_.graphic_provider->ApplyColorMode(request);
#endif
}

SdkFormatConvertResult GraphicFacade::ConvertImageFormat(const SdkFormatConvertRequest& request) const {
#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
    SdkFormatConvertResult result;
    PrivateGraphicCApi& api = GetPrivateGraphicCApi();
    Json response;
    std::string error;
    if (!InvokePrivateGraphicCApi(api.convert_image_format, FormatConvertRequestToJson(request), &response, &error)) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = error;
        return result;
    }
    result.code = IntField(response, "code");
    result.message = StringField(response, "message");
    result.converted = BoolField(response, "converted");
    result.passthrough = BoolField(response, "passthrough");
    result.output_path = StringField(response, "output_path");
    return result;
#else
    if (!providers_.graphic_provider) {
        SdkFormatConvertResult result;
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "format provider not ready";
        return result;
    }
    return providers_.graphic_provider->ConvertImageFormat(request);
#endif
}

SdkThumbnailResult GraphicFacade::GenerateThumbnail(const SdkThumbnailRequest& request) const {
    if (!providers_.graphic_provider) {
        SdkThumbnailResult result;
        result.code = ToCode(SdkStatusCode::UnsupportedMethod);
        result.message = "thumbnail provider not ready";
        return result;
    }
    return providers_.graphic_provider->GenerateThumbnail(request);
}

} // namespace sdk
} // namespace editor
