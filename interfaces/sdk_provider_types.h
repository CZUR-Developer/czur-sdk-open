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
    std::string pixel_format = "bgr24";
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
    bool image_transfer_protocol = false;
    std::vector<SdkVideoResolution> resolutions;
};

struct SdkDeviceOpenRequest {
    std::string device_id;
    int width = 0;
    int height = 0;
    int fps = 0;
    std::string pixel_format = "bgr24";
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
    std::string output_dir;
    bool include_base64 = false;
    int timeout_ms = 15000;
};

struct SdkCaptureResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool captured = false;
    std::string content_type;
    std::string payload;
    std::string output_path;
    std::string original_path;
    std::string laser_path;
    int width = 0;
    int height = 0;
    uint64_t size = 0;
};

using SdkCaptureCallback = std::function<void(const SdkCaptureResult&)>;

struct SdkVideoStartRequest {
    std::string device_id;
    std::string stream_id;
    int width = 0;
    int height = 0;
    int fps = 0;
    std::string pixel_format = "bgr24";
};

struct SdkVideoStartResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool accepted = false;
    std::string pixel_format = "bgr24";
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
    std::string pixel_format = "bgr24";
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

struct SdkCaptureProfile {
    std::string profile_version = "capture.profile.v1";
    int revision = 1;
    std::string device_id;
    int width = 0;
    int height = 0;
    int fps = 0;
    std::string page_processing = "single_page";
    std::string color_mode = "auto_optimize";
    std::string output_format = "jpg";
    bool thumbnail_original = true;
    bool thumbnail_page_processed = true;
    bool thumbnail_color_processed = false;
    bool thumbnail_final = true;
};

struct SdkCaptureAsset {
    std::string asset_id;
    std::string kind;
    std::string path;
    std::string url;
    std::string download_url;
    std::string content_type;
    int width = 0;
    int height = 0;
    uint64_t size = 0;
};

struct SdkCaptureStageResult {
    std::string name;
    std::string status = "queued";
    std::vector<std::string> input_assets;
    std::vector<std::string> output_assets;
    std::string provider;
    std::string message = "queued";
};

struct SdkPageProcessRequest {
    std::string device_id;
    std::string input_path;
    std::string laser_path;
    std::string output_dir;
    std::string output_path;
    std::string page_processing;
    int width = 0;
    int height = 0;
};

struct SdkPageOutput {
    std::string output_id;
    std::string role;
    int index = 0;
    std::string path;
    std::string content_type = "image/jpeg";
    int width = 0;
    int height = 0;
    uint64_t size = 0;
};

struct SdkPageProcessResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool processed = false;
    bool unsupported = false;
    std::string output_path;
    std::vector<SdkPageOutput> outputs;
};

struct SdkColorModeRequest {
    std::string input_path;
    std::string output_path;
    std::string color_mode;
};

struct SdkColorModeResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool processed = false;
    bool unsupported = false;
    std::string output_path;
};

struct SdkFormatConvertRequest {
    std::string input_path;
    std::string output_path;
    std::string output_format;
};

struct SdkFormatConvertResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool converted = false;
    bool passthrough = false;
    std::string output_path;
};

struct SdkThumbnailRequest {
    std::string input_path;
    std::string output_path;
    std::string thumbnail_kind;
    int max_width = 320;
    int max_height = 320;
    bool keep_aspect_ratio = true;
};

struct SdkThumbnailResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool generated = false;
    std::string output_path;
    int width = 0;
    int height = 0;
    uint64_t size = 0;
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
