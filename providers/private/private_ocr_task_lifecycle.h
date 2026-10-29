#pragma once

#include <cstddef>
#include <set>
#include <string>

namespace editor {
namespace sdk {
namespace private_impl {

// 记录底层 OCR 是否仍可能写盘；调用方负责使用 provider 互斥锁串行访问。
class PrivateOcrTaskLifecycle {
public:
    void MarkSubmitted(const std::string& task_id) {
        active_task_ids_.insert(task_id);
    }

    void MarkCompleted(const std::string& task_id) {
        active_task_ids_.erase(task_id);
    }

    bool IsActive(const std::string& task_id) const {
        return active_task_ids_.find(task_id) != active_task_ids_.end();
    }

    std::size_t ActiveTaskCount() const {
        return active_task_ids_.size();
    }

private:
    std::set<std::string> active_task_ids_;
};

} // namespace private_impl
} // namespace sdk
} // namespace editor
