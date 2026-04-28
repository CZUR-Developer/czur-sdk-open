// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

#include "sdk_provider_types.h"

namespace editor {
namespace sdk {

class ISdkGraphicProvider {
public:
    virtual ~ISdkGraphicProvider() = default;
    virtual std::string ProviderName() const = 0;
    virtual SdkImageProcessResult Process(const SdkImageProcessRequest& request) = 0;
    virtual SdkPageProcessResult ProcessPage(const SdkPageProcessRequest& request) = 0;
    virtual SdkColorModeResult ApplyColorMode(const SdkColorModeRequest& request) = 0;
    virtual SdkFormatConvertResult ConvertImageFormat(const SdkFormatConvertRequest& request) = 0;
    virtual SdkThumbnailResult GenerateThumbnail(const SdkThumbnailRequest& request) = 0;
};

} // namespace sdk
} // namespace editor
