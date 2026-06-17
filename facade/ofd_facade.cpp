// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ofd_facade.h"

#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
#include <windows.h>

#include "sdk_json_utils.h"
#endif

namespace editor {
namespace sdk {

namespace {

#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)

typedef const char* (*PrivateFileConvertJsonFn)(const char*);
typedef void (*PrivateFileConvertFreeStringFn)(const char*);

struct PrivateFileConvertCApi {
    HMODULE module = NULL;
    PrivateFileConvertJsonFn convert = NULL;
    PrivateFileConvertFreeStringFn free_string = NULL;
};

PrivateFileConvertCApi& GetPrivateFileConvertCApi() {
    static PrivateFileConvertCApi api;
    static bool loaded = false;
    if (loaded) {
        return api;
    }
    loaded = true;
    api.module = ::LoadLibraryA("sdk_private_providers.dll");
    if (api.module == NULL) {
        return api;
    }
    api.convert = reinterpret_cast<PrivateFileConvertJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_file_convert_json"));
    api.free_string = reinterpret_cast<PrivateFileConvertFreeStringFn>(
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

Json FileConvertRequestToJson(const SdkFileConvertRequest& request) {
    return Json{{"input_upload_id", request.input_upload_id},
                {"input_upload_ids", request.input_upload_ids},
                {"input_path", request.input_path},
                {"input_paths", request.input_paths},
                {"output_path", request.output_path},
                {"output_dir", request.output_dir},
                {"output_format", request.output_format},
                {"source_type", request.source_type},
                {"source_format", request.source_format},
                {"target_type", request.target_type},
                {"export_type", request.export_type},
                {"pages", request.pages},
                {"quality", request.quality},
                {"render_dpi", request.render_dpi},
                {"tiff_color", request.tiff_color},
                {"tiff_compression", request.tiff_compression}};
}

bool InvokePrivateFileConvertCApi(const Json& request, Json* response, std::string* message) {
    PrivateFileConvertCApi& api = GetPrivateFileConvertCApi();
    if (api.convert == NULL || api.free_string == NULL) {
        if (message != NULL) {
            *message = "private file convert c api not ready";
        }
        return false;
    }
    const char* response_ptr = api.convert(request.dump().c_str());
    if (response_ptr == NULL) {
        if (message != NULL) {
            *message = "private file convert c api returned null";
        }
        return false;
    }
    const std::string response_text(response_ptr);
    api.free_string(response_ptr);

    std::string parse_error;
    if (!TryParseJson(response_text, response, &parse_error) || response == NULL || !response->is_object()) {
        if (message != NULL) {
            *message = "private file convert c api returned invalid json";
        }
        return false;
    }
    return true;
}

#endif

} // namespace

OfdFacade::OfdFacade(const ProviderBundle& providers)
    : providers_(providers) {}

SdkFileConvertResult OfdFacade::Convert(const SdkFileConvertRequest& request) const {
#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
    SdkFileConvertResult result;
    Json response;
    std::string error;
    if (!InvokePrivateFileConvertCApi(FileConvertRequestToJson(request), &response, &error)) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = error;
        return result;
    }
    result.code = IntField(response, "code");
    result.message = StringField(response, "message");
    result.accepted = IntField(response, "accepted");
    result.converted = IntField(response, "converted");
    result.output_path = StringField(response, "output_path");
    result.output_paths = StringArrayField(response, "output_paths");
    result.source_format = StringField(response, "source_format");
    result.source_page_count = IntField(response, "source_page_count");
    result.selected_page_count = IntField(response, "selected_page_count");
    return result;
#else
    if (!providers_.ofd_provider) {
        SdkFileConvertResult result;
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "provider not ready";
        return result;
    }
    return providers_.ofd_provider->Convert(request);
#endif
}

} // namespace sdk
} // namespace editor
