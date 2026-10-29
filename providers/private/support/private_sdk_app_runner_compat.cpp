// Copyright (c) 2026 CZUR Tech. All rights reserved.

#include "private_sdk_support.h"

#include <string>

// devices_lib is shared with CapEditor and historically obtains its temporary
// still-image path from app_runner.  SDK Open has no CEF app_runner, so provide
// the same narrow contract from the SDK runtime directory on macOS.
namespace editor {
namespace app_runner {

std::string GetAppImageBaseDir() {
    const std::string base_dir = sdk::private_impl::GetSdkRuntimeDir() + "/capture/device";
    (void)sdk::private_impl::EnsureDirectoryRecursive(base_dir);
    return base_dir;
}

} // namespace app_runner
} // namespace editor
