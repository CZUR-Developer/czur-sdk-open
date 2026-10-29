// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <string>

#include "i_sdk_sane_provider.h"

namespace editor {
namespace sdk {
namespace private_impl {

class PrivateSaneProvider : public ISdkSaneProvider {
public:
    PrivateSaneProvider();
    ~PrivateSaneProvider() override;

    std::string ProviderName() const override;
    void SetDeviceEventSink(SdkSaneDeviceEventCallback sink) override;
    void SetScanTaskEventSink(SdkSaneScanTaskEventCallback sink) override;
    SdkSaneStatusResult GetStatus() override;
    SdkSaneListResult ListDevices(const SdkSaneListRequest& request) override;
    SdkSaneWatchResult WatchStart(const SdkSaneWatchRequest& request) override;
    SdkSaneWatchResult WatchStop(const SdkSaneWatchRequest& request) override;
    SdkSaneOpenResult OpenDevice(const SdkSaneOpenRequest& request) override;
    SdkSaneCloseResult CloseDevice(const SdkSaneCloseRequest& request) override;
    SdkSaneGetOptionsResult GetOptions(const SdkSaneGetOptionsRequest& request) override;
    SdkSaneSetOptionsResult SetOptions(const SdkSaneSetOptionsRequest& request) override;
    SdkSaneProfileListResult ListProfiles(const SdkSaneProfileRequest& request) override;
    SdkSaneProfileResult SaveProfile(const SdkSaneProfileRequest& request) override;
    SdkSaneProfileResult ApplyProfile(const SdkSaneProfileRequest& request) override;
    SdkSaneProfileResult DeleteProfile(const SdkSaneProfileRequest& request) override;
    SdkSaneScanResult Scan(const SdkSaneScanRequest& request) override;
    SdkSaneScanResult GetScan(const SdkSaneScanGetRequest& request) override;
    SdkSaneScanResult CancelScan(const SdkSaneScanCancelRequest& request) override;

#if defined(__linux__)
    // 供 Linux SDK Open 存储清理链路确认 private SANE 是否仍在写入 tasks 目录。
    std::size_t ActiveTaskCount() const;
    // 删除磁盘目录成功后，仅移除已结束的 SANE 任务快照。
    std::size_t ClearFinishedTasks();
#endif

private:
    void RunWatchLoop();
    void RunScanTask(const SdkSaneScanRequest& request,
                     const std::string& task_id,
                     const std::string& output_dir,
                     const std::string& image_extension);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace private_impl
} // namespace sdk
} // namespace editor
