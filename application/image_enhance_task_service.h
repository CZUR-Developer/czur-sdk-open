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

#include "sdk_json_utils.h"
#include "sdk_provider_bundle.h"
#include "sdk_provider_types.h"

namespace editor {
namespace sdk {

class ImageEnhanceTaskService {
public:
    using EventSink = std::function<void(const std::string&, const Json&)>;

    explicit ImageEnhanceTaskService(const ProviderBundle& providers, const std::string& asset_base_url = "");
    ~ImageEnhanceTaskService();

    void SetEventSink(EventSink sink);
    SdkImageEnhanceTaskResult StartTask(const SdkImageEnhanceTaskRequest& request);
    SdkImageEnhanceTaskSnapshot GetTask(const std::string& connection_id, const std::string& task_id) const;
    SdkImageEnhanceTaskResult CancelTask(const std::string& connection_id, const SdkImageEnhanceCancelRequest& request);

private:
    void RunTask(const std::string& task_id, SdkImageEnhanceTaskRequest request);
    void PublishEvent(const SdkImageEnhanceTaskSnapshot& task) const;
    SdkImageEnhanceTaskSnapshot GetTaskUnlocked(const std::string& task_id) const;
    void AttachAssetUrls(const std::string& task_id, std::vector<SdkCaptureAsset>* assets) const;
    std::string NextTaskId();

    ProviderBundle providers_;
    std::string asset_base_url_;
    mutable std::mutex mu_;
    std::map<std::string, SdkImageEnhanceTaskSnapshot> tasks_;
    std::set<std::string> cancel_requested_;
    std::vector<std::thread> workers_;
    EventSink event_sink_;
    std::atomic<uint64_t> next_task_seq_;
};

Json BuildImageEnhanceTaskJson(const SdkImageEnhanceTaskSnapshot& task);
Json BuildImageEnhanceCapabilityProviderJson(const SdkImageEnhanceCapabilityResult& provider);

} // namespace sdk
} // namespace editor
