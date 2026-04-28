// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "graphic_facade.h"

namespace editor {
namespace sdk {

GraphicFacade::GraphicFacade(const ProviderBundle& providers)
    : providers_(providers) {}

SdkImageProcessResult GraphicFacade::Process(const SdkImageProcessRequest& request) const {
    if (!providers_.graphic_provider) {
        SdkImageProcessResult result;
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "provider not ready";
        return result;
    }
    return providers_.graphic_provider->Process(request);
}

SdkPageProcessResult GraphicFacade::ProcessPage(const SdkPageProcessRequest& request) const {
    if (!providers_.graphic_provider) {
        SdkPageProcessResult result;
        result.code = ToCode(SdkStatusCode::UnsupportedMethod);
        result.message = "page processing provider not ready";
        result.unsupported = true;
        return result;
    }
    return providers_.graphic_provider->ProcessPage(request);
}

SdkColorModeResult GraphicFacade::ApplyColorMode(const SdkColorModeRequest& request) const {
    if (!providers_.graphic_provider) {
        SdkColorModeResult result;
        result.code = ToCode(SdkStatusCode::UnsupportedMethod);
        result.message = "color provider not ready";
        result.unsupported = true;
        return result;
    }
    return providers_.graphic_provider->ApplyColorMode(request);
}

SdkFormatConvertResult GraphicFacade::ConvertImageFormat(const SdkFormatConvertRequest& request) const {
    if (!providers_.graphic_provider) {
        SdkFormatConvertResult result;
        result.code = ToCode(SdkStatusCode::ProviderNotReady);
        result.message = "format provider not ready";
        return result;
    }
    return providers_.graphic_provider->ConvertImageFormat(request);
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
