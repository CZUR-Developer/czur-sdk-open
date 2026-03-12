#pragma once

#include <memory>

#include "i_sdk_device_provider.h"
#include "i_sdk_graphic_provider.h"
#include "i_sdk_ocr_provider.h"
#include "i_sdk_ofd_provider.h"

namespace editor {
namespace sdk {

struct ProviderBundle {
    std::shared_ptr<ISdkDeviceProvider> device_provider;
    std::shared_ptr<ISdkGraphicProvider> graphic_provider;
    std::shared_ptr<ISdkOcrProvider> ocr_provider;
    std::shared_ptr<ISdkOfdProvider> ofd_provider;
};

} // namespace sdk
} // namespace editor

