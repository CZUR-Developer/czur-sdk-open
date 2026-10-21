// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ocr_facade.h"

#include "sdk_ocr_language.h"

namespace editor {
namespace sdk {

OcrFacade::OcrFacade(const ProviderBundle& providers)
    : providers_(providers) {}

SdkOcrRecognizeResult OcrFacade::Recognize(const SdkOcrRecognizeRequest& request) const {
    if (!IsSupportedSdkOcrLanguage(request.language)) {
        SdkOcrRecognizeResult result;
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "unsupported ocr language";
        return result;
    }
    if (!providers_.ocr_provider) {
        SdkOcrRecognizeResult result;
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "provider not ready";
        return result;
    }
    return providers_.ocr_provider->Recognize(request);
}

SdkOcrGetResult OcrFacade::GetTask(const SdkOcrGetRequest& request) const {
    if (!providers_.ocr_provider) {
        SdkOcrGetResult result;
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "provider not ready";
        return result;
    }
    return providers_.ocr_provider->GetTask(request);
}

SdkOcrCancelResult OcrFacade::Cancel(const SdkOcrCancelRequest& request) const {
    if (!providers_.ocr_provider) {
        SdkOcrCancelResult result;
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "provider not ready";
        return result;
    }
    return providers_.ocr_provider->Cancel(request);
}

SdkOcrExtractTextResult OcrFacade::ExtractText(const SdkOcrExtractTextRequest& request) const {
    if (!IsSupportedSdkOcrLanguage(request.language)) {
        SdkOcrExtractTextResult result;
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "unsupported ocr language";
        return result;
    }
    if (!providers_.ocr_provider) {
        SdkOcrExtractTextResult result;
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "provider not ready";
        return result;
    }
    return providers_.ocr_provider->ExtractText(request);
}

} // namespace sdk
} // namespace editor
