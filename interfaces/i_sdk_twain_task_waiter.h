// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>

namespace editor {
namespace sdk {

// Optional internal interface: keep ISdkTwainProvider's existing vtable intact.
// A terminal snapshot or cancellation acknowledgement does not prove that the
// native worker, helper and terminal callbacks have stopped accessing output.
class ISdkTwainTaskWaiter {
public:
    virtual ~ISdkTwainTaskWaiter() {}
    virtual bool SupportsTaskWait() const = 0;
    // 0 polls, positive values wait up to that many milliseconds, negative
    // values wait without a timeout. Unknown tasks and unsupported waits fail.
    virtual bool WaitForTask(const std::string& task_id, int timeout_ms) = 0;
};

} // namespace sdk
} // namespace editor
