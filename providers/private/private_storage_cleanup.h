#pragma once

#include <string>

#include "i_sdk_storage_provider.h"

namespace editor {
namespace sdk {
namespace private_impl {

SdkStorageCleanupResult RemoveSdkOpenTempDirectories(const std::string& work_dir);

} // namespace private_impl
} // namespace sdk
} // namespace editor
