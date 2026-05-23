// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "capture_task_service.h"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>

#include "sdk_runtime_paths.h"

namespace editor {
namespace sdk {

namespace {

Json BuildAssetJson(const SdkCaptureAsset& asset) {
    return Json{{"asset_id", asset.asset_id},
                {"kind", asset.kind},
                {"path", asset.path},
                {"url", asset.url},
                {"download_url", asset.download_url},
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

std::string TrimTrailingSlash(const std::string& value) {
    if (!value.empty() && value[value.size() - 1] == '/') {
        return value.substr(0, value.size() - 1);
    }
    return value;
}

uint64_t LocalFileSize(const std::string& path) {
    struct stat st;
    if (path.empty() || ::stat(path.c_str(), &st) != 0) {
        return 0;
    }
    return static_cast<uint64_t>(st.st_size);
}

std::string CaptureEnhanceStepDir(const std::string& task_id, std::size_t index) {
    std::ostringstream name;
    name << "enhance-step-" << std::setw(3) << std::setfill('0') << (index + 1);
    return JoinPath(GetSdkOpenTaskAssetDir("capture", task_id, "assets"), name.str());
}

} // namespace

CaptureTaskService::CaptureTaskService(const ProviderBundle& providers, const std::string& asset_base_url)
    : pipeline_service_(providers),
      providers_(providers),
      asset_base_url_(TrimTrailingSlash(asset_base_url)),
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
    task.session_token = request.session_token;
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

CaptureAssetResult CaptureTaskService::GetAsset(const std::string& connection_id,
                                                const std::string& task_id,
                                                const std::string& asset_id) const {
    std::lock_guard<std::mutex> lock(mu_);
    CaptureAssetResult result;
    const CaptureTaskSnapshot task = GetTaskUnlocked(task_id);
    if (!IsOkStatusCode(task.code)) {
        result.code = task.code;
        result.message = task.message;
        return result;
    }
    if (task.connection_id != connection_id) {
        result.code = ToCode(SdkStatusCode::CapabilityNotAllowed);
        result.message = "task belongs to another connection";
        return result;
    }
    for (std::vector<SdkCaptureAsset>::const_iterator it = task.assets.begin(); it != task.assets.end(); ++it) {
        if (it->asset_id == asset_id) {
            result.asset = *it;
            return result;
        }
    }
    result.code = ToCode(SdkStatusCode::InvalidParams);
    result.message = "asset not found";
    return result;
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

    CapturePipelineResult final_pipeline_result = pipeline_result;
    if (IsOkStatusCode(final_pipeline_result.code) &&
        !request.pipeline.steps.empty() &&
        providers_.image_enhance_provider) {
        std::vector<SdkImageEnhancePage> pages;
        for (std::vector<SdkCaptureAsset>::const_iterator it = final_pipeline_result.assets.begin();
             it != final_pipeline_result.assets.end();
             ++it) {
            if (it->path.empty()) {
                continue;
            }
            if (it->kind == "final" || it->kind.find("final_") == 0) {
                SdkImageEnhancePage page;
                page.source_index = static_cast<int>(pages.size() + 1);
                page.output_index = page.source_index;
                page.path = it->path;
                pages.push_back(page);
            }
        }
        if (!pages.empty()) {
            SdkCaptureStageResult enhance_stage;
            enhance_stage.name = "image_enhance";
            enhance_stage.status = "running";
            enhance_stage.provider = providers_.image_enhance_provider->ProviderName();
            enhance_stage.message = "running";
            PublishEvent(request.connection_id, "capture.stage.updated", running, &enhance_stage);

            bool failed = false;
            std::string error;
            for (std::size_t step_index = 0; !failed && step_index < request.pipeline.steps.size(); ++step_index) {
                const SdkImageEnhanceStep& step = request.pipeline.steps[step_index];
                if (!step.enabled) {
                    continue;
                }
                SdkImageEnhanceStepRequest step_request;
                step_request.task_id = task_id;
                step_request.step = step;
                step_request.pages = pages;
                step_request.output_dir = CaptureEnhanceStepDir(task_id, step_index);
                step_request.online_api_key = request.online_api_key;
                step_request.online_base_url = request.online_base_url;
                EnsureDirectoryRecursive(step_request.output_dir);
                const SdkImageEnhanceStepResult step_result = providers_.image_enhance_provider->RunStep(step_request);
                if (!IsOkStatusCode(step_result.code)) {
                    if (step.on_error == "skip") {
                        final_pipeline_result.warnings.push_back(step.type + " skipped: " + step_result.message);
                        continue;
                    }
                    failed = true;
                    error = step_result.message;
                    break;
                }
                pages = step_result.pages;
            }
            enhance_stage.status = failed ? "failed" : "succeeded";
            enhance_stage.message = failed ? error : "ok";
            final_pipeline_result.stages.push_back(enhance_stage);
            if (failed || pages.empty()) {
                final_pipeline_result.code = ToCode(SdkStatusCode::ProviderCallFailed);
                final_pipeline_result.message = failed ? error : "image enhance produced no output pages";
                final_pipeline_result.status = "failed";
            } else {
                std::vector<SdkCaptureAsset> enhanced_assets;
                for (std::vector<SdkImageEnhancePage>::const_iterator it = pages.begin(); it != pages.end(); ++it) {
                    SdkCaptureAsset asset;
                    asset.asset_id = "asset-enhanced-final-" + std::to_string(static_cast<long long>(enhanced_assets.size() + 1));
                    asset.kind = "final";
                    asset.path = it->path;
                    asset.content_type = "image/jpeg";
                    asset.size = LocalFileSize(asset.path);
                    enhanced_assets.push_back(asset);
                }
                final_pipeline_result.assets = enhanced_assets;
            }
        }
    }

    CaptureTaskSnapshot final_task;
    {
        std::lock_guard<std::mutex> lock(mu_);
        CaptureTaskSnapshot& task = tasks_[task_id];
        task.status = final_pipeline_result.status;
        task.stages = final_pipeline_result.stages;
        task.assets = final_pipeline_result.assets;
        AttachAssetUrls(task_id, &task.assets);
        task.warnings = final_pipeline_result.warnings;
        task.code = final_pipeline_result.code;
        task.message = final_pipeline_result.message;
        if (!IsOkStatusCode(final_pipeline_result.code)) {
            task.error = final_pipeline_result.message;
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

void CaptureTaskService::AttachAssetUrls(const std::string& task_id, std::vector<SdkCaptureAsset>* assets) const {
    if (assets == NULL) {
        return;
    }
    for (std::vector<SdkCaptureAsset>::iterator it = assets->begin(); it != assets->end(); ++it) {
        it->url = asset_base_url_ + "/api/assets/" + task_id + "/" + it->asset_id;
        it->download_url = it->url + "/download";
    }
}

std::string CaptureTaskService::NextTaskId() {
    const uint64_t seq = next_task_seq_.fetch_add(1);
    return "cap-" + std::to_string(static_cast<long long>(std::time(nullptr))) + "-" + std::to_string(seq);
}

} // namespace sdk
} // namespace editor
