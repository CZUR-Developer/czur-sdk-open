// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "capture_task_service.h"

#include <ctime>

namespace editor {
namespace sdk {

namespace {

Json BuildAssetJson(const SdkCaptureAsset& asset) {
    return Json{{"asset_id", asset.asset_id},
                {"kind", asset.kind},
                {"path", asset.path},
                {"content_type", asset.content_type},
                {"width", asset.width},
                {"height", asset.height},
                {"size", asset.size}};
}

Json BuildStageJson(const SdkCaptureStageResult& stage) {
    return Json{{"name", stage.name},
                {"status", stage.status},
                {"input", stage.input_assets},
                {"output", stage.output_assets},
                {"provider", stage.provider},
                {"message", stage.message}};
}

Json BuildTaskJson(const CaptureTaskSnapshot& task) {
    Json stages = Json::array();
    for (std::vector<SdkCaptureStageResult>::const_iterator it = task.stages.begin(); it != task.stages.end(); ++it) {
        stages.push_back(BuildStageJson(*it));
    }
    Json assets = Json::array();
    for (std::vector<SdkCaptureAsset>::const_iterator it = task.assets.begin(); it != task.assets.end(); ++it) {
        assets.push_back(BuildAssetJson(*it));
    }
    return Json{{"task_id", task.task_id},
                {"status", task.status},
                {"device_id", task.device_id},
                {"profile_revision", task.profile_revision},
                {"stages", stages},
                {"assets", assets},
                {"warnings", task.warnings},
                {"error", task.error}};
}

} // namespace

CaptureTaskService::CaptureTaskService(const ProviderBundle& providers)
    : pipeline_service_(providers),
      next_task_seq_(1) {}

CaptureTaskService::~CaptureTaskService() {
    for (std::vector<std::thread>::iterator it = workers_.begin(); it != workers_.end(); ++it) {
        if (it->joinable()) {
            it->join();
        }
    }
}

void CaptureTaskService::SetEventSink(EventSink sink) {
    std::lock_guard<std::mutex> lock(mu_);
    event_sink_ = sink;
}

CaptureTaskStartResult CaptureTaskService::StartTask(const CaptureTaskStartRequest& request) {
    CaptureTaskStartResult result;
    if (request.device_id.empty()) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "device_id required";
        return result;
    }
    if (request.profile.profile_version != "capture.profile.v1") {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "unsupported capture profile";
        return result;
    }

    const std::string task_id = NextTaskId();
    CaptureTaskSnapshot task;
    task.task_id = task_id;
    task.connection_id = request.connection_id;
    task.device_id = request.device_id;
    task.status = "queued";
    task.profile_revision = request.profile.revision;

    {
        std::lock_guard<std::mutex> lock(mu_);
        if (busy_devices_.find(request.device_id) != busy_devices_.end()) {
            result.code = ToCode(SdkStatusCode::DeviceBusy);
            result.message = "device already has an active capture task";
            return result;
        }
        busy_devices_.insert(request.device_id);
        tasks_[task_id] = task;
        workers_.push_back(std::thread(&CaptureTaskService::RunTask, this, task_id, request));
    }

    result.accepted = true;
    result.task = task;
    return result;
}

CaptureTaskSnapshot CaptureTaskService::GetTask(const std::string& connection_id, const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(mu_);
    CaptureTaskSnapshot task = GetTaskUnlocked(task_id);
    if (!IsOkStatusCode(task.code)) {
        return task;
    }
    if (task.connection_id != connection_id) {
        task.code = ToCode(SdkStatusCode::CapabilityNotAllowed);
        task.message = "task belongs to another connection";
        return task;
    }
    return task;
}

void CaptureTaskService::RunTask(const std::string& task_id, CaptureTaskStartRequest request) {
    CaptureTaskSnapshot running;
    {
        std::lock_guard<std::mutex> lock(mu_);
        tasks_[task_id].status = "running";
        running = tasks_[task_id];
    }
    PublishEvent(request.connection_id, "capture.started", running);

    CapturePipelineRequest pipeline_request;
    pipeline_request.task_id = task_id;
    pipeline_request.device_id = request.device_id;
    pipeline_request.output_dir = request.output_dir;
    pipeline_request.include_base64 = request.include_base64;
    pipeline_request.timeout_ms = request.timeout_ms;
    pipeline_request.auth_context = request.auth_context;
    pipeline_request.profile = request.profile;

    const CapturePipelineResult pipeline_result =
        pipeline_service_.Run(pipeline_request, [this, task_id, request](const SdkCaptureStageResult& stage) {
            CaptureTaskSnapshot snapshot;
            {
                std::lock_guard<std::mutex> lock(mu_);
                std::map<std::string, CaptureTaskSnapshot>::iterator it = tasks_.find(task_id);
                if (it == tasks_.end()) {
                    return;
                }
                if (stage.status != "running") {
                    it->second.stages.push_back(stage);
                }
                snapshot = it->second;
            }
            PublishEvent(request.connection_id, "capture.stage.updated", snapshot, &stage);
        });

    CaptureTaskSnapshot final_task;
    {
        std::lock_guard<std::mutex> lock(mu_);
        CaptureTaskSnapshot& task = tasks_[task_id];
        task.status = pipeline_result.status;
        task.stages = pipeline_result.stages;
        task.assets = pipeline_result.assets;
        task.warnings = pipeline_result.warnings;
        task.code = pipeline_result.code;
        task.message = pipeline_result.message;
        if (!IsOkStatusCode(pipeline_result.code)) {
            task.error = pipeline_result.message;
        }
        busy_devices_.erase(request.device_id);
        final_task = task;
    }

    PublishEvent(request.connection_id,
                 final_task.status == "succeeded" ? "capture.completed" : "capture.failed",
                 final_task);
}

void CaptureTaskService::PublishEvent(const std::string& connection_id,
                                      const std::string& event,
                                      const CaptureTaskSnapshot& task,
                                      const SdkCaptureStageResult* stage) const {
    EventSink sink;
    {
        std::lock_guard<std::mutex> lock(mu_);
        sink = event_sink_;
    }
    if (!sink) {
        return;
    }
    Json payload = BuildTaskJson(task);
    if (stage != NULL) {
        payload["stage"] = BuildStageJson(*stage);
    }
    sink(connection_id, BuildWsEvent(event, payload));
}

CaptureTaskSnapshot CaptureTaskService::GetTaskUnlocked(const std::string& task_id) const {
    std::map<std::string, CaptureTaskSnapshot>::const_iterator it = tasks_.find(task_id);
    if (it == tasks_.end()) {
        CaptureTaskSnapshot task;
        task.code = ToCode(SdkStatusCode::InvalidParams);
        task.message = "task not found";
        return task;
    }
    return it->second;
}

std::string CaptureTaskService::NextTaskId() {
    const uint64_t seq = next_task_seq_.fetch_add(1);
    return "cap-" + std::to_string(static_cast<long long>(std::time(nullptr))) + "-" + std::to_string(seq);
}

} // namespace sdk
} // namespace editor
