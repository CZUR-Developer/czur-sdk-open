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
    std::string pixel_format = "mjpeg";
    bool is_default = false;
};

struct SdkOutputTargetSizeOption {
    int target_size = 0;
    int width = 0;
    int height = 0;
    bool is_device_default = false;
};

struct SdkCaptureOutputCapabilities {
    bool target_size_supported = false;
    std::vector<SdkOutputTargetSizeOption> target_sizes;
};

inline bool ResolveSdkOutputTargetSize(int target_size, int* width, int* height) {
    int resolved_width = 0;
    int resolved_height = 0;
    switch (target_size) {
        case 10:
            resolved_width = 320;
            resolved_height = 240;
            break;
        case 30:
            resolved_width = 640;
            resolved_height = 480;
            break;
        case 50:
            resolved_width = 800;
            resolved_height = 600;
            break;
        case 100:
            resolved_width = 1160;
            resolved_height = 870;
            break;
        case 180:
            resolved_width = 1536;
            resolved_height = 1152;
            break;
        case 200:
            resolved_width = 1600;
            resolved_height = 1200;
            break;
        case 300:
            resolved_width = 2048;
            resolved_height = 1536;
            break;
        case 400:
            resolved_width = 2304;
            resolved_height = 1728;
            break;
        case 500:
            resolved_width = 2592;
            resolved_height = 1944;
            break;
        case 600:
            resolved_width = 3072;
            resolved_height = 1728;
            break;
        case 800:
            resolved_width = 3264;
            resolved_height = 2448;
            break;
        case 1000:
            resolved_width = 3648;
            resolved_height = 2736;
            break;
        case 1001:
            resolved_width = 3672;
            resolved_height = 2754;
            break;
        case 1002:
            resolved_width = 3840;
            resolved_height = 2160;
            break;
        case 1200:
        case 1201:
            resolved_width = 4032;
            resolved_height = 3024;
            break;
        case 1202:
            resolved_width = 4000;
            resolved_height = 3000;
            break;
        case 1300:
            resolved_width = 4160;
            resolved_height = 3120;
            break;
        case 1400:
            resolved_width = 4320;
            resolved_height = 3240;
            break;
        case 1500:
            resolved_width = 4480;
            resolved_height = 3360;
            break;
        case 1600:
            resolved_width = 4608;
            resolved_height = 3456;
            break;
        case 1800:
            resolved_width = 4896;
            resolved_height = 3672;
            break;
        case 2000:
            resolved_width = 5248;
            resolved_height = 3936;
            break;
        case 2200:
            resolved_width = 5440;
            resolved_height = 4080;
            break;
        case 2400:
            resolved_width = 5696;
            resolved_height = 4272;
            break;
        case 2500:
            resolved_width = 5824;
            resolved_height = 4368;
            break;
        case 2600:
            resolved_width = 5888;
            resolved_height = 4416;
            break;
        case 2800:
            resolved_width = 6144;
            resolved_height = 4608;
            break;
        case 3200:
            resolved_width = 6528;
            resolved_height = 4896;
            break;
        case 3800:
            resolved_width = 7168;
            resolved_height = 5376;
            break;
        case 4000:
            resolved_width = 7296;
            resolved_height = 5472;
            break;
        case 4800:
            resolved_width = 7936;
            resolved_height = 5952;
            break;
        default:
            return false;
    }
    if (width != nullptr) {
        *width = resolved_width;
    }
    if (height != nullptr) {
        *height = resolved_height;
    }
    return true;
}

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
    SdkCaptureOutputCapabilities capture_output;
};

struct SdkDeviceOpenRequest {
    std::string device_id;
    int width = 0;
    int height = 0;
    int fps = 0;
    std::string pixel_format = "mjpeg";
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
    // 内部原图传递字段：provider 只把硬拍原始图交给 capture task，
    // 对外 websocket 仍通过 task assets 暴露最终路径，避免临时文件协议外泄。
    std::vector<uint8_t> raw_payload;
    std::vector<uint8_t> raw_laser_payload;
    std::string output_path;
    std::string original_path;
    std::string laser_path;
    int width = 0;
    int height = 0;
    int dpi = 0;
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
    std::string pixel_format = "mjpeg";
    std::string page_processing;
    bool single_page_realtime_detect_rects = false;
    bool single_page_multi_target_paging = false;
};

struct SdkVideoStartResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool accepted = false;
    std::string pixel_format = "mjpeg";
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
    std::string pixel_format = "mjpeg";
    std::vector<uint8_t> payload;
    std::vector<SdkRect4P> detected_rects;
    int detected_rects_source_width = 0;
    int detected_rects_source_height = 0;
};

using SdkVideoFrameCallback = std::function<void(const SdkVideoFrame&)>;

enum class SdkDeviceActionType {
    PageTurn,
    HardGrab,
};

struct SdkDeviceActionEvent {
    std::string device_id;
    SdkDeviceActionType type = SdkDeviceActionType::PageTurn;
    bool auto_capture = false;
    int64_t timestamp_ms = 0;
    // 硬拍事件携带已采集的原始图；翻页事件不使用该字段。
    SdkCaptureResult capture;
};

using SdkDeviceActionEventCallback = std::function<void(const SdkDeviceActionEvent&)>;

struct SdkDeviceEvent {
    std::string device_id;
    std::string type = "removed";
    std::string reason;
    bool was_opened = false;
    bool was_streaming = false;
    int64_t timestamp_ms = 0;
};

using SdkDeviceEventCallback = std::function<void(const SdkDeviceEvent&)>;

struct SdkTurnDetectRequest {
    std::string device_id;
    bool enabled = false;
    bool auto_capture = false;
    int scan_device_type = -1;
    int cooldown_ms = 1000;
};

struct SdkTurnDetectResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool applied = false;
    bool enabled = false;
    bool auto_capture = false;
    int scan_device_type = -1;
    int cooldown_ms = 1000;
};

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
    int dpi = 0;
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
    int output_quality = 0;
    int output_target_size = 0;
    int output_target_width = 0;
    int output_target_height = 0;
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
    int dpi = 0;
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
    int dpi = 0;
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
    int dpi = 0;
};

struct SdkColorModeResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool processed = false;
    bool unsupported = false;
    std::string output_path;
    int width = 0;
    int height = 0;
    uint64_t size = 0;
};

struct SdkFormatConvertRequest {
    std::string input_path;
    std::string output_path;
    std::string output_format;
    int quality = 0;
    float scale = 1.0f;
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
    std::vector<SdkCaptureAsset> assets;
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

struct SdkSaneStatusResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool available = false;
    std::string platform;
    std::vector<std::string> supported_platforms;
    int sane_major = 0;
    int sane_minor = 0;
    std::string sane_version;
    std::string reason;
};

struct SdkSaneDevice {
    std::string device_id;
    std::string device_name;
    std::string vendor;
    std::string model;
    std::string type;
    std::string backend;
    std::string status = "online";
    std::string discovery_source;
    bool openable = true;
};

struct SdkSaneListRequest {
    bool refresh = true;
    bool include_detected = false;
};

struct SdkSaneListResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    int generation = 0;
    std::vector<SdkSaneDevice> devices;
    std::vector<SdkSaneDevice> detected_devices;
};

struct SdkSaneWatchRequest {
    std::string connection_id;
    bool enabled = true;
};

struct SdkSaneWatchResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool watching = false;
    int generation = 0;
};

struct SdkSaneDeviceEvent {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    std::string connection_id;
    std::string event_name;
    int generation = 0;
    std::vector<SdkSaneDevice> devices;
    std::vector<SdkSaneDevice> detected_devices;
    std::vector<SdkSaneDevice> added_devices;
    std::vector<SdkSaneDevice> removed_devices;
};

using SdkSaneDeviceEventCallback = std::function<void(const SdkSaneDeviceEvent&)>;

struct SdkSaneOpenRequest {
    std::string device_id;
    std::string device_name;
    std::string profile_id;
    std::vector<std::string> option_keys;
    std::vector<std::string> option_values_json;
};

struct SdkSaneOpenResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool opened = false;
    std::string session_id;
    SdkSaneDevice device;
};

struct SdkSaneCloseRequest {
    std::string session_id;
};

struct SdkSaneCloseResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool closed = false;
    bool was_opened = false;
};

struct SdkSaneOptionConstraint {
    std::string type = "none";
    double min = 0.0;
    double max = 0.0;
    double quant = 0.0;
    std::vector<std::string> values_json;
};

struct SdkSaneOption {
    int index = -1;
    std::string name;
    std::string title;
    std::string description;
    std::string group;
    std::string type;
    std::string unit;
    std::string value_json;
    SdkSaneOptionConstraint constraint;
    bool readonly = false;
    bool settable = true;
    bool automatic = false;
    bool inactive = false;
    bool advanced = false;
    bool requires_reload = false;
};

struct SdkSaneGetOptionsRequest {
    std::string session_id;
};

struct SdkSaneGetOptionsResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    std::vector<SdkSaneOption> options;
};

struct SdkSaneOptionSetItem {
    std::string key;
    int index = -1;
    std::string value_json;
};

struct SdkSaneSetOptionsRequest {
    std::string session_id;
    std::vector<SdkSaneOptionSetItem> options;
};

struct SdkSaneOptionSetResultItem {
    std::string key;
    int index = -1;
    std::string status;
    std::string message;
    std::string value_json;
    bool inexact = false;
    bool requires_reload = false;
};

struct SdkSaneSetOptionsResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool applied = false;
    bool requires_reload = false;
    std::vector<SdkSaneOptionSetResultItem> results;
};

struct SdkSaneProfile {
    std::string profile_id;
    std::string device_key;
    std::string name;
    std::vector<SdkSaneOptionSetItem> options;
    std::string created_at;
    std::string updated_at;
};

struct SdkSaneProfileRequest {
    std::string session_id;
    std::string device_id;
    std::string device_name;
    std::string device_key;
    std::string profile_id;
    std::string name;
    std::vector<SdkSaneOptionSetItem> options;
};

struct SdkSaneProfileListResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    std::vector<SdkSaneProfile> profiles;
};

struct SdkSaneProfileResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool applied = false;
    bool saved = false;
    bool deleted = false;
    SdkSaneProfile profile;
};

struct SdkSaneScanRequest {
    std::string connection_id;
    std::string session_id;
    std::vector<SdkSaneOptionSetItem> options;
    std::string output_type = "images";
    std::string output_format = "jpg";
    std::string output_path;
    std::string output_dir;
    std::string export_type = "multi-page";
};

struct SdkSaneScanTask {
    std::string task_id;
    std::string connection_id;
    std::string session_id;
    std::string status = "queued";
    std::string phase = "queued";
    int progress = 0;
    int page_count = 0;
    int current_page = 0;
    std::string output_type = "images";
    std::string output_format = "jpg";
    std::string output_dir;
    std::string export_type = "multi-page";
    std::string output_path;
    std::vector<std::string> output_paths;
    std::string last_page_path;
    std::vector<SdkCaptureAsset> assets;
    std::string message = "queued";
    std::string error;
    std::string started_at;
    std::string updated_at;
    bool cancel_requested = false;
};

struct SdkSaneScanResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool accepted = false;
    std::string task_id;
    SdkSaneScanTask task;
};

struct SdkSaneScanTaskEvent {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    std::string connection_id;
    std::string event_name = "sane.scan_changed";
    SdkSaneScanTask task;
};

using SdkSaneScanTaskEventCallback = std::function<void(const SdkSaneScanTaskEvent&)>;

struct SdkSaneScanGetRequest {
    std::string task_id;
};

struct SdkSaneScanCancelRequest {
    std::string task_id;
};

struct SdkTwainStatusResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool available = false;
    std::string platform;
    std::vector<std::string> supported_platforms;
    bool dsm_loaded = false;
    std::string dsm_path;
    std::string twain_version;
    std::string reason;
};

struct SdkTwainSource {
    std::string source_id;
    std::string source_name;
    std::string manufacturer;
    std::string product_family;
    std::string product_name;
    int protocol_major = 0;
    int protocol_minor = 0;
    std::string status = "online";
    bool openable = true;
};

struct SdkTwainListRequest {
    bool refresh = true;
};

struct SdkTwainListResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    int generation = 0;
    std::vector<SdkTwainSource> sources;
};

struct SdkTwainWatchRequest {
    std::string connection_id;
    bool enabled = true;
};

struct SdkTwainWatchResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool watching = false;
    int generation = 0;
};

struct SdkTwainSourceEvent {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    std::string connection_id;
    std::string event_name = "twain.source_snapshot";
    int generation = 0;
    std::vector<SdkTwainSource> sources;
    std::vector<SdkTwainSource> added_sources;
    std::vector<SdkTwainSource> removed_sources;
};

using SdkTwainSourceEventCallback = std::function<void(const SdkTwainSourceEvent&)>;

struct SdkTwainOpenRequest {
    std::string source_id;
};

struct SdkTwainOpenResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool opened = false;
    std::string session_id;
    SdkTwainSource source;
};

struct SdkTwainCloseRequest {
    std::string session_id;
};

struct SdkTwainCloseResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool closed = false;
    bool was_opened = false;
};

struct SdkTwainCapabilityConstraint {
    std::string type = "none";
    double min = 0.0;
    double max = 0.0;
    double quant = 0.0;
    std::vector<std::string> values_json;
};

struct SdkTwainCapability {
    std::string cap;
    int cap_id = 0;
    std::string name;
    std::string title;
    std::string type;
    std::string value_json;
    SdkTwainCapabilityConstraint constraint;
    bool readonly = false;
    bool settable = true;
    bool advanced = false;
};

struct SdkTwainGetCapabilitiesRequest {
    std::string session_id;
};

struct SdkTwainGetCapabilitiesResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    std::vector<SdkTwainCapability> capabilities;
};

struct SdkTwainCapabilitySetItem {
    std::string cap;
    std::string name;
    std::string value_json;
};

struct SdkTwainSetCapabilitiesRequest {
    std::string session_id;
    std::vector<SdkTwainCapabilitySetItem> capabilities;
};

struct SdkTwainCapabilitySetResultItem {
    std::string cap;
    std::string name;
    std::string status;
    std::string message;
    std::string value_json;
};

struct SdkTwainSetCapabilitiesResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool applied = false;
    bool requires_reload = false;
    std::vector<SdkTwainCapabilitySetResultItem> results;
};

struct SdkTwainProfile {
    std::string profile_id;
    std::string source_key;
    std::string name;
    std::vector<SdkTwainCapabilitySetItem> capabilities;
    std::string created_at;
    std::string updated_at;
};

struct SdkTwainProfileRequest {
    std::string session_id;
    std::string source_id;
    std::string source_key;
    std::string profile_id;
    std::string name;
    std::vector<SdkTwainCapabilitySetItem> capabilities;
};

struct SdkTwainProfileListResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    std::vector<SdkTwainProfile> profiles;
};

struct SdkTwainProfileResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool applied = false;
    bool saved = false;
    bool deleted = false;
    SdkTwainProfile profile;
};

struct SdkTwainScanRequest {
    std::string connection_id;
    std::string session_id;
    bool show_ui = false;
    std::string transfer_mechanism = "native";
    std::vector<SdkTwainCapabilitySetItem> capabilities;
    std::string output_type = "images";
    std::string output_format;
    std::string output_path;
    std::string output_dir;
    std::string export_type = "multi-page";
};

struct SdkTwainScanTask {
    std::string task_id;
    std::string connection_id;
    std::string session_id;
    std::string status = "queued";
    std::string phase = "queued";
    int progress = 0;
    int page_count = 0;
    int current_page = 0;
    std::string output_type = "images";
    std::string output_format;
    std::string output_dir;
    std::string export_type = "multi-page";
    std::string output_path;
    std::vector<std::string> output_paths;
    std::string last_page_path;
    std::vector<SdkCaptureAsset> assets;
    std::string message = "queued";
    std::string error;
    std::string started_at;
    std::string updated_at;
    bool cancel_requested = false;
    bool ui_required = false;
    std::string transfer_mechanism = "native";
};

struct SdkTwainScanResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool accepted = false;
    std::string task_id;
    SdkTwainScanTask task;
};

struct SdkTwainScanTaskEvent {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    std::string connection_id;
    std::string event_name = "twain.scan_changed";
    SdkTwainScanTask task;
};

using SdkTwainScanTaskEventCallback = std::function<void(const SdkTwainScanTaskEvent&)>;

struct SdkTwainScanGetRequest {
    std::string task_id;
};

struct SdkTwainScanCancelRequest {
    std::string task_id;
};

struct SdkImageEnhanceCapability {
    std::string type;
    std::string title;
    std::string description;
    std::string i18n_key;
    std::string title_zh_cn;
    std::string description_zh_cn;
    std::string category;
    std::string runtime = "offline";
    bool available = true;
    std::string unavailable_reason;
    std::string unavailable_reason_zh_cn;
    std::string requires_capability;
    std::string quota_unit = "page";
    std::vector<std::string> source_types;
    int min_pages = 1;
    int max_pages = 1000;
    std::string page_effect = "transform";
    bool metadata = false;
    int order_hint = 0;
    std::string version = "1.0";
    std::string defaults_json = "{}";
    std::string schema_json = "{}";
};

struct SdkImageEnhanceCapabilityResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    std::string provider;
    std::string kind = "offline";
    bool available = true;
    std::vector<SdkImageEnhanceCapability> capabilities;
};

struct SdkImageEnhancePage {
    int source_index = 0;
    int output_index = 0;
    std::string path;
    bool dropped = false;
    std::string metadata_json = "{}";
};

struct SdkImageEnhanceStep {
    std::string id;
    std::string type;
    std::string provider = "auto";
    bool enabled = true;
    std::string on_error = "fail";
    std::string params_json = "{}";
};

struct SdkImageEnhanceTarget {
    std::string type = "images";
    std::string format = "jpg";
    std::string export_type = "single-page";
    std::string output_path;
    std::string output_dir;
    int quality = 90;
    std::string tiff_color = "color";
    std::string tiff_compression = "lzw";
};

struct SdkImageEnhancePipeline {
    std::string version = "image.enhance.pipeline.v1";
    std::vector<SdkImageEnhanceStep> steps;
    SdkImageEnhanceTarget target;
    bool keep_intermediate = false;
    bool include_metadata = true;
};

struct SdkImageEnhanceStepRequest {
    std::string task_id;
    SdkImageEnhanceStep step;
    std::vector<SdkImageEnhancePage> pages;
    std::string output_dir;
    std::string online_api_key;
    std::string online_base_url;
};

struct SdkImageEnhanceStepResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool processed = false;
    std::vector<SdkImageEnhancePage> pages;
    std::string metadata_json = "{}";
    std::vector<std::string> warnings;
};

struct SdkImageEnhanceStepSnapshot {
    std::string id;
    std::string type;
    std::string status = "queued";
    std::string provider;
    int input_page_count = 0;
    int output_page_count = 0;
    std::string metadata_json = "{}";
    std::vector<std::string> warnings;
    std::string message = "queued";
};

struct SdkImageEnhanceTaskSnapshot {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    std::string task_id;
    std::string connection_id;
    std::string status = "queued";
    std::string phase = "queued";
    int progress = 0;
    int input_page_count = 0;
    int output_page_count = 0;
    std::vector<SdkImageEnhancePage> pages;
    std::vector<SdkImageEnhanceStepSnapshot> steps;
    std::vector<SdkCaptureAsset> assets;
    std::vector<std::string> warnings;
    std::string output_path;
    std::vector<std::string> output_paths;
    std::string output_type = "images";
    std::string output_format = "jpg";
    std::string export_type = "single-page";
    std::string error;
    bool cancel_requested = false;
};

struct SdkImageEnhanceTaskRequest {
    std::string connection_id;
    std::vector<std::string> input_paths;
    std::string output_dir;
    SdkImageEnhancePipeline pipeline;
    std::string online_api_key;
    std::string online_session_token;
    std::string online_base_url;
    std::string authz_base_url;
};

struct SdkImageEnhanceTaskResult {
    int code = ToCode(SdkStatusCode::Ok);
    std::string message = "ok";
    bool accepted = false;
    std::string task_id;
    SdkImageEnhanceTaskSnapshot task;
};

struct SdkImageEnhanceGetRequest {
    std::string task_id;
};

struct SdkImageEnhanceCancelRequest {
    std::string task_id;
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
