// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

namespace editor {
namespace sdk {

std::string JoinPath(const std::string& base, const std::string& leaf);
bool EnsureDirectoryRecursive(const std::string& path);

const std::string& GetSdkOpenWorkDir();
std::string ResolveSdkOpenLogDir();
std::string GetSdkOpenCaptureDir();
std::string GetSdkOpenCaptureTaskDir(const std::string& task_id);

} // namespace sdk
} // namespace editor
