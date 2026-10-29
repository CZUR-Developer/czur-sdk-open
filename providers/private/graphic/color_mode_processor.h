#pragma once

#include "sdk_provider_types.h"

namespace editor {
namespace sdk {
namespace private_impl {
namespace graphic {

class ColorModeProcessor {
public:
    SdkColorModeResult Apply(const SdkColorModeRequest& request) const;
};

} // namespace graphic
} // namespace private_impl
} // namespace sdk
} // namespace editor
