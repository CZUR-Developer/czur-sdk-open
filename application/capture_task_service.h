// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <atomic>
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
    std::string device_id;
    std::string output_dir;
    bool include_base64 = false;
    int timeout_ms = 15000;
    AuthContext auth_context;
    SdkCaptureProfile profile;
};

struct CaptureTaskSnapshot {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    std::string task_id;
    std::string connection_id;
    std::string device_id;
    std::string status = "queued";
    int profile_revision = 1;
    std::vector<SdkCaptureStageResult> stages;
    std::vector<SdkCaptureAsset> assets;
    std::vector<std::string> warnings;
    std::string error;
};

struct CaptureTaskStartResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool accepted = false;
    CaptureTaskSnapshot task;
};

class CaptureTaskService {
public:
    using EventSink = std::function<void(const std::string&, const Json&)>;

    explicit CaptureTaskService(const ProviderBundle& providers);
    ~CaptureTaskService();

    void SetEventSink(EventSink sink);
    CaptureTaskStartResult StartTask(const CaptureTaskStartRequest& request);
    CaptureTaskSnapshot GetTask(const std::string& connection_id, const std::string& task_id) const;

private:
    void RunTask(const std::string& task_id, CaptureTaskStartRequest request);
    void PublishEvent(const std::string& connection_id,
                      const std::string& event,
                      const CaptureTaskSnapshot& task,
                      const SdkCaptureStageResult* stage = nullptr) const;
    CaptureTaskSnapshot GetTaskUnlocked(const std::string& task_id) const;
    std::string NextTaskId();

    CapturePipelineService pipeline_service_;
    mutable std::mutex mu_;
    std::map<std::string, CaptureTaskSnapshot> tasks_;
    std::set<std::string> busy_devices_;
    std::vector<std::thread> workers_;
    EventSink event_sink_;
    std::atomic<uint64_t> next_task_seq_;
};

} // namespace sdk
} // namespace editor
