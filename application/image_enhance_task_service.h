// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "sdk_json_utils.h"
#include "sdk_provider_bundle.h"
#include "sdk_provider_types.h"

namespace editor {
namespace sdk {

class ImageEnhanceTaskService {
public:
    struct OutputPublication {
        int code = ToCode(SdkStatusCode::Ok);
        std::string message = "ok";
        std::vector<std::string> output_paths;
    };
    // Optional in-process finalization, owned by the task worker. The default
    // Open path has no publisher and retains its existing output semantics.
    using OutputPublisher = std::function<OutputPublication(const std::vector<std::string>&)>;
    using EventSink = std::function<void(const std::string&, const Json&)>;

    explicit ImageEnhanceTaskService(const ProviderBundle& providers, const std::string& asset_base_url = "");
    ~ImageEnhanceTaskService();

    void SetEventSink(EventSink sink);
    SdkImageEnhanceTaskResult StartTask(const SdkImageEnhanceTaskRequest& request);
    SdkImageEnhanceTaskResult StartTask(const SdkImageEnhanceTaskRequest& request, OutputPublisher publisher);
    SdkImageEnhanceTaskSnapshot GetTask(const std::string& connection_id, const std::string& task_id) const;
    SdkImageEnhanceTaskResult CancelTask(const std::string& connection_id, const SdkImageEnhanceCancelRequest& request);
    void CancelAndWait(const std::string& connection_id = std::string());
    std::size_t ActiveTaskCount() const;
    std::size_t ClearFinishedTasks();

private:
    void RunTask(const std::string& task_id, SdkImageEnhanceTaskRequest request, OutputPublisher publisher) noexcept;
    void RunTaskImpl(const std::string& task_id, SdkImageEnhanceTaskRequest request, const OutputPublisher& publisher);
    void PublishOutputs(const std::string& task_id, const OutputPublisher& publisher,
                        SdkImageEnhanceTaskSnapshot* task);
    void MarkWorkerExited(const std::string& task_id);
    void PublishEvent(const SdkImageEnhanceTaskSnapshot& task) const;
    SdkImageEnhanceTaskSnapshot GetTaskUnlocked(const std::string& task_id) const;
    void AttachAssetUrls(const std::string& task_id, std::vector<SdkCaptureAsset>* assets) const;
    std::string NextTaskId();

    ProviderBundle providers_;
    std::string asset_base_url_;
    mutable std::mutex mu_;
    std::map<std::string, SdkImageEnhanceTaskSnapshot> tasks_;
    std::set<std::string> cancel_requested_;
    // 协议状态可能先进入终态；该集合以 worker 实际退出作为清理边界。
    std::set<std::string> active_worker_task_ids_;
    std::condition_variable worker_cv_;
    std::vector<std::thread> workers_;
    EventSink event_sink_;
    std::atomic<uint64_t> next_task_seq_;
};

Json BuildImageEnhanceTaskJson(const SdkImageEnhanceTaskSnapshot& task);
Json BuildImageEnhanceCapabilityProviderJson(const SdkImageEnhanceCapabilityResult& provider);

} // namespace sdk
} // namespace editor
