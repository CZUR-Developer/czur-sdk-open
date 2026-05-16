// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "recognition_facade.h"

namespace editor {
namespace sdk {

RecognitionFacade::RecognitionFacade(const ProviderBundle& providers)
    : providers_(providers) {}

SdkBarcodeDetectResult RecognitionFacade::DetectBarcode(const SdkBarcodeDetectRequest& request) const {
    if (!providers_.recognition_provider) {
        SdkBarcodeDetectResult result;
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "provider not ready";
        return result;
    }
    return providers_.recognition_provider->DetectBarcode(request);
}

} // namespace sdk
} // namespace editor
