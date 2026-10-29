// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace editor {
namespace sdk {
namespace detail {

struct SdkWsVideoFramePair {
    std::string metadata;
    std::string binary;
    uint64_t frame_seq = 0;
};

// Per-connection state machine. All methods are called while the owning
// server's connection mutex is held.
class SdkWsVideoBackpressure {
public:
    typedef std::shared_ptr<const SdkWsVideoFramePair> FramePairPtr;

    struct OfferResult {
        bool schedule_wake = false;
        bool replaced_pending = false;
    };

    OfferResult Offer(const FramePairPtr& frame) {
        OfferResult result;
        if (!connected_ || !frame) {
            return result;
        }
        result.replaced_pending = static_cast<bool>(pending_);
        pending_ = frame;
        if (!in_flight_ && !wake_scheduled_) {
            wake_scheduled_ = true;
            result.schedule_wake = true;
        }
        return result;
    }

    FramePairPtr BeginSend() {
        wake_scheduled_ = false;
        if (!connected_ || in_flight_ || !pending_) {
            return FramePairPtr();
        }
        FramePairPtr frame = pending_;
        pending_.reset();
        in_flight_ = true;
        return frame;
    }

    bool CompleteSend() {
        if (!connected_) {
            return false;
        }
        in_flight_ = false;
        if (pending_ && !wake_scheduled_) {
            wake_scheduled_ = true;
            return true;
        }
        return false;
    }

    void Disconnect() {
        connected_ = false;
        pending_.reset();
        wake_scheduled_ = false;
        in_flight_ = false;
    }

    bool HasPending() const { return static_cast<bool>(pending_); }
    bool IsInFlight() const { return in_flight_; }
    bool IsConnected() const { return connected_; }
    size_t PendingCount() const { return pending_ ? 1u : 0u; }

private:
    bool connected_ = true;
    bool wake_scheduled_ = false;
    bool in_flight_ = false;
    FramePairPtr pending_;
};

} // namespace detail
} // namespace sdk
} // namespace editor
