// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include <iostream>
#include <string>

#include "sdk_runtime_paths.h"
#include "sdk_logger.h"

namespace {

bool Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
    }
    return condition;
}

} // namespace

int main(int argc, char** argv) {
    using namespace editor::sdk;
    if (argc != 2) {
        return 2;
    }
    const std::string mode(argv[1]);
    if (mode == "default") {
        const std::string original = GetSdkOpenWorkDir();
        return Check(!original.empty(), "default work directory is empty") &&
               Check(IsSdkCoreFileLoggingEnabled(), "Open file logging default changed") &&
               Check(!ConfigureSdkCorePaths("different-local-root", "", false),
                     "cannot reconfigure a core already used by Open") &&
               Check(GetSdkOpenWorkDir() == original, "default path changed") ? 0 : 1;
    }
    if (mode != "explicit") {
        return 2;
    }
    return Check(ConfigureSdkCorePaths("local-work", "local-logs", false),
                 "initial Local configuration failed") &&
           Check(GetSdkOpenWorkDir() == "local-work", "work path not injected") &&
           Check(ResolveSdkOpenLogDir() == "local-logs", "log path not injected") &&
           Check(GetSdkOpenTasksDir() == "local-work/tasks", "task root not injected") &&
           Check(GetSdkOpenCaptureDir() == "local-work/capture", "capture root not injected") &&
           Check(!IsSdkCoreFileLoggingEnabled(), "file logging remains enabled") &&
           Check(ConfigureSdkCorePaths("local-work", "local-logs", false),
                 "same configuration is not idempotent") &&
           Check(!ConfigureSdkCorePaths("other-work", "local-logs", false),
                 "conflicting configuration accepted") &&
           Check(GetSdkOpenLogPath().empty(), "disabled file logging created a file sink") &&
           Check(GetSdkOpenWorkDir() == "local-work", "immutable configuration changed") ? 0 : 1;
}
