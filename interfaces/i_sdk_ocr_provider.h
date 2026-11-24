// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <vector>

#include "sdk_provider_types.h"

namespace editor {
namespace sdk {

class ISdkOcrProvider {
public:
    virtual ~ISdkOcrProvider() = default;
    virtual std::string ProviderName() const = 0;
    virtual SdkOcrRecognizeResult Recognize(const SdkOcrRecognizeRequest& request) = 0;
    virtual SdkOcrGetResult GetTask(const SdkOcrGetRequest& request) = 0;
    virtual bool SupportsTaskWait() const { return false; }
    // Providers that return true from SupportsTaskWait() may use this hook as a
    // quiescence barrier. Ok means the underlying task completion callback has
    // crossed its write/callback boundary; it is stronger than a cancelled task
    // snapshot and weaker than process-wide OCR manager shutdown.
    virtual SdkOcrGetResult WaitForTask(const SdkOcrGetRequest&) {
        SdkOcrGetResult result;
        result.code = ToCode(SdkStatusCode::UnsupportedMethod);
        result.message = "ocr task wait is not supported";
        return result;
    }
    virtual SdkOcrCancelResult Cancel(const SdkOcrCancelRequest& request) = 0;
    virtual SdkOcrExtractTextResult ExtractText(const SdkOcrExtractTextRequest& request) = 0;
};

} // namespace sdk
} // namespace editor
