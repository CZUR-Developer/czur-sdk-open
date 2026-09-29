// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

namespace editor {
namespace sdk {

std::string JoinPath(const std::string& base, const std::string& leaf);
bool EnsureDirectoryRecursive(const std::string& path);

// Configure the shared core before constructing any services. One immutable
// configuration is supported per process. Open Runtime callers need not call
// this function: their existing environment/default lookup remains unchanged.
bool ConfigureSdkCorePaths(const std::string& work_dir,
                           const std::string& log_dir,
                           bool enable_file_logging);
bool IsSdkCoreFileLoggingEnabled();

const std::string& GetSdkOpenWorkDir();
std::string ResolveSdkOpenLogDir();
std::string GetSdkOpenTasksDir();
std::string GetSdkOpenTaskDir(const std::string& module, const std::string& task_id);
std::string GetSdkOpenTaskAssetDir(const std::string& module,
                                   const std::string& task_id,
                                   const std::string& asset_group);
std::string GetSdkOpenCaptureDir();
std::string GetSdkOpenCaptureTaskDir(const std::string& task_id);

} // namespace sdk
} // namespace editor
