// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <string>

#include "sdk_status_code.h"

namespace editor {
namespace sdk {

// 汇总所有可能占用 SDK Open 临时目录的异步任务。
// 清理前 Open 层和 private 层会分别填充自己维护的任务数量。
struct SdkStorageActiveTasks {
    std::size_t capture = 0;
    std::size_t image_enhance = 0;
    std::size_t ocr = 0;
    std::size_t sane = 0;
    std::size_t twain = 0;

    std::size_t Total() const {
        return capture + image_enhance + ocr + sane + twain;
    }
};

struct SdkStorageCleanupRequest {
    // 只允许 private 层清理该工作目录下固定的 capture/tasks 子目录。
    std::string work_dir;
};

struct SdkStorageCleanupResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool capture_removed = false;
    bool tasks_removed = false;
    // 对外返回的任务数量按任务 ID 语义统计，不能把资产索引等附属记录重复计入。
    std::size_t cleared_task_count = 0;
    // private 分项计数用于 Open 层合并时去重；例如 TWAIN 在两层都保留快照。
    std::size_t cleared_ocr_task_count = 0;
    std::size_t cleared_twain_task_count = 0;
    SdkStorageActiveTasks active;
};

class ISdkStorageProvider {
public:
    virtual ~ISdkStorageProvider() = default;
    virtual std::string ProviderName() const = 0;
    // 执行目录删除并清理 private 层的终态任务记录。
    virtual SdkStorageCleanupResult CleanupTemp(const SdkStorageCleanupRequest& request) = 0;
};

} // namespace sdk
} // namespace editor
