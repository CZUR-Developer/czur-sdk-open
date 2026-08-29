// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "capture_task_service.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <exception>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <sys/stat.h>

#include "sdk_logger.h"
#include "sdk_runtime_paths.h"

namespace editor {
namespace sdk {

namespace {

const int kCaptureCooldownMs = 1500;

struct CaptureCallbackState {
    std::mutex mu;
    std::condition_variable cv;
    bool completed = false;
    SdkCaptureResult result;
};

Json BuildAssetJson(const SdkCaptureAsset& asset) {
    return Json{{"asset_id", asset.asset_id}, {"kind", asset.kind}, {"path", asset.path},
                {"url", asset.url}, {"download_url", asset.download_url},
                {"content_type", asset.content_type}, {"width", asset.width},
                {"height", asset.height}, {"size", asset.size}};
}

Json BuildStageJson(const SdkCaptureStageResult& stage) {
    return Json{{"name", stage.name}, {"status", stage.status}, {"input", stage.input_assets},
                {"output", stage.output_assets}, {"provider", stage.provider}, {"message", stage.message}};
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
    return Json{{"task_id", task.task_id}, {"status", task.status}, {"code", task.code},
                {"message", task.message}, {"device_id", task.device_id},
                {"capture_source", task.capture_source}, {"acquisition_status", task.acquisition_status},
                {"processing_status", task.processing_status}, {"profile_revision", task.profile_revision},
                {"stages", stages}, {"assets", assets}, {"warnings", task.warnings}, {"error", task.error}};
}

std::string TrimTrailingSlash(const std::string& value) {
    return !value.empty() && value[value.size() - 1] == '/' ? value.substr(0, value.size() - 1) : value;
}

uint64_t LocalFileSize(const std::string& path) {
    struct stat st;
    return path.empty() || ::stat(path.c_str(), &st) != 0 ? 0 : static_cast<uint64_t>(st.st_size);
}

std::string CaptureEnhanceStepDir(const std::string& task_id, std::size_t index) {
    std::ostringstream name;
    name << "enhance-step-" << std::setw(3) << std::setfill('0') << (index + 1);
    return JoinPath(GetSdkOpenTaskAssetDir("capture", task_id, "assets"), name.str());
}

bool CopyFileBinary(const std::string& input_path, const std::string& output_path) {
    std::ifstream input(input_path.c_str(), std::ios::binary);
    if (!input.is_open()) {
        return false;
    }
    std::ofstream output(output_path.c_str(), std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }
    output << input.rdbuf();
    return output.good();
}

bool WriteBytes(const std::string& output_path, const std::vector<uint8_t>& bytes) {
    if (output_path.empty() || bytes.empty()) {
        return false;
    }
    std::ofstream output(output_path.c_str(), std::ios::binary | std::ios::trunc);
    if (!output.is_open()) {
        return false;
    }
    output.write(reinterpret_cast<const char*>(&bytes[0]), static_cast<std::streamsize>(bytes.size()));
    return output.good();
}

bool FileExists(const std::string& path) {
    struct stat st;
    return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

} // namespace

Json BuildCaptureSessionSummaryJson(const CaptureSessionSummary& summary) {
    Json devices = Json::array();
    for (std::vector<CaptureSessionDeviceSummary>::const_iterator it = summary.devices.begin(); it != summary.devices.end(); ++it) {
        devices.push_back(Json{{"device_id", it->device_id}, {"captured_count", it->captured_count},
                               {"processed_count", it->processed_count}, {"failed_count", it->failed_count},
                               {"pending_count", it->pending_count}});
    }
    return Json{{"captured_count", summary.captured_count}, {"processed_count", summary.processed_count},
                {"failed_count", summary.failed_count}, {"pending_count", summary.pending_count},
                {"devices", devices}};
}

CaptureTaskService::CaptureTaskService(const ProviderBundle& providers, const std::string& asset_base_url)
    : pipeline_service_(providers), device_facade_(providers), providers_(providers),
      asset_base_url_(TrimTrailingSlash(asset_base_url)), next_task_seq_(1) {}

CaptureTaskService::~CaptureTaskService() {
    // 采集 worker 在运行时可能再创建按设备处理的 worker。逐个在锁内取走
    // thread，再在锁外 join，能同时覆盖初始采集 worker 和它们追加的处理 worker。
    std::size_t worker_index = 0;
    for (;;) {
        std::thread worker;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (worker_index >= workers_.size()) {
                break;
            }
            worker.swap(workers_[worker_index]);
            ++worker_index;
        }
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void CaptureTaskService::SetEventSink(EventSink sink) {
    std::lock_guard<std::mutex> lock(mu_);
    event_sink_ = sink;
}

CaptureTaskStartResult CaptureTaskService::ReserveTask(const CaptureTaskStartRequest& request) {
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

    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    const std::string task_id = NextTaskId();
    CaptureTaskSnapshot task;
    task.task_id = task_id;
    task.connection_id = request.connection_id;
    task.session_token = request.session_token;
    task.device_id = request.device_id;
    task.capture_source = request.raw_capture.captured ? "hardgrab" : "manual";
    task.profile_revision = request.profile.revision;

    {
        std::lock_guard<std::mutex> lock(mu_);
        if (active_capture_devices_.find(request.device_id) != active_capture_devices_.end()) {
            result.code = ToCode(SdkStatusCode::DeviceBusy);
            result.message = "device has an active capture action";
            return result;
        }
        const std::map<std::string, std::chrono::steady_clock::time_point>::const_iterator cooldown_it =
            capture_cooldown_until_.find(request.device_id);
        if (cooldown_it != capture_cooldown_until_.end() && now < cooldown_it->second) {
            result.code = ToCode(SdkStatusCode::RateLimited);
            result.message = "capture is rate limited";
            result.retry_after_ms = static_cast<int>(std::max<long long>(
                1, std::chrono::duration_cast<std::chrono::milliseconds>(cooldown_it->second - now).count()));
            return result;
        }
        // 硬拍回调已携带原图，当前线程只做原图落盘而不再发起物理拍照；
        // 它参与同一 1500ms 限频，但不应把短暂的落盘当成设备物理采集占用。
        if (!request.raw_capture.captured) {
            active_capture_devices_.insert(request.device_id);
        }
        capture_cooldown_until_[request.device_id] = now + std::chrono::milliseconds(kCaptureCooldownMs);
        tasks_[task_id] = task;
        requests_[task_id] = request;
    }
    result.accepted = true;
    result.task = task;
    return result;
}

CaptureTaskStartResult CaptureTaskService::StartReservedTask(const std::string& task_id) {
    CaptureTaskStartResult result;
    std::lock_guard<std::mutex> lock(mu_);
    std::map<std::string, CaptureTaskSnapshot>::iterator task_it = tasks_.find(task_id);
    if (task_it == tasks_.end() || requests_.find(task_id) == requests_.end()) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "capture task not found";
        return result;
    }
    if (task_it->second.acquisition_status != "queued") {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "capture task is not pending";
        return result;
    }
    active_worker_task_ids_.insert(task_id);
    try {
        // reserve 先完成可能抛异常的内存分配；随后移动 std::thread 不会再因
        // vector 扩容丢失一个 joinable worker。
        workers_.reserve(workers_.size() + 1);
        workers_.push_back(std::thread(&CaptureTaskService::RunCaptureTask, this, task_id));
    } catch (const std::exception& e) {
        active_worker_task_ids_.erase(task_id);
        active_capture_devices_.erase(task_it->second.device_id);
        capture_cooldown_until_.erase(task_it->second.device_id);
        requests_.erase(task_id);
        tasks_.erase(task_it);
        result.code = ToCode(SdkStatusCode::InternalError);
        result.message = e.what();
        return result;
    } catch (...) {
        active_worker_task_ids_.erase(task_id);
        active_capture_devices_.erase(task_it->second.device_id);
        capture_cooldown_until_.erase(task_it->second.device_id);
        requests_.erase(task_id);
        tasks_.erase(task_it);
        result.code = ToCode(SdkStatusCode::InternalError);
        result.message = "failed to start capture task";
        return result;
    }
    result.accepted = true;
    result.task = task_it->second;
    return result;
}

void CaptureTaskService::AbortReservedTask(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(mu_);
    std::map<std::string, CaptureTaskSnapshot>::iterator task_it = tasks_.find(task_id);
    if (task_it == tasks_.end() || task_it->second.acquisition_status != "queued") {
        return;
    }
    active_capture_devices_.erase(task_it->second.device_id);
    capture_cooldown_until_.erase(task_it->second.device_id);
    requests_.erase(task_id);
    tasks_.erase(task_it);
}

CaptureTaskSnapshot CaptureTaskService::GetTask(const std::string& connection_id, const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(mu_);
    CaptureTaskSnapshot task = GetTaskUnlocked(task_id);
    if (IsOkStatusCode(task.code) && task.connection_id != connection_id) {
        task.code = ToCode(SdkStatusCode::CapabilityNotAllowed);
        task.message = "task belongs to another connection";
    }
    return task;
}

CaptureSessionSummary CaptureTaskService::GetSessionSummary(const std::string& connection_id) const {
    std::lock_guard<std::mutex> lock(mu_);
    return GetSessionSummaryUnlocked(connection_id);
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

std::size_t CaptureTaskService::ActiveTaskCount() const {
    std::lock_guard<std::mutex> lock(mu_);
    return active_worker_task_ids_.size();
}

std::size_t CaptureTaskService::ClearFinishedTasks() {
    std::lock_guard<std::mutex> lock(mu_);
    std::size_t count = 0;
    for (std::map<std::string, CaptureTaskSnapshot>::iterator it = tasks_.begin(); it != tasks_.end();) {
        if (active_worker_task_ids_.find(it->first) != active_worker_task_ids_.end()) {
            ++it;
            continue;
        }
        requests_.erase(it->first);
        it = tasks_.erase(it);
        ++count;
    }
    return count;
}

void CaptureTaskService::RunCaptureTask(const std::string& task_id) {
    CaptureTaskStartRequest request;
    CaptureTaskSnapshot running;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const std::map<std::string, CaptureTaskStartRequest>::const_iterator request_it = requests_.find(task_id);
        std::map<std::string, CaptureTaskSnapshot>::iterator task_it = tasks_.find(task_id);
        if (request_it == requests_.end() || task_it == tasks_.end()) {
            return;
        }
        request = request_it->second;
        task_it->second.status = "running";
        task_it->second.acquisition_status = "capturing";
        running = task_it->second;
    }
    PublishEvent(request.connection_id, "capture.started", running);

    SdkCaptureResult raw_capture;
    try {
        raw_capture = CaptureRaw(task_id, request);
    } catch (const std::exception& e) {
        SDK_OPEN_LOG_ERROR("[capture_task] capture action exception task_id={} err={}", task_id, e.what());
        CompleteCaptureFailure(task_id, request, e.what());
        return;
    } catch (...) {
        SDK_OPEN_LOG_ERROR("[capture_task] capture action unknown exception task_id={}", task_id);
        CompleteCaptureFailure(task_id, request, "capture action failed");
        return;
    }
    if (!IsOkStatusCode(raw_capture.code) || !raw_capture.captured) {
        CompleteCaptureFailure(task_id, request, raw_capture.message.empty() ? "capture failed" : raw_capture.message);
        return;
    }
    std::string stage_error;
    if (!StageCapturedRaw(task_id, &raw_capture, &stage_error)) {
        CompleteCaptureFailure(task_id, request, stage_error);
        return;
    }

    bool start_processing_failed = false;
    CaptureTaskSnapshot captured_task;
    {
        std::lock_guard<std::mutex> lock(mu_);
        std::map<std::string, CaptureTaskSnapshot>::iterator task_it = tasks_.find(task_id);
        std::map<std::string, CaptureTaskStartRequest>::iterator request_it = requests_.find(task_id);
        if (task_it == tasks_.end() || request_it == requests_.end()) {
            return;
        }
        request_it->second.raw_capture = raw_capture;
        task_it->second.acquisition_status = "captured";
        task_it->second.processing_status = "queued";
        active_capture_devices_.erase(request.device_id);
        CaptureSessionDeviceSummary& summary = session_summaries_[request.connection_id][request.device_id];
        summary.device_id = request.device_id;
        ++summary.captured_count;
        ++summary.pending_count;
        processing_queues_[request.device_id].push_back(task_id);
        if (active_processing_devices_.insert(request.device_id).second) {
            try {
                // 同 StartReservedTask，先完成可能抛异常的 vector 扩容。
                workers_.reserve(workers_.size() + 1);
                workers_.push_back(std::thread(&CaptureTaskService::RunProcessingQueue, this, request.device_id));
            } catch (...) {
                active_processing_devices_.erase(request.device_id);
                processing_queues_[request.device_id].pop_back();
                start_processing_failed = true;
            }
        }
        captured_task = task_it->second;
    }
    if (start_processing_failed) {
        CapturePipelineResult failure;
        failure.code = ToCode(SdkStatusCode::InternalError);
        failure.message = "failed to start capture processing worker";
        failure.status = "failed";
        CompleteProcessingTask(task_id, request, failure);
        return;
    }
    SDK_OPEN_LOG_INFO("[capture_task] raw captured task_id={} source={} device={} queued_for_processing=true",
                      task_id, captured_task.capture_source, request.device_id);
    PublishSessionUpdate(request.connection_id, request.device_id, task_id, "raw_captured");
}

void CaptureTaskService::RunProcessingQueue(const std::string& device_id) {
    for (;;) {
        std::string task_id;
        CaptureTaskStartRequest request;
        {
            std::lock_guard<std::mutex> lock(mu_);
            std::map<std::string, std::deque<std::string> >::iterator queue_it = processing_queues_.find(device_id);
            if (queue_it == processing_queues_.end() || queue_it->second.empty()) {
                active_processing_devices_.erase(device_id);
                if (queue_it != processing_queues_.end()) {
                    processing_queues_.erase(queue_it);
                }
                return;
            }
            task_id = queue_it->second.front();
            queue_it->second.pop_front();
            const std::map<std::string, CaptureTaskStartRequest>::const_iterator request_it = requests_.find(task_id);
            std::map<std::string, CaptureTaskSnapshot>::iterator task_it = tasks_.find(task_id);
            if (request_it == requests_.end() || task_it == tasks_.end()) {
                continue;
            }
            request = request_it->second;
            task_it->second.processing_status = "running";
        }
        CapturePipelineResult pipeline_result;
        try {
            pipeline_result = RunProcessingPipeline(task_id, request);
        } catch (const std::exception& e) {
            SDK_OPEN_LOG_ERROR("[capture_task] processing exception task_id={} err={}", task_id, e.what());
            pipeline_result.code = ToCode(SdkStatusCode::InternalError);
            pipeline_result.message = e.what();
            pipeline_result.status = "failed";
        } catch (...) {
            SDK_OPEN_LOG_ERROR("[capture_task] processing unknown exception task_id={}", task_id);
            pipeline_result.code = ToCode(SdkStatusCode::InternalError);
            pipeline_result.message = "capture processing failed";
            pipeline_result.status = "failed";
        }
        CompleteProcessingTask(task_id, request, pipeline_result);
    }
}

SdkCaptureResult CaptureTaskService::CaptureRaw(const std::string& task_id,
                                                const CaptureTaskStartRequest& request) const {
    if (request.raw_capture.captured) {
        return request.raw_capture;
    }
    SdkCaptureRequest capture_request;
    capture_request.device_id = request.device_id;
    // 使用任务专属 raw 目录，保证后续拍照不会覆盖尚在处理队列里的原图。
    // 最终输出仍由处理阶段写入调用方指定的 output_dir。
    capture_request.output_dir = GetSdkOpenTaskAssetDir("capture", task_id, "raw");
    if (!EnsureDirectoryRecursive(capture_request.output_dir)) {
        SdkCaptureResult result;
        result.code = ToCode(SdkStatusCode::InternalError);
        result.message = "failed to create capture raw directory";
        return result;
    }
    capture_request.include_base64 = request.include_base64;
    capture_request.timeout_ms = request.timeout_ms;
    const std::shared_ptr<CaptureCallbackState> state(new CaptureCallbackState());
    device_facade_.CaptureStill(request.auth_context, capture_request, [state](const SdkCaptureResult& result) {
        bool notify = false;
        {
            std::lock_guard<std::mutex> lock(state->mu);
            if (!state->completed) {
                state->result = result;
                state->completed = true;
                notify = true;
            }
        }
        if (notify) {
            state->cv.notify_one();
        }
    });
    std::unique_lock<std::mutex> lock(state->mu);
    if (!state->completed) {
        const int timeout_ms = request.timeout_ms > 0 ? request.timeout_ms : kDefaultCaptureTimeoutMs;
        if (!state->cv.wait_for(lock, std::chrono::milliseconds(timeout_ms), [state]() { return state->completed; })) {
            state->completed = true;
            state->result.code = ToCode(SdkStatusCode::CaptureTimeout);
            state->result.message = "capture timeout";
        }
    }
    return state->result;
}

bool CaptureTaskService::StageCapturedRaw(const std::string& task_id,
                                          SdkCaptureResult* raw_capture,
                                          std::string* error) const {
    if (raw_capture == NULL) {
        if (error != NULL) {
            *error = "captured original missing";
        }
        return false;
    }
    const std::string raw_dir = GetSdkOpenTaskAssetDir("capture", task_id, "raw");
    if (!EnsureDirectoryRecursive(raw_dir)) {
        if (error != NULL) {
            *error = "failed to create capture raw directory";
        }
        return false;
    }

    const std::string original_path = JoinPath(raw_dir, "original.jpg");
    if (!raw_capture->raw_payload.empty()) {
        if (!WriteBytes(original_path, raw_capture->raw_payload)) {
            if (error != NULL) {
                *error = "failed to persist captured original";
            }
            return false;
        }
        raw_capture->raw_payload.clear();
    } else {
        const std::string source_path = !raw_capture->original_path.empty()
                                            ? raw_capture->original_path
                                            : raw_capture->output_path;
        if (source_path.empty() || (source_path != original_path && !CopyFileBinary(source_path, original_path)) ||
            (source_path == original_path && !FileExists(original_path))) {
            if (error != NULL) {
                *error = "failed to persist captured original";
            }
            return false;
        }
    }
    raw_capture->original_path = original_path;
    raw_capture->output_path = original_path;

    if (raw_capture->raw_laser_payload.empty() && raw_capture->laser_path.empty()) {
        return true;
    }
    const std::string laser_path = JoinPath(raw_dir, "laser.jpg");
    if (!raw_capture->raw_laser_payload.empty()) {
        if (!WriteBytes(laser_path, raw_capture->raw_laser_payload)) {
            if (error != NULL) {
                *error = "failed to persist captured laser image";
            }
            return false;
        }
        raw_capture->raw_laser_payload.clear();
    } else if ((raw_capture->laser_path != laser_path && !CopyFileBinary(raw_capture->laser_path, laser_path)) ||
               (raw_capture->laser_path == laser_path && !FileExists(laser_path))) {
        if (error != NULL) {
            *error = "failed to persist captured laser image";
        }
        return false;
    }
    raw_capture->laser_path = laser_path;
    return true;
}

CapturePipelineResult CaptureTaskService::RunProcessingPipeline(const std::string& task_id,
                                                                 const CaptureTaskStartRequest& request) {
    CapturePipelineRequest pipeline_request;
    pipeline_request.task_id = task_id;
    pipeline_request.device_id = request.device_id;
    pipeline_request.output_dir = request.output_dir;
    pipeline_request.include_base64 = request.include_base64;
    pipeline_request.timeout_ms = request.timeout_ms;
    pipeline_request.auth_context = request.auth_context;
    pipeline_request.profile = request.profile;
    pipeline_request.raw_capture = request.raw_capture;
    CapturePipelineResult final_result = pipeline_service_.Run(
        pipeline_request, [this, task_id, request](const SdkCaptureStageResult& stage) {
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

    if (!IsOkStatusCode(final_result.code) || request.pipeline.steps.empty() || !providers_.image_enhance_provider) {
        return final_result;
    }
    std::vector<SdkImageEnhancePage> pages;
    for (std::vector<SdkCaptureAsset>::const_iterator it = final_result.assets.begin(); it != final_result.assets.end(); ++it) {
        if (it->path.empty() || (it->kind != "final" && it->kind.find("final_") != 0)) {
            continue;
        }
        SdkImageEnhancePage page;
        page.source_index = static_cast<int>(pages.size() + 1);
        page.output_index = page.source_index;
        page.path = it->path;
        pages.push_back(page);
    }
    if (pages.empty()) {
        return final_result;
    }
    SdkCaptureStageResult enhance_stage;
    enhance_stage.name = "image_enhance";
    enhance_stage.status = "running";
    enhance_stage.provider = providers_.image_enhance_provider->ProviderName();
    enhance_stage.message = "running";
    PublishEvent(request.connection_id, "capture.stage.updated", GetTask(request.connection_id, task_id), &enhance_stage);
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
                final_result.warnings.push_back(step.type + " skipped: " + step_result.message);
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
    final_result.stages.push_back(enhance_stage);
    if (failed || pages.empty()) {
        final_result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        final_result.message = failed ? error : "image enhance produced no output pages";
        final_result.status = "failed";
        return final_result;
    }
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
    final_result.assets = enhanced_assets;
    return final_result;
}

void CaptureTaskService::CompleteCaptureFailure(const std::string& task_id,
                                                const CaptureTaskStartRequest& request,
                                                const std::string& message) {
    CaptureTaskSnapshot task;
    {
        std::lock_guard<std::mutex> lock(mu_);
        std::map<std::string, CaptureTaskSnapshot>::iterator it = tasks_.find(task_id);
        if (it == tasks_.end()) {
            return;
        }
        it->second.status = "failed";
        it->second.acquisition_status = "failed";
        it->second.processing_status = "skipped";
        it->second.code = ToCode(SdkStatusCode::CaptureFailed);
        it->second.message = message.empty() ? "capture failed" : message;
        it->second.error = it->second.message;
        active_capture_devices_.erase(request.device_id);
        active_worker_task_ids_.erase(task_id);
        CaptureSessionDeviceSummary& summary = session_summaries_[request.connection_id][request.device_id];
        summary.device_id = request.device_id;
        ++summary.failed_count;
        task = it->second;
    }
    PublishEvent(request.connection_id, "capture.failed", task);
    PublishSessionUpdate(request.connection_id, request.device_id, task_id, "capture_failed");
}

void CaptureTaskService::CompleteProcessingTask(const std::string& task_id,
                                                const CaptureTaskStartRequest& request,
                                                const CapturePipelineResult& result) {
    CaptureTaskSnapshot task;
    bool succeeded = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        std::map<std::string, CaptureTaskSnapshot>::iterator it = tasks_.find(task_id);
        if (it == tasks_.end()) {
            return;
        }
        it->second.status = result.status.empty() ? (IsOkStatusCode(result.code) ? "succeeded" : "failed") : result.status;
        succeeded = IsOkStatusCode(result.code) && it->second.status == "succeeded";
        it->second.processing_status = succeeded ? "succeeded" : "failed";
        it->second.stages = result.stages;
        it->second.assets = result.assets;
        AttachAssetUrls(task_id, &it->second.assets);
        it->second.warnings = result.warnings;
        it->second.code = result.code;
        it->second.message = result.message;
        if (!succeeded) {
            it->second.error = result.message;
        }
        active_worker_task_ids_.erase(task_id);
        CaptureSessionDeviceSummary& summary = session_summaries_[request.connection_id][request.device_id];
        summary.device_id = request.device_id;
        if (summary.pending_count > 0) {
            --summary.pending_count;
        }
        if (succeeded) {
            ++summary.processed_count;
        } else {
            ++summary.failed_count;
        }
        task = it->second;
    }
    PublishEvent(request.connection_id, succeeded ? "capture.completed" : "capture.failed", task);
    PublishSessionUpdate(request.connection_id, request.device_id, task_id,
                         succeeded ? "processing_completed" : "processing_failed");
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

void CaptureTaskService::PublishSessionUpdate(const std::string& connection_id,
                                              const std::string& device_id,
                                              const std::string& task_id,
                                              const std::string& reason) const {
    EventSink sink;
    CaptureSessionSummary summary;
    {
        std::lock_guard<std::mutex> lock(mu_);
        sink = event_sink_;
        summary = GetSessionSummaryUnlocked(connection_id);
    }
    if (!sink) {
        return;
    }
    sink(connection_id, BuildWsEvent("capture.session.updated",
                                     Json{{"device_id", device_id}, {"task_id", task_id}, {"reason", reason},
                                          {"summary", BuildCaptureSessionSummaryJson(summary)}}));
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

CaptureSessionSummary CaptureTaskService::GetSessionSummaryUnlocked(const std::string& connection_id) const {
    CaptureSessionSummary summary;
    const std::map<std::string, std::map<std::string, CaptureSessionDeviceSummary> >::const_iterator session_it =
        session_summaries_.find(connection_id);
    if (session_it == session_summaries_.end()) {
        return summary;
    }
    for (std::map<std::string, CaptureSessionDeviceSummary>::const_iterator it = session_it->second.begin();
         it != session_it->second.end(); ++it) {
        summary.devices.push_back(it->second);
        summary.captured_count += it->second.captured_count;
        summary.processed_count += it->second.processed_count;
        summary.failed_count += it->second.failed_count;
        summary.pending_count += it->second.pending_count;
    }
    return summary;
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
