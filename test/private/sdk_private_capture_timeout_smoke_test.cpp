// Copyright (c) 2026 CZUR Tech. All rights reserved.

#include <memory>

#include "capture_pending_identity.h"
#include "sdk_provider_types.h"

int main() {
    typedef int CaptureIdentity;
    const std::shared_ptr<CaptureIdentity> first(new CaptureIdentity(1));
    const std::shared_ptr<CaptureIdentity> second(new CaptureIdentity(2));
    const std::shared_ptr<CaptureIdentity> missing;

    using editor::sdk::kDefaultCaptureTimeoutMs;
    using editor::sdk::private_impl::MatchPendingCapture;
    using editor::sdk::private_impl::PendingCaptureMatch;
    return kDefaultCaptureTimeoutMs == 30000 &&
           MatchPendingCapture(first, first) == PendingCaptureMatch::Matched &&
           MatchPendingCapture(first, second) == PendingCaptureMatch::Replaced &&
           MatchPendingCapture(first, missing) == PendingCaptureMatch::Missing &&
           MatchPendingCapture(missing, second) == PendingCaptureMatch::Matched
        ? 0
        : 1;
}
