// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "sdk_ws_video_backpressure.h"

namespace editor {
namespace sdk {
namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

detail::SdkWsVideoBackpressure::FramePairPtr MakeFrame(uint64_t sequence) {
    std::shared_ptr<detail::SdkWsVideoFramePair> frame(new detail::SdkWsVideoFramePair());
    frame->frame_seq = sequence;
    frame->metadata = "meta-" + std::to_string(static_cast<unsigned long long>(sequence));
    frame->binary = "jpeg-" + std::to_string(static_cast<unsigned long long>(sequence));
    return frame;
}

void TestLatestPendingFrameWins() {
    detail::SdkWsVideoBackpressure queue;
    detail::SdkWsVideoBackpressure::OfferResult first = queue.Offer(MakeFrame(1));
    Require(first.schedule_wake, "first frame should schedule one wake");
    Require(!first.replaced_pending, "first frame should not replace another frame");
    Require(queue.PendingCount() == 1u, "pending queue should contain exactly one frame");

    detail::SdkWsVideoBackpressure::OfferResult second = queue.Offer(MakeFrame(2));
    Require(!second.schedule_wake, "replacement should not schedule another wake");
    Require(second.replaced_pending, "newest frame should replace the pending frame");
    Require(queue.PendingCount() == 1u, "replacement must keep the queue bounded to one frame");

    detail::SdkWsVideoBackpressure::FramePairPtr sent = queue.BeginSend();
    Require(sent && sent->frame_seq == 2u, "the latest pending frame should be sent");
    Require(sent->metadata == "meta-2" && sent->binary == "jpeg-2",
            "metadata and JPEG must remain paired");
    Require(queue.PendingCount() == 0u && queue.IsInFlight(),
            "begin send should move the pair into the single in-flight slot");
}

void TestSlowConnectionKeepsOneInFlightAndOnePending() {
    detail::SdkWsVideoBackpressure queue;
    queue.Offer(MakeFrame(10));
    detail::SdkWsVideoBackpressure::FramePairPtr in_flight = queue.BeginSend();
    Require(in_flight && in_flight->frame_seq == 10u, "initial frame should enter flight");

    for (uint64_t sequence = 11; sequence <= 100; ++sequence) {
        const detail::SdkWsVideoBackpressure::OfferResult result = queue.Offer(MakeFrame(sequence));
        Require(!result.schedule_wake, "an in-flight connection must not accumulate wake posts");
        Require(queue.PendingCount() == 1u, "slow connection pending queue must stay bounded");
    }

    Require(queue.CompleteSend(), "drain completion should request the pending latest frame");
    detail::SdkWsVideoBackpressure::FramePairPtr next = queue.BeginSend();
    Require(next && next->frame_seq == 100u, "recovery should send only the newest frame");
    Require(next->metadata == "meta-100" && next->binary == "jpeg-100",
            "latest metadata must match the latest JPEG");
}

void TestDisconnectClearsPendingAndWakeState() {
    detail::SdkWsVideoBackpressure queue;
    queue.Offer(MakeFrame(1));
    queue.BeginSend();
    queue.Offer(MakeFrame(2));
    Require(queue.IsInFlight() && queue.HasPending(), "test setup should contain both bounded slots");

    queue.Disconnect();
    Require(!queue.IsConnected(), "disconnect should mark the state closed");
    Require(!queue.IsInFlight() && !queue.HasPending() && queue.PendingCount() == 0u,
            "disconnect should clear pending and in-flight state");
    Require(!queue.CompleteSend(), "a late completion after disconnect must be ignored");
    Require(!queue.Offer(MakeFrame(3)).schedule_wake,
            "a disconnected connection must not schedule new work");
    Require(!queue.BeginSend(), "a disconnected connection must not produce a frame");
}

} // namespace
} // namespace sdk
} // namespace editor

int main() {
    try {
        editor::sdk::TestLatestPendingFrameWins();
        editor::sdk::TestSlowConnectionKeepsOneInFlightAndOnePending();
        editor::sdk::TestDisconnectClearsPendingAndWakeState();
        std::cout << "sdk_ws_video_backpressure_test passed" << std::endl;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "sdk_ws_video_backpressure_test failed: " << error.what() << std::endl;
        return 1;
    }
}
