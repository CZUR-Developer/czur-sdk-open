// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "recognition_facade.h"

#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
#include <windows.h>

#include "sdk_json_utils.h"
#endif

namespace editor {
namespace sdk {

namespace {

#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)

typedef const char* (*PrivateRecognitionJsonFn)(const char*);
typedef void (*PrivateRecognitionFreeStringFn)(const char*);

struct PrivateRecognitionCApi {
    HMODULE module = NULL;
    PrivateRecognitionJsonFn detect_barcode = NULL;
    PrivateRecognitionFreeStringFn free_string = NULL;
};

PrivateRecognitionCApi& GetPrivateRecognitionCApi() {
    static PrivateRecognitionCApi api;
    static bool loaded = false;
    if (loaded) {
        return api;
    }
    loaded = true;
    api.module = ::LoadLibraryA("sdk_private_providers.dll");
    if (api.module == NULL) {
        return api;
    }
    api.detect_barcode = reinterpret_cast<PrivateRecognitionJsonFn>(
        ::GetProcAddress(api.module, "czur_sdk_private_recognition_barcode_detect_json"));
    api.free_string = reinterpret_cast<PrivateRecognitionFreeStringFn>(
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

float FloatField(const Json& object, const char* key, float fallback = 0.0f) {
    if (!object.is_object()) {
        return fallback;
    }
    Json::const_iterator it = object.find(key);
    return it != object.end() && it->is_number() ? it->get<float>() : fallback;
}

bool BoolField(const Json& object, const char* key, bool fallback = false) {
    if (!object.is_object()) {
        return fallback;
    }
    Json::const_iterator it = object.find(key);
    return it != object.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

Json BarcodeDetectRequestToJson(const SdkBarcodeDetectRequest& request) {
    return Json{{"input_upload_id", request.input_upload_id},
                {"input_path", request.input_path},
                {"formats", request.formats}};
}

bool InvokePrivateRecognitionCApi(PrivateRecognitionJsonFn fn,
                                  const Json& request,
                                  Json* response,
                                  std::string* message) {
    PrivateRecognitionCApi& api = GetPrivateRecognitionCApi();
    if (fn == NULL || api.free_string == NULL) {
        if (message != NULL) {
            *message = "private recognition c api not ready";
        }
        return false;
    }
    const char* response_ptr = fn(request.dump().c_str());
    if (response_ptr == NULL) {
        if (message != NULL) {
            *message = "private recognition c api returned null";
        }
        return false;
    }
    const std::string response_text(response_ptr);
    api.free_string(response_ptr);

    std::string parse_error;
    if (!TryParseJson(response_text, response, &parse_error) || response == NULL || !response->is_object()) {
        if (message != NULL) {
            *message = "private recognition c api returned invalid json";
        }
        return false;
    }
    return true;
}

SdkPoint2f PointFromJson(const Json& value) {
    SdkPoint2f point;
    point.x = FloatField(value, "x");
    point.y = FloatField(value, "y");
    return point;
}

SdkBarcodeResult BarcodeFromJson(const Json& value) {
    SdkBarcodeResult barcode;
    barcode.format_name = StringField(value, "format_name");
    barcode.format = IntField(value, "format");
    barcode.text = StringField(value, "text");
    Json::const_iterator points_it = value.find("points");
    if (points_it != value.end() && points_it->is_array()) {
        for (Json::const_iterator it = points_it->begin(); it != points_it->end(); ++it) {
            barcode.points.push_back(PointFromJson(*it));
        }
    }
    return barcode;
}

#endif

} // namespace

RecognitionFacade::RecognitionFacade(const ProviderBundle& providers)
    : providers_(providers) {}

SdkBarcodeDetectResult RecognitionFacade::DetectBarcode(const SdkBarcodeDetectRequest& request) const {
#if defined(_WIN32) && defined(SDK_USE_PRIVATE_PROVIDER)
    SdkBarcodeDetectResult result;
    PrivateRecognitionCApi& api = GetPrivateRecognitionCApi();
    Json response;
    std::string error;
    if (!InvokePrivateRecognitionCApi(api.detect_barcode, BarcodeDetectRequestToJson(request), &response, &error)) {
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = error;
        return result;
    }
    result.code = IntField(response, "code");
    result.message = StringField(response, "message");
    result.detected = BoolField(response, "detected");
    result.input_path = StringField(response, "input_path");
    result.width = IntField(response, "width");
    result.height = IntField(response, "height");
    Json::const_iterator barcodes_it = response.find("barcodes");
    if (barcodes_it != response.end() && barcodes_it->is_array()) {
        for (Json::const_iterator it = barcodes_it->begin(); it != barcodes_it->end(); ++it) {
            result.barcodes.push_back(BarcodeFromJson(*it));
        }
    }
    return result;
#else
    if (!providers_.recognition_provider) {
        SdkBarcodeDetectResult result;
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "provider not ready";
        return result;
    }
    return providers_.recognition_provider->DetectBarcode(request);
#endif
}

} // namespace sdk
} // namespace editor
