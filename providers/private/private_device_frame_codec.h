// Copyright (c) 2026 CZUR Tech. All rights reserved.

#pragma once

#include <string>

#include "device_manager.h"
#include "opencv2/opencv.hpp"

namespace editor {
namespace sdk {
namespace private_impl {
namespace device_frame_codec {

inline editor::devices::DeviceStreamOptions BuildStreamOptions(
    const std::string& output_pixel_format,
    bool force_native_mjpeg) {
    editor::devices::DeviceStreamOptions options;
    options.decodeFrame = !force_native_mjpeg && output_pixel_format == "bgr24";
    options.deliverNativeMjpeg = force_native_mjpeg || !options.decodeFrame;
    options.captureRawOnly = false;
    return options;
}

inline bool NeedsBgrFrame(const std::string& output_pixel_format,
                          bool realtime_detect_rects,
                          bool turn_detect_enabled) {
    return output_pixel_format == "bgr24" || realtime_detect_rects || turn_detect_enabled;
}

inline bool DecodeNativeMjpegFrame(editor::devices::GenericFrame* frame) {
    if (frame == NULL || frame->native_mjpeg.empty()) {
        return false;
    }

    cv::Mat image;
    try {
        image = cv::imdecode(frame->native_mjpeg, cv::IMREAD_COLOR);
    } catch (const cv::Exception&) {
        return false;
    }
    if (image.empty() || image.channels() != 3) {
        return false;
    }
    if (!image.isContinuous()) {
        image = image.clone();
    }

    frame->width = image.cols;
    frame->height = image.rows;
    frame->data.assign(image.data, image.data + image.total() * image.elemSize());
    return !frame->data.empty();
}

inline std::string OhosDeviceConfigPath(const std::string& runtime_dir) {
    if (runtime_dir.empty()) {
        return "configs/device_config_compact.conf";
    }
    if (runtime_dir[runtime_dir.size() - 1] == '/') {
        return runtime_dir + "configs/device_config_compact.conf";
    }
    return runtime_dir + "/configs/device_config_compact.conf";
}

} // namespace device_frame_codec
} // namespace private_impl
} // namespace sdk
} // namespace editor
