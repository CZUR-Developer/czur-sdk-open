// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "capture_pipeline_service.h"
#include "sdk_json_utils.h"

namespace editor {
namespace sdk {

struct CaptureTaskStartRequest {
    std::string connection_id;
    std::string session_token;
    std::string device_id;
    std::string output_dir;
    bool include_base64 = false;
    int timeout_ms = kDefaultCaptureTimeoutMs;
    AuthContext auth_context;
    SdkCaptureProfile profile;
    // 硬拍异步任务的原始输入；为空时仍按普通 capture.take 调用 provider。
    SdkCaptureResult raw_capture;
    SdkImageEnhancePipeline pipeline;
    std::string online_api_key;
    std::string online_base_url;
};

struct CaptureTaskSnapshot {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    std::string task_id;
    std::string connection_id;
    std::string session_token;
    std::string device_id;
    std::string status = "queued";
    std::string capture_source = "manual";
    std::string acquisition_status = "queued";
    std::string processing_status = "not_started";
    int profile_revision = 1;
    std::vector<SdkCaptureStageResult> stages;
    std::vector<SdkCaptureAsset> assets;
    std::vector<std::string> warnings;
    std::string error;
};

struct CaptureAssetResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    SdkCaptureAsset asset;
};

struct CaptureTaskStartResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool accepted = false;
    int retry_after_ms = 0;
    CaptureTaskSnapshot task;
};

struct CaptureSessionDeviceSummary {
    std::string device_id;
    uint64_t captured_count = 0;
    uint64_t processed_count = 0;
    uint64_t failed_count = 0;
    uint64_t pending_count = 0;
};

struct CaptureSessionSummary {
    uint64_t captured_count = 0;
    uint64_t processed_count = 0;
    uint64_t failed_count = 0;
    uint64_t pending_count = 0;
    std::vector<CaptureSessionDeviceSummary> devices;
};

Json BuildCaptureSessionSummaryJson(const CaptureSessionSummary& summary);

class CaptureTaskService {
public:
    using EventSink = std::function<void(const std::string&, const Json&)>;

    explicit CaptureTaskService(const ProviderBundle& providers, const std::string& asset_base_url = "");
    ~CaptureTaskService();

    void SetEventSink(EventSink sink);
    // 先登记任务，并为手动拍照占用物理采集窗口。硬拍只占用同一限频窗口。
    // 调用方完成配额扣减后必须调用 StartReservedTask；扣减失败则调用
    // AbortReservedTask 释放登记与限频状态。
    CaptureTaskStartResult ReserveTask(const CaptureTaskStartRequest& request);
    CaptureTaskStartResult StartReservedTask(const std::string& task_id);
    void AbortReservedTask(const std::string& task_id);
    CaptureTaskSnapshot GetTask(const std::string& connection_id, const std::string& task_id) const;
    CaptureSessionSummary GetSessionSummary(const std::string& connection_id) const;
    CaptureAssetResult GetAsset(const std::string& connection_id,
                                const std::string& task_id,
                                const std::string& asset_id) const;
    std::size_t ActiveTaskCount() const;
    std::size_t ClearFinishedTasks();

private:
    void RunCaptureTask(const std::string& task_id);
    void RunProcessingQueue(const std::string& device_id);
    SdkCaptureResult CaptureRaw(const std::string& task_id, const CaptureTaskStartRequest& request) const;
    bool StageCapturedRaw(const std::string& task_id, SdkCaptureResult* raw_capture, std::string* error) const;
    void CompleteCaptureFailure(const std::string& task_id,
                                const CaptureTaskStartRequest& request,
                                const std::string& message);
    void CompleteProcessingTask(const std::string& task_id,
                                const CaptureTaskStartRequest& request,
                                const CapturePipelineResult& result);
    CapturePipelineResult RunProcessingPipeline(const std::string& task_id,
                                                const CaptureTaskStartRequest& request);
    void PublishEvent(const std::string& connection_id,
                      const std::string& event,
                      const CaptureTaskSnapshot& task,
                      const SdkCaptureStageResult* stage = nullptr) const;
    void PublishSessionUpdate(const std::string& connection_id,
                              const std::string& device_id,
                              const std::string& task_id,
                              const std::string& reason) const;
    CaptureTaskSnapshot GetTaskUnlocked(const std::string& task_id) const;
    CaptureSessionSummary GetSessionSummaryUnlocked(const std::string& connection_id) const;
    void AttachAssetUrls(const std::string& task_id, std::vector<SdkCaptureAsset>* assets) const;
    std::string NextTaskId();

    CapturePipelineService pipeline_service_;
    DeviceFacade device_facade_;
    ProviderBundle providers_;
    std::string asset_base_url_;
    mutable std::mutex mu_;
    std::map<std::string, CaptureTaskSnapshot> tasks_;
    std::map<std::string, CaptureTaskStartRequest> requests_;
    std::map<std::string, std::deque<std::string> > processing_queues_;
    std::set<std::string> active_capture_devices_;
    std::set<std::string> active_processing_devices_;
    std::map<std::string, std::chrono::steady_clock::time_point> capture_cooldown_until_;
    std::map<std::string, std::map<std::string, CaptureSessionDeviceSummary> > session_summaries_;
    // 该集合以采集或处理 worker 实际退出作为清理边界。
    std::set<std::string> active_worker_task_ids_;
    std::vector<std::thread> workers_;
    EventSink event_sink_;
    std::atomic<uint64_t> next_task_seq_;
};

} // namespace sdk
} // namespace editor
