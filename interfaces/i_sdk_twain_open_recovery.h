// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <memory>

namespace editor {
namespace sdk {

struct SdkTwainOpenRequest;
struct SdkTwainOpenResult;

// Internal optional interface, not part of the Local public ABI or the
// ISdkTwainProvider vtable. An operation identifies one admission, not a source.
class ISdkTwainOpenOperation {
public:
    virtual ~ISdkTwainOpenOperation() {}
    virtual SdkTwainOpenResult Open() = 0; // Single admission.
    // true proves this admission can no longer acquire/use resources and all
    // its resources were released, including a session published only natively
    // after the caller timed out. false/throw preserves ownership for retry.
    // 0 polls, positive is a bounded wait, negative waits indefinitely.
    virtual bool Recover(int timeout_ms) = 0;
};

class ISdkTwainOpenRecovery {
public:
    virtual ~ISdkTwainOpenRecovery() {}
    // Adapters must advertise only genuine underlying native support.
    virtual bool SupportsOpenRecovery() const = 0;
    // No physical effects; caller stores the operation before Open.
    virtual std::shared_ptr<ISdkTwainOpenOperation>
        CreateOpenOperation(const SdkTwainOpenRequest& request) = 0;
};

} // namespace sdk
} // namespace editor
