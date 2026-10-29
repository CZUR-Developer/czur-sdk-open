#pragma once

#include "sdk_provider_types.h"

namespace editor {
namespace sdk {
namespace private_impl {
namespace graphic {

class ThumbnailProcessor {
public:
    SdkThumbnailResult Generate(const SdkThumbnailRequest& request) const;
};

} // namespace graphic
} // namespace private_impl
} // namespace sdk
} // namespace editor
