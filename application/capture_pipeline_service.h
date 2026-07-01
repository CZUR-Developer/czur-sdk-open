// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "device_facade.h"
#include "graphic_facade.h"
#include "sdk_auth_types.h"
#include "sdk_provider_types.h"

namespace editor {
namespace sdk {

struct CapturePipelineRequest {
    std::string task_id;
    std::string device_id;
    std::string output_dir;
    bool include_base64 = false;
    int timeout_ms = 15000;
    AuthContext auth_context;
    SdkCaptureProfile profile;
    // 硬拍已由 provider 取得原始图时使用。pipeline 直接写入当前 task
    // 输出目录，再继续走与普通 capture.take 相同的处理阶段。
    SdkCaptureResult raw_capture;
};

struct CapturePipelineResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    std::string status = "succeeded";
    std::vector<SdkCaptureStageResult> stages;
    std::vector<SdkCaptureAsset> assets;
    std::vector<std::string> warnings;
};

using CaptureStageCallback = std::function<void(const SdkCaptureStageResult&)>;

class CapturePipelineService {
public:
    explicit CapturePipelineService(const ProviderBundle& providers);

    CapturePipelineResult Run(const CapturePipelineRequest& request, CaptureStageCallback stage_callback) const;

private:
    DeviceFacade device_facade_;
    GraphicFacade graphic_facade_;
    ProviderBundle providers_;
};

} // namespace sdk
} // namespace editor
