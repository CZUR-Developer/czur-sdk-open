// Copyright (c) 2026 CZUR Tech. All rights reserved.

#pragma once

#include <vector>

#include "opencv2/opencv.hpp"

namespace editor {
namespace sdk {
namespace private_impl {
namespace graphic {

template <typename T>
void RehomeCzcvStdVector(std::vector<T>& value) {
    std::vector<T> local(value.begin(), value.end());

    // Some Windows Debug czcv_scan.dll APIs populate STL containers with
    // their CRT allocator. Rebuild the vector here so destruction is local.
    new (&value) std::vector<T>();
    value.swap(local);
}

inline void DetachCzcvOutputMat(cv::Mat& value) {
#if defined(_WIN32) && !defined(NDEBUG)
    // The image buffer may be allocated inside czcv_scan.dll. In Debug, letting
    // this module's cv::Mat destructor release that UMatData trips the CRT heap
    // ownership assertion. Clear only the header; callers clone first when they
    // need the pixels.
    value.flags = cv::Mat::MAGIC_VAL;
    value.dims = 0;
    value.rows = 0;
    value.cols = 0;
    value.data = nullptr;
    value.datastart = nullptr;
    value.dataend = nullptr;
    value.datalimit = nullptr;
    value.allocator = nullptr;
    value.u = nullptr;
    value.size.p = &value.rows;
    value.step.p = value.step.buf;
    value.step.buf[0] = 0;
    value.step.buf[1] = 0;
#else
    (void)value;
#endif
}

inline cv::Mat RehomeCzcvOutputMat(cv::Mat& value) {
    cv::Mat local;
    if (!value.empty()) {
        local = value.clone();
    }
    DetachCzcvOutputMat(value);
    return local;
}

} // namespace graphic
} // namespace private_impl
} // namespace sdk
} // namespace editor
