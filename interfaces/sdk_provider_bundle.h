// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>

#include "i_sdk_auth_provider.h"
#include "i_sdk_device_provider.h"
#include "i_sdk_graphic_provider.h"
#include "i_sdk_image_enhance_provider.h"
#include "i_sdk_ocr_provider.h"
#include "i_sdk_ofd_provider.h"
#include "i_sdk_recognition_provider.h"
#include "i_sdk_sane_provider.h"

namespace editor {
namespace sdk {

struct ProviderBundle {
    std::shared_ptr<ISdkAuthProvider> auth_provider;
    std::shared_ptr<ISdkDeviceProvider> device_provider;
    std::shared_ptr<ISdkGraphicProvider> graphic_provider;
    std::shared_ptr<ISdkImageEnhanceProvider> image_enhance_provider;
    std::shared_ptr<ISdkOcrProvider> ocr_provider;
    std::shared_ptr<ISdkOfdProvider> ofd_provider;
    std::shared_ptr<ISdkRecognitionProvider> recognition_provider;
    std::shared_ptr<ISdkSaneProvider> sane_provider;
};

} // namespace sdk
} // namespace editor
