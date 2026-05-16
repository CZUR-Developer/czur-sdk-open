// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "sdk_provider_bundle.h"
#include "sdk_provider_types.h"

namespace editor {
namespace sdk {

class OcrFacade {
public:
    explicit OcrFacade(const ProviderBundle& providers);

    SdkOcrRecognizeResult Recognize(const SdkOcrRecognizeRequest& request) const;
    SdkOcrGetResult GetTask(const SdkOcrGetRequest& request) const;
    SdkOcrCancelResult Cancel(const SdkOcrCancelRequest& request) const;
    SdkOcrExtractTextResult ExtractText(const SdkOcrExtractTextRequest& request) const;

private:
    ProviderBundle providers_;
};

} // namespace sdk
} // namespace editor
