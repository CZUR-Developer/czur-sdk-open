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

struct SdkPoint2f {
    float x = 0.0f;
    float y = 0.0f;
};

struct SdkRect4P {
    SdkPoint2f left_top;
    SdkPoint2f right_top;
    SdkPoint2f right_down;
    SdkPoint2f left_down;
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
    std::vector<SdkRect4P> detected_rects;
    int detected_rects_source_width = 0;
    int detected_rects_source_height = 0;
    int scan_device_type = 0;
};

using SdkCaptureCallback = std::function<void(const SdkCaptureResult&)>;

struct SdkVideoStartRequest {
    std::string device_id;
    std::string stream_id;
    int width = 0;
    int height = 0;
    int fps = 0;
    std::string pixel_format = "bgr24";
    std::string page_processing;
    bool single_page_realtime_detect_rects = false;
    bool single_page_multi_target_paging = false;
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
    std::vector<SdkRect4P> detected_rects;
    int detected_rects_source_width = 0;
    int detected_rects_source_height = 0;
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

struct SdkVideoProfileRequest {
    std::string device_id;
    std::string page_processing;
    bool single_page_realtime_detect_rects = false;
    bool single_page_multi_target_paging = false;
};

struct SdkVideoProfileResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool applied = false;
    std::string page_processing;
    bool single_page_realtime_detect_rects = false;
    bool single_page_multi_target_paging = false;
};

struct SdkCropBorderOptions {
    bool enabled = false;
    int width = 0;
    int height = 0;
};

struct SdkSinglePageOptions {
    bool realtime_detect_rects = false;
    SdkCropBorderOptions crop_border;
    bool id_card_round_corner = false;
    bool auto_rotate = false;
    bool smart_black_edge_optimize = true;
    bool multi_target_paging = false;
};

struct SdkCurvedBookOptions {
    bool remove_finger = true;
    std::string finger_type = "with_sleeve";
    bool smart_paging = true;
    SdkCropBorderOptions crop_border;
    bool auto_complete = false;
};

struct SdkImageProcessRequest {
    std::string input_upload_id;
    std::string input_path;
    std::string output_path;
    std::string output_dir;
    std::string page_processing = "keep_original";
    std::string color_mode = "no_optimize";
    std::string output_format;
    SdkSinglePageOptions single_page;
    SdkCurvedBookOptions curved_book;
    SdkRect4P selected_area_rect;
    int selected_area_source_width = 0;
    int selected_area_source_height = 0;
    int scan_device_type = 0;
};

struct SdkImageProcessOutput {
    std::string asset_id;
    std::string output_id;
    std::string role;
    int index = 0;
    std::string path;
    std::string url;
    std::string download_url;
    std::string content_type = "image/jpeg";
    int width = 0;
    int height = 0;
    uint64_t size = 0;
};

struct SdkImageProcessResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool processed = false;
    std::string output_path;
    std::vector<SdkImageProcessOutput> outputs;
};

struct SdkCaptureProfile {
    std::string profile_version = "capture.profile.v1";
    int revision = 1;
    std::string device_id;
    int width = 0;
    int height = 0;
    int fps = 0;
    std::string page_processing = "single_page";
    bool single_page_realtime_detect_rects = false;
    SdkSinglePageOptions single_page;
    SdkCurvedBookOptions curved_book;
    SdkRect4P selected_area_rect;
    int selected_area_source_width = 0;
    int selected_area_source_height = 0;
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
    bool single_page_realtime_detect_rects = false;
    SdkSinglePageOptions single_page;
    SdkCurvedBookOptions curved_book;
    std::vector<SdkRect4P> detected_rects;
    int detected_rects_source_width = 0;
    int detected_rects_source_height = 0;
    SdkRect4P selected_area_rect;
    int selected_area_source_width = 0;
    int selected_area_source_height = 0;
    int scan_device_type = 0;
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
    std::vector<std::string> input_upload_ids;
    std::vector<std::string> input_files;
    std::string output_path;
    std::string output_dir;
    std::string format = "docx";
    std::string export_type = "multi-page";
    std::string ext_params_json;
};

struct SdkOcrTaskSnapshot {
    std::string task_id;
    std::string status = "queued";
    int progress = 0;
    std::string output_path;
    std::vector<std::string> output_paths;
    std::string format;
    std::string export_type = "multi-page";
    std::string message = "queued";
    std::string error;
};

struct SdkOcrRecognizeResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    std::string task_id;
    SdkOcrTaskSnapshot task;
};

struct SdkOcrGetRequest {
    std::string task_id;
};

struct SdkOcrGetResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    SdkOcrTaskSnapshot task;
};

struct SdkOcrCancelRequest {
    std::string task_id;
};

struct SdkOcrCancelResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool cancelled = false;
    SdkOcrTaskSnapshot task;
};

struct SdkOcrTextBlock {
    std::string text;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float confidence = 0.0f;
    float font_size = 0.0f;
};

struct SdkOcrExtractTextRequest {
    std::string input_upload_id;
    std::string input_path;
};

struct SdkOcrExtractTextResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool recognized = false;
    std::string input_path;
    int width = 0;
    int height = 0;
    std::vector<SdkOcrTextBlock> blocks;
};

struct SdkBarcodeResult {
    std::string format_name;
    int format = 0;
    std::string text;
    std::vector<SdkPoint2f> points;
};

struct SdkBarcodeDetectRequest {
    std::string input_upload_id;
    std::string input_path;
    std::vector<std::string> formats;
};

struct SdkBarcodeDetectResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool detected = false;
    std::string input_path;
    int width = 0;
    int height = 0;
    std::vector<SdkBarcodeResult> barcodes;
};

struct SdkFileConvertRequest {
    std::string input_upload_id;
    std::vector<std::string> input_upload_ids;
    std::string input_path;
    std::vector<std::string> input_paths;
    std::string output_path;
    std::string output_dir;
    std::string output_format;
    std::string source_type = "image";
    std::string source_format;
    std::string target_type;
    std::string export_type = "multi-page";
    std::string pages = "all";
    int quality = 90;
    int render_dpi = 144;
    std::string tiff_color = "color";
    std::string tiff_compression = "lzw";
};

struct SdkFileConvertResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    int accepted = 0;
    int converted = 0;
    std::string output_path;
    std::vector<std::string> output_paths;
    std::string source_format;
    int source_page_count = 0;
    int selected_page_count = 0;
};

} // namespace sdk
} // namespace editor
