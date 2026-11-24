// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "capture_task_service.h"

#include "image_enhance_quota.h"

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
    EventSink previous;
    {
        std::lock_guard<std::mutex> lock(mu_);
        previous.swap(event_sink_);
        event_sink_.swap(sink);
    }
}

void CaptureTaskService::SetSnapshotEventSink(SnapshotEventSink sink) {
    SnapshotEventSink previous;
    {
        std::lock_guard<std::mutex> lock(mu_);
        previous.swap(snapshot_event_sink_);
        snapshot_event_sink_.swap(sink);
    }
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
            active_capture_devices_[request.device_id] = task_id;
        }
        capture_cooldown_until_[request.device_id] = now + std::chrono::milliseconds(kCaptureCooldownMs);
        capture_cooldown_owners_[request.device_id] = task_id;
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
    if (task_it->second.acquisition_status != "queued" ||
        cancel_requested_task_ids_.find(task_id) != cancel_requested_task_ids_.end() ||
        task_activity_counts_.find(task_id) != task_activity_counts_.end()) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "capture task is not pending";
        return result;
    }
    try {
        // Prepare the complete return value before admitting a thread.
        result.task = task_it->second;
        BeginTaskActivityUnlocked(task_id);
        // reserve 先完成可能抛异常的内存分配；随后移动 std::thread 不会再因
        // vector 扩容丢失一个 joinable worker。
        workers_.reserve(workers_.size() + 1);
        std::thread worker(&CaptureTaskService::RunCaptureTask, this, task_id);
        workers_.push_back(std::move(worker));
    } catch (const std::exception& e) {
        task_activity_counts_.erase(task_id);
        ReleaseCaptureOwnershipUnlocked(task_id, true);
        requests_.erase(task_id);
        tasks_.erase(task_it);
        result.task = CaptureTaskSnapshot();
        result.code = ToCode(SdkStatusCode::InternalError);
        result.message = e.what();
        return result;
    } catch (...) {
        task_activity_counts_.erase(task_id);
        ReleaseCaptureOwnershipUnlocked(task_id, true);
        requests_.erase(task_id);
        tasks_.erase(task_it);
        result.task = CaptureTaskSnapshot();
        result.code = ToCode(SdkStatusCode::InternalError);
        result.message = "failed to start capture task";
        return result;
    }
    result.accepted = true;
    return result;
}

void CaptureTaskService::ReleaseCaptureOwnershipUnlocked(const std::string& task_id,
                                                          bool reset_cooldown) {
    const auto task = tasks_.find(task_id);
    if (task == tasks_.end()) return;
    const std::string& device_id = task->second.device_id;
    const auto owner = active_capture_devices_.find(device_id);
    if (owner != active_capture_devices_.end() && owner->second == task_id) {
        active_capture_devices_.erase(owner);
    }
    const auto cooldown = capture_cooldown_owners_.find(device_id);
    if (reset_cooldown && cooldown != capture_cooldown_owners_.end() && cooldown->second == task_id) {
        capture_cooldown_until_.erase(device_id);
        capture_cooldown_owners_.erase(cooldown);
    }
}

void CaptureTaskService::RequestCancellationUnlocked(const std::string& task_id) {
    auto task_it = tasks_.find(task_id);
    if (task_it == tasks_.end() || IsTerminalStatus(task_it->second.status)) return;
    CaptureTaskSnapshot& task = task_it->second;
    cancel_requested_task_ids_.insert(task_id);
    MarkTaskCancellingUnlocked(&task);

    // Queued processing has a reference, but no executing provider. Remove only
    // this task; another Context may own the device's currently executing job.
    auto queue = processing_queues_.find(task.device_id);
    if (queue != processing_queues_.end()) {
        auto position = std::find(queue->second.begin(), queue->second.end(), task_id);
        if (position != queue->second.end()) {
            task.status = "cancelled";
            task.message = "cancelled";
            task.processing_status = "cancelled";
            queue->second.erase(position);
            auto session = session_summaries_.find(task.connection_id);
            if (session != session_summaries_.end()) {
                auto summary = session->second.find(task.device_id);
                if (summary != session->second.end() && summary->second.pending_count > 0) {
                    --summary->second.pending_count;
                }
            }
            EndTaskActivityUnlocked(task_id); // release the queue's reference only
            return;
        }
    }
    if (task_activity_counts_.find(task_id) == task_activity_counts_.end()) {
        task.status = "cancelled";
        task.message = "cancelled";
        task.acquisition_status = "cancelled";
        task.processing_status = "cancelled";
        ReleaseCaptureOwnershipUnlocked(task_id, true);
        requests_.erase(task_id);
    }
}

CaptureTaskSnapshot CaptureTaskService::CancelTask(const std::string& connection_id,
                                                  const std::string& task_id) {
    CaptureTaskSnapshot result;
    TaskActivityGuard event_activity = {this, &task_id, false};
    {
        std::lock_guard<std::mutex> lock(mu_);
        const auto task = tasks_.find(task_id);
        if (task == tasks_.end()) {
            result.code = ToCode(SdkStatusCode::InvalidParams);
            result.message = "capture task not found";
            return result;
        }
        if (task->second.connection_id != connection_id) {
            result.code = ToCode(SdkStatusCode::CapabilityNotAllowed);
            result.message = "task belongs to another connection";
            return result;
        }
        if (IsTerminalStatus(task->second.status)) return task->second;
        RequestCancellationUnlocked(task_id);
        BeginTaskActivityUnlocked(task_id);
        event_activity.active = true;
        result = task->second;
    }
    PublishEvent(connection_id, result.status == "cancelled" ? "capture.cancelled" : "capture.cancelling", result);
    return result;
}

bool CaptureTaskService::CancelAndWait(const std::string& connection_id, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeout_ms > 0 ? timeout_ms : 0);
    std::unique_lock<std::mutex> lock(mu_);
    for (auto it = tasks_.begin(); it != tasks_.end(); ++it) {
        if (connection_id.empty() || it->second.connection_id == connection_id) {
            RequestCancellationUnlocked(it->first);
        }
    }
    // Bulk teardown does not synchronously invoke a host sink: a blocked callback
    // must not consume an unbounded amount of this caller's timeout budget.
    const auto drained = [this, &connection_id]() {
        for (auto it = task_activity_counts_.begin(); it != task_activity_counts_.end(); ++it) {
            const auto task = tasks_.find(it->first);
            if (task != tasks_.end() &&
                (connection_id.empty() || task->second.connection_id == connection_id)) return false;
        }
        return true;
    };
    if (timeout_ms < 0) {
        task_activity_cv_.wait(lock, drained);
        return true;
    }
    return task_activity_cv_.wait_until(lock, deadline, drained);
}

void CaptureTaskService::AbortReservedTask(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(mu_);
    std::map<std::string, CaptureTaskSnapshot>::iterator task_it = tasks_.find(task_id);
    if (task_it == tasks_.end() || task_it->second.acquisition_status != "queued" ||
        task_activity_counts_.find(task_id) != task_activity_counts_.end()) {
        return;
    }
    ReleaseCaptureOwnershipUnlocked(task_id, true);
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
    // A reserved task is already a storage owner even before its worker thread
    // starts. Count queued and running tasks as active so cleanup cannot remove
    // a hard-grab provider temporary file in the ReserveTask/StartReservedTask
    // hand-off window.
    std::size_t active_count = 0;
    for (std::map<std::string, CaptureTaskSnapshot>::const_iterator it = tasks_.begin(); it != tasks_.end(); ++it) {
        if (task_activity_counts_.find(it->first) != task_activity_counts_.end() ||
            !IsTerminalStatus(it->second.status)) {
            ++active_count;
        }
    }
    return active_count;
}

std::size_t CaptureTaskService::ClearFinishedTasks() {
    std::lock_guard<std::mutex> lock(mu_);
    std::size_t count = 0;
    for (std::map<std::string, CaptureTaskSnapshot>::iterator it = tasks_.begin(); it != tasks_.end();) {
        if (!IsTerminalStatus(it->second.status) ||
            task_activity_counts_.find(it->first) != task_activity_counts_.end()) {
            ++it;
            continue;
        }
        requests_.erase(it->first);
        cancel_requested_task_ids_.erase(it->first);
        it = tasks_.erase(it);
        ++count;
    }
    return count;
}

bool CaptureTaskService::IsCancelRequested(const std::string& task_id) const {
    std::lock_guard<std::mutex> lock(mu_);
    return cancel_requested_task_ids_.find(task_id) != cancel_requested_task_ids_.end();
}

bool CaptureTaskService::IsTerminalStatus(const std::string& status) const {
    return status == "succeeded" || status == "failed" || status == "cancelled";
}

void CaptureTaskService::MarkTaskCancellingUnlocked(CaptureTaskSnapshot* task) const {
    if (task == NULL || IsTerminalStatus(task->status)) {
        return;
    }
    task->status = "cancelling";
    task->code = ToCode(SdkStatusCode::Ok);
    task->message = "cancelling";
}

void CaptureTaskService::BeginTaskActivityUnlocked(const std::string& task_id) {
    ++task_activity_counts_[task_id];
}

void CaptureTaskService::EndTaskActivityUnlocked(const std::string& task_id) {
    const auto it = task_activity_counts_.find(task_id);
    if (it != task_activity_counts_.end() && --it->second == 0) task_activity_counts_.erase(it);
    task_activity_cv_.notify_all();
}

void CaptureTaskService::EndTaskActivity(const std::string& task_id) noexcept {
    try {
        std::lock_guard<std::mutex> lock(mu_);
        EndTaskActivityUnlocked(task_id);
    } catch (...) {
    }
}

void CaptureTaskService::FailTaskAfterException(const std::string& task_id, const char* message) noexcept {
    try {
        std::lock_guard<std::mutex> lock(mu_);
        const auto it = tasks_.find(task_id);
        if (it == tasks_.end() || IsTerminalStatus(it->second.status)) return;
        CaptureTaskSnapshot& task = it->second;
        const bool processing = task.acquisition_status == "captured";
        auto session = session_summaries_.find(task.connection_id);
        if (session != session_summaries_.end()) {
            auto summary = session->second.find(task.device_id);
            if (summary != session->second.end()) {
                if (processing && summary->second.pending_count > 0) --summary->second.pending_count;
                ++summary->second.failed_count;
            }
        }
        ReleaseCaptureOwnershipUnlocked(task_id, false);
        task.status = "failed";
        task.processing_status = processing ? "failed" : "skipped";
        if (!processing) task.acquisition_status = "failed";
        task.code = ToCode(SdkStatusCode::InternalError);
        task.message = message;
        task.error = message;
    } catch (...) {
        // Allocation failure during reporting must not escape the thread boundary.
    }
}

void CaptureTaskService::FailProcessingQueue(const std::string& device_id) noexcept {
    // A failure before dequeue must not strand the remaining queue behind a dead
    // device worker. Each pending task still owns its queue reference here.
    for (;;) {
        std::string task_id;
        try {
            {
                std::lock_guard<std::mutex> lock(mu_);
                auto queue = processing_queues_.find(device_id);
                if (queue == processing_queues_.end() || queue->second.empty()) {
                    active_processing_devices_.erase(device_id);
                    if (queue != processing_queues_.end()) processing_queues_.erase(queue);
                    return;
                }
                task_id.swap(queue->second.front());
                queue->second.pop_front();
            }
            FailTaskAfterException(task_id, "capture processing worker failed");
            EndTaskActivity(task_id);
        } catch (...) {
            return;
        }
    }
}

CaptureTaskSnapshot CaptureTaskService::CompleteCancelledTask(const std::string& task_id,
                                                              const CaptureTaskStartRequest& request,
                                                              const std::string& reason) {
    CaptureTaskSnapshot task;
    {
        std::lock_guard<std::mutex> lock(mu_);
        std::map<std::string, CaptureTaskSnapshot>::iterator it = tasks_.find(task_id);
        if (it == tasks_.end()) {
            return task;
        }
        it->second.status = "cancelled";
        it->second.code = ToCode(SdkStatusCode::Ok);
        it->second.message = reason.empty() ? "cancelled" : reason;
        if (it->second.acquisition_status != "captured") {
            it->second.acquisition_status = "cancelled";
        }
        it->second.processing_status = "cancelled";
        ReleaseCaptureOwnershipUnlocked(task_id, false);
        task = it->second;
    }
    PublishEvent(request.connection_id, "capture.cancelled", task);
    return task;
}

void CaptureTaskService::RunCaptureTask(const std::string& task_id) noexcept {
    TaskActivityGuard activity = {this, &task_id, true};
    try {
        RunCaptureTaskImpl(task_id);
    } catch (const std::exception& error) {
        FailTaskAfterException(task_id, error.what());
    } catch (...) {
        FailTaskAfterException(task_id, "capture worker failed");
    }
}

void CaptureTaskService::RunCaptureTaskImpl(const std::string& task_id) {
    CaptureTaskStartRequest request;
    CaptureTaskSnapshot running;
    bool cancelled_before_start = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        const std::map<std::string, CaptureTaskStartRequest>::const_iterator request_it = requests_.find(task_id);
        std::map<std::string, CaptureTaskSnapshot>::iterator task_it = tasks_.find(task_id);
        if (request_it == requests_.end() || task_it == tasks_.end()) {
            return;
        }
        request = request_it->second;
        cancelled_before_start = cancel_requested_task_ids_.find(task_id) != cancel_requested_task_ids_.end();
        if (!cancelled_before_start) task_it->second.status = "running";
        task_it->second.acquisition_status = "capturing";
        running = task_it->second;
    }
    if (cancelled_before_start) {
        CompleteCancelledTask(task_id, request, "capture cancelled");
        return;
    }
    PublishEvent(request.connection_id, "capture.started", running);
    // A synchronous notification can outlive a concurrent teardown request.
    // Recheck cancellation before entering the physical Provider, not only
    // after it returns. Open callers that do not request cancellation are unchanged.
    if (IsCancelRequested(task_id)) {
        CompleteCancelledTask(task_id, request, "capture cancelled");
        return;
    }

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
    if (IsCancelRequested(task_id)) {
        CompleteCancelledTask(task_id, request, "capture cancelled");
        return;
    }
    if (!IsOkStatusCode(raw_capture.code) || !raw_capture.captured) {
        CompleteCaptureFailure(task_id, request, raw_capture.message.empty() ? "capture failed" : raw_capture.message,
                               raw_capture.code);
        return;
    }
    std::string stage_error;
    if (!StageCapturedRaw(task_id, &raw_capture, &stage_error)) {
        CompleteCaptureFailure(task_id, request, stage_error);
        return;
    }
    if (IsCancelRequested(task_id)) {
        CompleteCancelledTask(task_id, request, "capture cancelled");
        return;
    }

    bool start_processing_failed = false;
    bool cancelled_before_queue = false;
    CaptureTaskSnapshot captured_task;
    {
        std::lock_guard<std::mutex> lock(mu_);
        std::map<std::string, CaptureTaskSnapshot>::iterator task_it = tasks_.find(task_id);
        std::map<std::string, CaptureTaskStartRequest>::iterator request_it = requests_.find(task_id);
        if (task_it == tasks_.end() || request_it == requests_.end()) {
            return;
        }
        cancelled_before_queue = cancel_requested_task_ids_.find(task_id) != cancel_requested_task_ids_.end();
        if (!cancelled_before_queue) {
            request_it->second.raw_capture = raw_capture;
            task_it->second.acquisition_status = "captured";
            task_it->second.processing_status = "queued";
            ReleaseCaptureOwnershipUnlocked(task_id, false);
            CaptureSessionDeviceSummary& summary = session_summaries_[request.connection_id][request.device_id];
            summary.device_id = request.device_id;
            ++summary.captured_count;
            ++summary.pending_count;
            // Register both queue ownership and a possible device worker transactionally.
            // The acquisition guard remains active through raw-captured event delivery.
            captured_task = task_it->second;
            BeginTaskActivityUnlocked(task_id);
            bool enqueued = false;
            bool owns_processing_slot = false;
            try {
                processing_queues_[request.device_id].push_back(task_id);
                enqueued = true;
                owns_processing_slot = active_processing_devices_.insert(request.device_id).second;
                if (owns_processing_slot) {
                    workers_.reserve(workers_.size() + 1);
                    std::thread worker(&CaptureTaskService::RunProcessingQueue, this, request.device_id);
                    workers_.push_back(std::move(worker));
                }
            } catch (...) {
                if (owns_processing_slot) active_processing_devices_.erase(request.device_id);
                if (enqueued) processing_queues_[request.device_id].pop_back();
                EndTaskActivityUnlocked(task_id);
                start_processing_failed = true;
            }
        }

    }
    if (cancelled_before_queue) {
        CompleteCancelledTask(task_id, request, "capture cancelled");
        return;
    }
    if (start_processing_failed) {
        CapturePipelineResult failure;
        failure.code = ToCode(SdkStatusCode::InternalError);
        failure.message = "failed to start capture processing worker";
        failure.status = "failed";
        CompleteProcessingTask(task_id, request, failure);
        return;
    }
    try {
        SDK_OPEN_LOG_INFO("[capture_task] raw captured task_id={} source={} device={} queued_for_processing=true",
                          task_id, captured_task.capture_source, request.device_id);
    } catch (...) {
        // Logging after queue admission must not turn accepted processing into failure.
    }
    PublishSessionUpdate(request.connection_id, request.device_id, task_id, "raw_captured");
}

void CaptureTaskService::RunProcessingQueue(const std::string& device_id) noexcept {
    try {
        RunProcessingQueueImpl(device_id);
    } catch (...) {
        FailProcessingQueue(device_id);
    }
}

void CaptureTaskService::RunProcessingQueueImpl(const std::string& device_id) {
    for (;;) {
        std::string task_id;
        CaptureTaskStartRequest request;
        TaskActivityGuard activity = {this, &task_id, false};
        {
            std::lock_guard<std::mutex> lock(mu_);
            auto queue = processing_queues_.find(device_id);
            if (queue == processing_queues_.end() || queue->second.empty()) {
                active_processing_devices_.erase(device_id);
                if (queue != processing_queues_.end()) processing_queues_.erase(queue);
                return;
            }
            // Copy before popping so allocation failure leaves a recoverable queue.
            task_id = queue->second.front();
            const auto request_it = requests_.find(task_id);
            const auto task_it = tasks_.find(task_id);
            if (request_it == requests_.end() || task_it == tasks_.end()) {
                queue->second.pop_front();
                EndTaskActivityUnlocked(task_id);
                continue;
            }
            request = request_it->second;
            queue->second.pop_front();
            activity.active = true; // adopts, rather than increments, the queue reference
            task_it->second.processing_status = "running";
        }
        try {
            const CapturePipelineResult result = RunProcessingPipeline(task_id, request);
            CompleteProcessingTask(task_id, request, result);
        } catch (const std::exception& error) {
            FailTaskAfterException(task_id, error.what());
        } catch (...) {
            FailTaskAfterException(task_id, "capture processing failed");
        }
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
    // Provider 写入任务 raw 目录时，路径理论上会随结果返回。为兼容已经部署的
    // Provider 版本以及跨 DLL 边界的字段缺失，若响应未带路径但目标文件已经存在，
    // 直接接管该文件；绝不回退到原图 Base64 解码。
    std::string source_path = !raw_capture->original_path.empty()
                                  ? raw_capture->original_path
                                  : raw_capture->output_path;
    if (source_path.empty() && FileExists(original_path)) {
        source_path = original_path;
    }
    const bool source_exists = !source_path.empty() && FileExists(source_path);
    bool original_persisted = false;
    if (source_exists) {
        original_persisted = source_path == original_path || CopyFileBinary(source_path, original_path);
    }
    if (!original_persisted || !FileExists(original_path)) {
        SDK_OPEN_LOG_ERROR(
            "[capture_task] persist original failed task_id={} source={} source_exists={} target={} target_exists={} "
            "captured={} code={} output_path={} original_path={} laser_path={}",
            task_id,
            source_path,
            source_exists,
            original_path,
            FileExists(original_path),
            raw_capture->captured,
            raw_capture->code,
            raw_capture->output_path,
            raw_capture->original_path,
            raw_capture->laser_path);
        if (error != NULL) {
            *error = "failed to persist captured original";
        }
        return false;
    }
    raw_capture->original_path = original_path;
    raw_capture->output_path = original_path;

    if (raw_capture->laser_path.empty()) {
        return true;
    }
    const std::string laser_path = JoinPath(raw_dir, "laser.jpg");
    if ((raw_capture->laser_path != laser_path && !CopyFileBinary(raw_capture->laser_path, laser_path)) ||
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
    pipeline_request.should_cancel = [this, task_id]() { return IsCancelRequested(task_id); };
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

    if (final_result.status == "cancelled" || !IsOkStatusCode(final_result.code) || request.pipeline.steps.empty() || !providers_.image_enhance_provider) {
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
    std::map<std::string, int> online_usage_by_capability;
    for (std::size_t step_index = 0; !failed && step_index < request.pipeline.steps.size(); ++step_index) {
        if (IsCancelRequested(task_id)) {
            final_result.code = ToCode(SdkStatusCode::Ok);
            final_result.message = "cancelled";
            final_result.status = "cancelled";
            return final_result;
        }
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
        if (IsCancelRequested(task_id)) {
            final_result.code = ToCode(SdkStatusCode::Ok);
            final_result.message = "cancelled";
            final_result.status = "cancelled";
            return final_result;
        }
        if (!IsOkStatusCode(step_result.code)) {
            if (step.on_error == "skip") {
                final_result.warnings.push_back(step.type + " skipped: " + step_result.message);
                continue;
            }
            failed = true;
            error = step_result.message;
            break;
        }
        if (request.confirm_online_enhance_quota && IsOnlineEnhanceCapability(step.type)) {
            const std::string capability = NormalizeOnlineEnhanceCapability(step.type);
            const int units = static_cast<int>(step_result.pages.empty() ? pages.size() : step_result.pages.size());
            online_usage_by_capability[capability] += units > 0 ? units : 1;
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
    if (request.confirm_online_enhance_quota && !online_usage_by_capability.empty()) {
        // Do not begin confirmation after cancellation has been observed. Once
        // the external call starts it cannot be forcibly stopped or refunded.
        if (IsCancelRequested(task_id)) {
            final_result.code = ToCode(SdkStatusCode::Ok);
            final_result.message = "cancelled";
            final_result.status = "cancelled";
            return final_result;
        }
        OnlineEnhanceQuotaCredentials quota_credentials;
        quota_credentials.online_api_key = request.online_api_key;
        quota_credentials.online_session_token = request.session_token;
        quota_credentials.authz_base_url = request.authz_base_url;
        QuotaConsumeResult quota_result;
        try {
            quota_result = ConfirmOnlineEnhanceQuota(providers_, quota_credentials, task_id, online_usage_by_capability);
        } catch (...) {
            // Do not publish credentials or provider internals from exception text.
            quota_result.code = ToCode(SdkStatusCode::InternalError);
            quota_result.message = "online image enhance quota confirmation failed";
        }
        if (!IsOkStatusCode(quota_result.code)) {
            final_result.code = quota_result.code;
            final_result.message = quota_result.message.empty() ? "online image enhance quota confirm failed" : quota_result.message;
            final_result.status = "failed";
            final_result.assets.clear();
            return final_result;
        }
    }
    if (IsCancelRequested(task_id)) {
        final_result.code = ToCode(SdkStatusCode::Ok);
        final_result.message = "cancelled";
        final_result.status = "cancelled";
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
                                                const std::string& message,
                                                int failure_code) {
    CaptureTaskSnapshot task;
    {
        std::lock_guard<std::mutex> lock(mu_);
        std::map<std::string, CaptureTaskSnapshot>::iterator it = tasks_.find(task_id);
        if (it == tasks_.end()) {
            return;
        }
        const bool cancelled = cancel_requested_task_ids_.find(task_id) != cancel_requested_task_ids_.end();
        it->second.status = cancelled ? "cancelled" : "failed";
        it->second.acquisition_status = cancelled ? "cancelled" : "failed";
        it->second.processing_status = cancelled ? "cancelled" : "skipped";
        const int code = request.preserve_acquisition_error_code && !IsOkStatusCode(failure_code)
            ? failure_code : ToCode(SdkStatusCode::CaptureFailed);
        it->second.code = cancelled ? ToCode(SdkStatusCode::Ok) : code;
        it->second.message = cancelled ? "cancelled" : (message.empty() ? "capture failed" : message);
        it->second.error = cancelled ? "" : it->second.message;
        ReleaseCaptureOwnershipUnlocked(task_id, false);
        CaptureSessionDeviceSummary& summary = session_summaries_[request.connection_id][request.device_id];
        summary.device_id = request.device_id;
        if (!cancelled) {
            ++summary.failed_count;
        }
        task = it->second;
    }
    PublishEvent(request.connection_id, task.status == "cancelled" ? "capture.cancelled" : "capture.failed", task);
    PublishSessionUpdate(request.connection_id, request.device_id, task_id,
                         task.status == "cancelled" ? "capture_cancelled" : "capture_failed");
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
        const bool cancelled = result.status == "cancelled" ||
            cancel_requested_task_ids_.find(task_id) != cancel_requested_task_ids_.end();
        it->second.status = cancelled ? "cancelled" :
            (result.status.empty() ? (IsOkStatusCode(result.code) ? "succeeded" : "failed") : result.status);
        succeeded = IsOkStatusCode(result.code) && it->second.status == "succeeded";
        it->second.processing_status = cancelled ? "cancelled" : (succeeded ? "succeeded" : "failed");
        it->second.stages = result.stages;
        it->second.assets = result.assets;
        AttachAssetUrls(task_id, &it->second.assets);
        it->second.warnings = result.warnings;
        it->second.code = cancelled ? ToCode(SdkStatusCode::Ok) : result.code;
        it->second.message = cancelled ? "cancelled" : result.message;
        it->second.error = cancelled || succeeded ? "" : result.message;
        CaptureSessionDeviceSummary& summary = session_summaries_[request.connection_id][request.device_id];
        summary.device_id = request.device_id;
        if (summary.pending_count > 0) {
            --summary.pending_count;
        }
        if (cancelled) {
            // Cancellation is not a processing failure and does not roll back
            // already captured files or quota accounting.
        } else if (succeeded) {
            ++summary.processed_count;
        } else {
            ++summary.failed_count;
        }
        task = it->second;
    }
    task_activity_cv_.notify_all();
    const bool cancelled = task.status == "cancelled";
    PublishEvent(request.connection_id, cancelled ? "capture.cancelled" : (succeeded ? "capture.completed" : "capture.failed"), task);
    PublishSessionUpdate(request.connection_id, request.device_id, task_id,
                         cancelled ? "capture_cancelled" : (succeeded ? "processing_completed" : "processing_failed"));
}

void CaptureTaskService::PublishEvent(const std::string& connection_id,
                                      const std::string& event,
                                      const CaptureTaskSnapshot& task,
                                      const SdkCaptureStageResult* stage) const {
    SnapshotEventSink snapshot_sink;
    EventSink event_sink;
    try {
        std::lock_guard<std::mutex> lock(mu_);
        snapshot_sink = snapshot_event_sink_;
    } catch (...) {
        // A typed sink copy can allocate. Legacy delivery remains independent.
    }
    try {
        std::lock_guard<std::mutex> lock(mu_);
        event_sink = event_sink_;
    } catch (...) {
        // A legacy sink copy can allocate. Typed delivery remains independent.
    }

    if (snapshot_sink) {
        try {
            // `task` is the caller-provided event snapshot. Do not re-read the
            // task map here: typed consumers must observe the same state that
            // produced the legacy event.
            snapshot_sink(connection_id, event, task, stage);
        } catch (...) {
            // Typed delivery is best effort and must not suppress legacy JSON
            // delivery or prevent worker cleanup.
        }
    }

    if (event_sink) {
        try {
            Json payload = BuildTaskJson(task);
            if (stage != NULL) {
                payload["stage"] = BuildStageJson(*stage);
            }
            event_sink(connection_id, BuildWsEvent(event, payload));
        } catch (...) {
            // Legacy delivery is best effort and must not escape worker threads.
        }
    }
}

void CaptureTaskService::PublishSessionUpdate(const std::string& connection_id,
                                              const std::string& device_id,
                                              const std::string& task_id,
                                              const std::string& reason) const {
    try {
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
    } catch (...) {
    }
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
