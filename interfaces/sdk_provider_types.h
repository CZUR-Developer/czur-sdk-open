// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "sdk_status_code.h"

namespace editor {
namespace sdk {

struct SdkVideoResolution {
    int width = 0;
    int height = 0;
    int real_width = 0;
    int real_height = 0;
    int fps = 0;
    std::string pixel_format = "jpeg";
    bool is_default = false;
};

struct SdkDeviceDescriptor {
    std::string device_id;
    std::string model;
    std::string display_name;
    int vid = 0;
    int pid = 0;
    std::string status = "offline";
    bool authorized = false;
    bool supports_video = false;
    std::vector<SdkVideoResolution> resolutions;
};

struct SdkDeviceOpenRequest {
    std::string device_id;
    int width = 0;
    int height = 0;
    int fps = 0;
    std::string pixel_format = "jpeg";
};

struct SdkDeviceOpenResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool opened = false;
    SdkDeviceDescriptor device;
};

struct SdkDeviceCloseRequest {
    std::string device_id;
};

struct SdkDeviceCloseResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool closed = false;
    bool was_opened = false;
};

struct SdkCaptureRequest {
    std::string device_id;
    bool include_base64 = false;
};

struct SdkCaptureResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool captured = false;
    std::string content_type;
    std::string payload;
    std::string output_path;
};

struct SdkVideoStartRequest {
    std::string device_id;
    std::string stream_id;
    int width = 0;
    int height = 0;
    int fps = 0;
    std::string pixel_format = "jpeg";
};

struct SdkVideoStartResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool accepted = false;
    std::string pixel_format = "jpeg";
    int width = 1280;
    int height = 720;
    int fps = 15;
};

struct SdkVideoFrame {
    std::string device_id;
    std::string stream_id;
    uint64_t frame_seq = 0;
    int64_t timestamp_ms = 0;
    int width = 0;
    int height = 0;
    std::string pixel_format = "jpeg";
    std::vector<uint8_t> payload;
};

using SdkVideoFrameCallback = std::function<void(const SdkVideoFrame&)>;

struct SdkVideoStopRequest {
    std::string device_id;
};

struct SdkVideoStopResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool stopped = false;
};

struct SdkVideoFormatRequest {
    std::string device_id;
    std::string pixel_format;
    int width = 0;
    int height = 0;
    int fps = 0;
};

struct SdkVideoFormatResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool applied = false;
};

struct SdkImageProcessRequest {
    std::string input_path;
    std::string output_path;
};

struct SdkImageProcessResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool processed = false;
};

struct SdkOcrRecognizeRequest {
    std::vector<std::string> input_files;
    std::string output_path;
};

struct SdkOcrRecognizeResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    std::string task_id;
};

struct SdkFileConvertRequest {
    std::string input_path;
    std::string output_path;
};

struct SdkFileConvertResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool accepted = false;
};

} // namespace sdk
} // namespace editor
