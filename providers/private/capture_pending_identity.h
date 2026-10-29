// Copyright (c) 2026 CZUR Tech. All rights reserved.

#pragma once

#include <memory>

namespace editor {
namespace sdk {
namespace private_impl {

enum class PendingCaptureMatch {
    Matched,
    Missing,
    Replaced
};

template <typename T>
PendingCaptureMatch MatchPendingCapture(const std::shared_ptr<T>& expected,
                                        const std::shared_ptr<T>& current) {
    if (!current) {
        return PendingCaptureMatch::Missing;
    }
    if (expected && expected != current) {
        return PendingCaptureMatch::Replaced;
    }
    return PendingCaptureMatch::Matched;
}

} // namespace private_impl
} // namespace sdk
} // namespace editor
