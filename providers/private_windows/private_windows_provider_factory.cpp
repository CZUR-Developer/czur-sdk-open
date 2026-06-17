// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "private_windows_provider_factory.h"

#include <memory>

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

struct PrivateProvidersCApi {
    HMODULE module = NULL;
    PrivateProviderJsonFn image_enhance_capabilities = NULL;
    PrivateProviderJsonFn image_enhance_run_step = NULL;
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

#endif

} // namespace

ProviderBundle CreateProviderBundle() {
    ProviderBundle bundle;
#if defined(_WIN32)
    bundle.image_enhance_provider = std::make_shared<WindowsPrivateImageEnhanceProvider>();
#endif
    return bundle;
}

} // namespace private_windows
} // namespace sdk
} // namespace editor
