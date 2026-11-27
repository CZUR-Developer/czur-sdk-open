// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

#include "sdk_json_utils.h"
#include "sdk_status_code.h"

namespace editor {
namespace sdk {

struct ImageEnhanceWorkflowResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    Json data = Json::object();
};

ImageEnhanceWorkflowResult ListImageEnhanceWorkflows(bool strict = false);
ImageEnhanceWorkflowResult GetImageEnhanceWorkflow(const std::string& workflow_id, bool strict = false);
ImageEnhanceWorkflowResult SaveImageEnhanceWorkflow(Json workflow, bool strict = false);
ImageEnhanceWorkflowResult DeleteImageEnhanceWorkflow(const std::string& workflow_id, bool strict = false);

} // namespace sdk
} // namespace editor
