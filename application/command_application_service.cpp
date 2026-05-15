// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "command_application_service.h"

#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>
#include <utility>

#include "sdk_runtime_paths.h"

namespace editor {
namespace sdk {

namespace {

std::string GetOptionalStringField(const Json& obj, const char* key) {
    const auto it = obj.find(key);
    if (it != obj.end() && it->is_string()) {
        return it->get<std::string>();
    }
    return "";
}

Json GetOptionalObjectField(const Json& obj, const char* key) {
    const auto it = obj.find(key);
    if (it != obj.end() && it->is_object()) {
        return *it;
    }
    return Json::object();
}

std::vector<std::string> GetOptionalStringArrayField(const Json& obj, const char* key) {
    std::vector<std::string> values;
    const auto it = obj.find(key);
    if (it == obj.end() || !it->is_array()) {
        return values;
    }
    for (Json::const_iterator value_it = it->begin(); value_it != it->end(); ++value_it) {
        if (value_it->is_string()) {
            values.push_back(value_it->get<std::string>());
        }
    }
    return values;
}

bool FileExists(const std::string& path) {
    struct stat st;
    return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

uint64_t FileSize(const std::string& path) {
    struct stat st;
    if (path.empty() || ::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        return 0;
    }
    return static_cast<uint64_t>(st.st_size);
}

std::string JoinLocalPath(const std::string& dir, const std::string& name) {
    if (dir.empty() || dir == ".") {
        return name;
    }
    if (dir[dir.size() - 1] == '/') {
        return dir + name;
    }
    return dir + "/" + name;
}

std::string BuildDefaultAssetBaseUrl(const SdkConfig& config) {
    if (!config.asset_base_url.empty()) {
        return config.asset_base_url;
    }
    const std::string host = config.bind_host == "0.0.0.0" ? "127.0.0.1" : config.bind_host;
    return "http://" + host + ":" + std::to_string(config.asset_http_port);
}

std::string NormalizeLower(std::string value) {
    for (std::string::iterator it = value.begin(); it != value.end(); ++it) {
        *it = static_cast<char>(std::tolower(static_cast<unsigned char>(*it)));
    }
    return value;
}

std::string ExtensionFromFilename(const std::string& filename) {
    const std::string::size_type slash_pos = filename.find_last_of("/\\");
    const std::string leaf = slash_pos == std::string::npos ? filename : filename.substr(slash_pos + 1);
    const std::string::size_type dot_pos = leaf.find_last_of('.');
    if (dot_pos == std::string::npos || dot_pos + 1 >= leaf.size()) {
        return "";
    }
    return NormalizeLower(leaf.substr(dot_pos + 1));
}

std::string ImageExtensionForContentType(const std::string& content_type, const std::string& filename) {
    const std::string type = NormalizeLower(content_type);
    const std::string extension = ExtensionFromFilename(filename);
    if (type.find("png") != std::string::npos || extension == "png") {
        return "png";
    }
    if (type.find("bmp") != std::string::npos || extension == "bmp") {
        return "bmp";
    }
    if (type.find("tiff") != std::string::npos || extension == "tif" || extension == "tiff") {
        return "tiff";
    }
    if (type.find("webp") != std::string::npos || extension == "webp") {
        return "webp";
    }
    return "jpg";
}

bool IsSupportedImageExtension(const std::string& extension) {
    const std::string value = NormalizeLower(extension);
    return value == "jpg" || value == "jpeg" || value == "png" || value == "bmp" ||
           value == "tif" || value == "tiff" || value == "webp";
}

std::string ContentTypeForImageExtension(const std::string& extension) {
    const std::string value = NormalizeLower(extension);
    if (value == "png") {
        return "image/png";
    }
    if (value == "bmp") {
        return "image/bmp";
    }
    if (value == "tif" || value == "tiff") {
        return "image/tiff";
    }
    if (value == "webp") {
        return "image/webp";
    }
    return "image/jpeg";
}

std::string ContentTypeForOutputFormat(const std::string& format) {
    const std::string value = NormalizeLower(format);
    if (value == "png") {
        return "image/png";
    }
    if (value == "tif" || value == "tiff") {
        return "image/tiff";
    }
    return "image/jpeg";
}

bool WriteBinaryFile(const std::string& path, const std::string& content) {
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    return static_cast<bool>(out);
}

int GetOptionalIntField(const Json& obj, const char* key, int default_value) {
    const auto it = obj.find(key);
    if (it != obj.end() && it->is_number_integer()) {
        return it->get<int>();
    }
    return default_value;
}

bool GetOptionalBoolField(const Json& obj, const char* key, bool default_value) {
    const auto it = obj.find(key);
    if (it != obj.end() && it->is_boolean()) {
        return it->get<bool>();
    }
    return default_value;
}

float GetOptionalFloatField(const Json& obj, const char* key, float default_value) {
    const auto it = obj.find(key);
    if (it != obj.end() && it->is_number()) {
        return it->get<float>();
    }
    return default_value;
}

int ClampCropMargin(int value) {
    if (value < -100) {
        return -100;
    }
    if (value > 100) {
        return 100;
    }
    return value;
}

std::string GetOptionalStringAnyField(const Json& obj,
                                      const std::vector<const char*>& keys,
                                      const std::string& default_value) {
    for (std::vector<const char*>::const_iterator it = keys.begin(); it != keys.end(); ++it) {
        const std::string value = GetOptionalStringField(obj, *it);
        if (!value.empty()) {
            return value;
        }
    }
    return default_value;
}

int GetOptionalIntAnyField(const Json& obj, const std::vector<const char*>& keys, int default_value) {
    for (std::vector<const char*>::const_iterator it = keys.begin(); it != keys.end(); ++it) {
        const auto json_it = obj.find(*it);
        if (json_it != obj.end() && json_it->is_number_integer()) {
            return json_it->get<int>();
        }
    }
    return default_value;
}

bool GetOptionalBoolAnyField(const Json& obj, const std::vector<const char*>& keys, bool default_value) {
    for (std::vector<const char*>::const_iterator it = keys.begin(); it != keys.end(); ++it) {
        const auto json_it = obj.find(*it);
        if (json_it != obj.end() && json_it->is_boolean()) {
            return json_it->get<bool>();
        }
    }
    return default_value;
}

SdkCropBorderOptions ParseCropBorderOptions(const Json& obj,
                                            bool default_enabled = false,
                                            int default_width = 0,
                                            int default_height = 0) {
    SdkCropBorderOptions options;
    options.enabled = default_enabled;
    options.width = default_width;
    options.height = default_height;
    const Json crop_json = GetOptionalObjectField(obj, "crop_border");
    if (!crop_json.empty()) {
        options.enabled = GetOptionalBoolAnyField(crop_json, {"enabled", "crop"}, options.enabled);
        options.width = GetOptionalIntAnyField(crop_json, {"width", "widthCutMargin"}, options.width);
        options.height = GetOptionalIntAnyField(crop_json, {"height", "heightCutMargin"}, options.height);
    }
    const Json crop_adjust_json = GetOptionalObjectField(obj, "cropAdjust");
    if (!crop_adjust_json.empty()) {
        options.enabled = GetOptionalBoolField(crop_adjust_json, "crop", options.enabled);
        const Json param_json = GetOptionalObjectField(crop_adjust_json, "param");
        options.width = GetOptionalIntField(param_json, "width", options.width);
        options.height = GetOptionalIntField(param_json, "height", options.height);
    }
    options.enabled = GetOptionalBoolAnyField(obj, {"cropBorder", "crop_border_enabled"}, options.enabled);
    options.width = GetOptionalIntAnyField(obj, {"cropBorderWidth", "crop_border_width"}, options.width);
    options.height = GetOptionalIntAnyField(obj, {"cropBorderHeight", "crop_border_height"}, options.height);
    options.width = ClampCropMargin(options.width);
    options.height = ClampCropMargin(options.height);
    if (!options.enabled) {
        options.width = 0;
        options.height = 0;
    }
    return options;
}

SdkSinglePageOptions ParseSinglePageOptions(const Json& obj, const SdkSinglePageOptions& defaults) {
    SdkSinglePageOptions options = defaults;
    options.realtime_detect_rects =
        GetOptionalBoolField(obj, "realtime_detect_rects", options.realtime_detect_rects);
    options.crop_border = ParseCropBorderOptions(obj,
                                                 options.crop_border.enabled,
                                                 options.crop_border.width,
                                                 options.crop_border.height);
    options.id_card_round_corner =
        GetOptionalBoolAnyField(obj, {"id_card_round_corner", "idCardRoundCorner", "roundedCorner"}, options.id_card_round_corner);
    options.auto_rotate =
        GetOptionalBoolAnyField(obj, {"auto_rotate", "autoRotate", "automaticConversion"}, options.auto_rotate);
    options.smart_black_edge_optimize =
        GetOptionalBoolAnyField(obj, {"smart_black_edge_optimize", "smartBlackEdgeOptimize", "optiBlackEdges"}, options.smart_black_edge_optimize);
    options.multi_target_paging =
        GetOptionalBoolAnyField(obj, {"multi_target_paging", "multiTargetPaging", "multiTagets"}, options.multi_target_paging);
    return options;
}

SdkCurvedBookOptions ParseCurvedBookOptions(const Json& obj, const SdkCurvedBookOptions& defaults) {
    SdkCurvedBookOptions options = defaults;
    const Json remove_finger_json = GetOptionalObjectField(obj, "remove_finger");
    if (!remove_finger_json.empty()) {
        options.remove_finger = GetOptionalBoolAnyField(remove_finger_json, {"enabled", "remove"}, options.remove_finger);
        options.finger_type = GetOptionalStringAnyField(remove_finger_json, {"finger_type", "type"}, options.finger_type);
    }
    const Json legacy_remove_finger_json = GetOptionalObjectField(obj, "removeFinger");
    if (!legacy_remove_finger_json.empty()) {
        options.remove_finger = GetOptionalBoolField(legacy_remove_finger_json, "remove", options.remove_finger);
        if (legacy_remove_finger_json.find("type") != legacy_remove_finger_json.end()) {
            const int type = GetOptionalIntField(legacy_remove_finger_json, "type", options.finger_type == "with_sleeve" ? 1 : 0);
            options.finger_type = type == 0 ? "without_sleeve" : "with_sleeve";
        }
    }
    options.remove_finger = GetOptionalBoolAnyField(obj, {"removeFingerEnabled", "removeFinger"}, options.remove_finger);
    const bool with_sleeve = GetOptionalBoolField(obj, "withFingerSleeve", options.finger_type != "without_sleeve");
    options.finger_type = with_sleeve ? "with_sleeve" : "without_sleeve";
    const std::string explicit_finger_type = GetOptionalStringField(obj, "finger_type");
    if (!explicit_finger_type.empty()) {
        options.finger_type = explicit_finger_type;
    }
    options.smart_paging = GetOptionalBoolAnyField(obj, {"smart_paging", "smartPaging"}, options.smart_paging);
    const Json smart_split_json = GetOptionalObjectField(obj, "smartSplit");
    if (!smart_split_json.empty()) {
        options.smart_paging = GetOptionalBoolField(smart_split_json, "split", options.smart_paging);
    }
    options.crop_border = ParseCropBorderOptions(obj,
                                                 options.crop_border.enabled,
                                                 options.crop_border.width,
                                                 options.crop_border.height);
    options.auto_complete = GetOptionalBoolAnyField(obj, {"auto_complete", "autoComplete"}, options.auto_complete);
    if (options.finger_type != "without_sleeve") {
        options.finger_type = "with_sleeve";
    }
    return options;
}

SdkPoint2f ParsePoint(const Json& obj) {
    SdkPoint2f point;
    point.x = GetOptionalFloatField(obj, "x", 0.0f);
    point.y = GetOptionalFloatField(obj, "y", 0.0f);
    return point;
}

SdkRect4P ParseRect4P(const Json& obj) {
    SdkRect4P rect;
    rect.left_top = ParsePoint(GetOptionalObjectField(obj, "left_top"));
    rect.right_top = ParsePoint(GetOptionalObjectField(obj, "right_top"));
    rect.right_down = ParsePoint(GetOptionalObjectField(obj, "right_down"));
    rect.left_down = ParsePoint(GetOptionalObjectField(obj, "left_down"));
    return rect;
}

bool IsSupportedVideoPixelFormat(const std::string& pixel_format) {
    return pixel_format.empty() || pixel_format == "bgr24";
}

SdkCaptureProfile ParseCaptureProfile(const Json& params, const std::string& device_id) {
    SdkCaptureProfile profile;
    profile.device_id = device_id;
    const Json profile_json = GetOptionalObjectField(params, "profile");
    if (profile_json.empty()) {
        return profile;
    }
    profile.profile_version = GetOptionalStringField(profile_json, "profile_version");
    if (profile.profile_version.empty()) {
        profile.profile_version = "capture.profile.v1";
    }
    profile.revision = GetOptionalIntField(profile_json, "revision", 1);

    const Json device_json = GetOptionalObjectField(profile_json, "device");
    if (!device_json.empty()) {
        const std::string profile_device_id = GetOptionalStringField(device_json, "device_id");
        if (!profile_device_id.empty()) {
            profile.device_id = profile_device_id;
        }
        const Json resolution_json = GetOptionalObjectField(device_json, "resolution");
        profile.width = GetOptionalIntField(resolution_json, "width", 0);
        profile.height = GetOptionalIntField(resolution_json, "height", 0);
        profile.fps = GetOptionalIntField(resolution_json, "fps", 0);
    }

    const Json capture_json = GetOptionalObjectField(profile_json, "capture");
    if (!capture_json.empty()) {
        const std::string page_processing = GetOptionalStringField(capture_json, "page_processing");
        const std::string color_mode = GetOptionalStringField(capture_json, "color_mode");
        if (!page_processing.empty()) {
            profile.page_processing = page_processing;
        }
        if (!color_mode.empty()) {
            profile.color_mode = color_mode;
        }
        const Json single_page_json = GetOptionalObjectField(capture_json, "single_page");
        profile.single_page = ParseSinglePageOptions(single_page_json, profile.single_page);
        profile.single_page_realtime_detect_rects = profile.single_page.realtime_detect_rects;
        const Json curved_book_json = GetOptionalObjectField(capture_json, "curved_book");
        profile.curved_book = ParseCurvedBookOptions(curved_book_json, profile.curved_book);
        const Json selected_area_json = GetOptionalObjectField(capture_json, "selected_area");
        if (!selected_area_json.empty()) {
            profile.selected_area_rect = ParseRect4P(GetOptionalObjectField(selected_area_json, "points"));
            const Json source_json = GetOptionalObjectField(selected_area_json, "source");
            profile.selected_area_source_width = GetOptionalIntField(source_json, "width", 0);
            profile.selected_area_source_height = GetOptionalIntField(source_json, "height", 0);
        }
    }

    const Json output_json = GetOptionalObjectField(profile_json, "output");
    if (!output_json.empty()) {
        const std::string format = GetOptionalStringField(output_json, "format");
        if (!format.empty()) {
            profile.output_format = format;
        }
        const Json thumbnails_json = GetOptionalObjectField(output_json, "thumbnails");
        profile.thumbnail_original = GetOptionalBoolField(thumbnails_json, "original", profile.thumbnail_original);
        profile.thumbnail_page_processed = GetOptionalBoolField(thumbnails_json, "page_processed", profile.thumbnail_page_processed);
        profile.thumbnail_color_processed = GetOptionalBoolField(thumbnails_json, "color_processed", profile.thumbnail_color_processed);
        profile.thumbnail_final = GetOptionalBoolField(thumbnails_json, "final", profile.thumbnail_final);
    }
    return profile;
}

Json BuildAssetJson(const SdkCaptureAsset& asset) {
    return Json{{"asset_id", asset.asset_id},
                {"kind", asset.kind},
                {"path", asset.path},
                {"url", asset.url},
                {"download_url", asset.download_url},
                {"content_type", asset.content_type},
                {"width", asset.width},
                {"height", asset.height},
                {"size", asset.size}};
}

Json BuildStageJson(const SdkCaptureStageResult& stage) {
    return Json{{"name", stage.name},
                {"status", stage.status},
                {"input", stage.input_assets},
                {"output", stage.output_assets},
                {"provider", stage.provider},
                {"message", stage.message}};
}

Json BuildImageProcessOutputJson(const SdkImageProcessOutput& output) {
    return Json{{"asset_id", output.asset_id},
                {"output_id", output.output_id},
                {"role", output.role},
                {"index", output.index},
                {"path", output.path},
                {"url", output.url},
                {"download_url", output.download_url},
                {"content_type", output.content_type},
                {"width", output.width},
                {"height", output.height},
                {"size", output.size}};
}

Json BuildCaptureTaskJson(const CaptureTaskSnapshot& task) {
    Json stages = Json::array();
    for (std::vector<SdkCaptureStageResult>::const_iterator it = task.stages.begin(); it != task.stages.end(); ++it) {
        stages.push_back(BuildStageJson(*it));
    }
    Json assets = Json::array();
    for (std::vector<SdkCaptureAsset>::const_iterator it = task.assets.begin(); it != task.assets.end(); ++it) {
        assets.push_back(BuildAssetJson(*it));
    }
    return Json{{"task_id", task.task_id},
                {"status", task.status},
                {"device_id", task.device_id},
                {"profile_revision", task.profile_revision},
                {"stages", stages},
                {"assets", assets},
                {"warnings", task.warnings},
                {"error", task.error}};
}

CommandApplicationService::MethodDescriptor MakeMethod(const std::string& method,
                                                       bool requires_session,
                                                       const std::string& summary) {
    CommandApplicationService::MethodDescriptor descriptor;
    descriptor.method = method;
    descriptor.requires_session = requires_session;
    descriptor.summary = summary;
    return descriptor;
}

std::string InferOutputFormatFromPath(const std::string& output_path) {
    const std::string::size_type pos = output_path.find_last_of('.');
    if (pos == std::string::npos || pos + 1 >= output_path.size()) {
        return "jpg";
    }
    std::string extension = output_path.substr(pos + 1);
    for (std::string::iterator it = extension.begin(); it != extension.end(); ++it) {
        *it = static_cast<char>(std::tolower(static_cast<unsigned char>(*it)));
    }
    if (extension == "jpeg") {
        return "jpg";
    }
    if (extension == "tif") {
        return "tiff";
    }
    return extension;
}

std::string NormalizeImageFormat(const std::string& format) {
    std::string value = NormalizeLower(format);
    if (value == "jpeg") {
        return "jpg";
    }
    if (value == "tif") {
        return "tiff";
    }
    return value;
}

std::string ExtensionForImageFormat(const std::string& format) {
    const std::string value = NormalizeImageFormat(format);
    if (value == "tiff") {
        return "tiff";
    }
    if (value == "jpg") {
        return "jpg";
    }
    return value;
}

bool IsGraphicConvertFormat(const std::string& format) {
    const std::string value = NormalizeImageFormat(format);
    return value == "jpg" || value == "png" || value == "tiff";
}

std::string OutputPathForIndexedAsset(const std::string& output_dir,
                                      const std::string& output_path,
                                      const std::string& prefix,
                                      int index,
                                      const std::string& extension) {
    if (index == 0 && !output_path.empty()) {
        return output_path;
    }
    std::ostringstream name;
    name << prefix;
    if (index > 0) {
        name << "-" << std::setw(3) << std::setfill('0') << (index + 1);
    }
    name << "." << extension;
    return JoinLocalPath(output_dir, name.str());
}

} // namespace

CommandApplicationService::CommandApplicationService(const SdkConfig& config, const ProviderBundle& providers)
    : config_(config),
      providers_(providers),
      authorization_service_(providers_),
      device_facade_(providers_),
      graphic_facade_(providers_),
      ocr_facade_(providers_),
      ofd_facade_(providers_),
      capture_task_service_(providers_, BuildDefaultAssetBaseUrl(config_)),
      next_image_task_seq_(1) {
    methods_.push_back(MakeMethod("system.ping", false, "SDK heartbeat probe"));
    methods_.push_back(MakeMethod("system.info", false, "SDK runtime status snapshot"));
    methods_.push_back(MakeMethod("system.capabilities", false, "List public methods and auth model"));
    methods_.push_back(MakeMethod("auth.create_session", false, "Create a bound session from token"));
    methods_.push_back(MakeMethod("auth.get_context", true, "Get the current bound auth context"));
    methods_.push_back(MakeMethod("auth.refresh_session", true, "Refresh the current bound session"));
    methods_.push_back(MakeMethod("auth.activate_offline", true, "Unlock one offline api key on this machine"));
    methods_.push_back(MakeMethod("auth.destroy_session", true, "Destroy the current bound session"));
    methods_.push_back(MakeMethod("device.list", true, "List devices visible to the current session"));
    methods_.push_back(MakeMethod("device.get", true, "Get one device visible to the current session"));
    methods_.push_back(MakeMethod("device.open", true, "Open a device"));
    methods_.push_back(MakeMethod("device.close", true, "Close a device and release active preview resources"));
    methods_.push_back(MakeMethod("capture.take", true, "Capture a still image"));
    methods_.push_back(MakeMethod("capture.get", true, "Get one capture task snapshot"));
    methods_.push_back(MakeMethod("video.start", true, "Create one video stream session"));
    methods_.push_back(MakeMethod("video.stop", true, "Stop one video stream session"));
    methods_.push_back(MakeMethod("video.set_format", true, "Update one video stream format"));
    methods_.push_back(MakeMethod("video.set_profile", true, "Update one video stream processing profile"));
    methods_.push_back(MakeMethod("image.process", true, "Run one combined image-processing request"));
    methods_.push_back(MakeMethod("image.process_page", true, "Run page processing and keep the source image format"));
    methods_.push_back(MakeMethod("image.apply_color_mode", true, "Apply one color mode and keep the source image format"));
    methods_.push_back(MakeMethod("ocr.recognize", true, "Submit one OCR request"));
    methods_.push_back(MakeMethod("file.convert", true, "Submit one file conversion request"));
}

void CommandApplicationService::SetStatusSupplier(StatusSupplier supplier) {
    status_supplier_ = std::move(supplier);
}

void CommandApplicationService::SetVideoFrameSink(VideoFrameSink sink) {
    video_frame_sink_ = std::move(sink);
}

void CommandApplicationService::SetVideoStreamClosedSink(VideoStreamClosedSink sink) {
    video_stream_closed_sink_ = std::move(sink);
}

void CommandApplicationService::SetCommandEventSink(CommandEventSink sink) {
    capture_task_service_.SetEventSink(std::move(sink));
}

Json CommandApplicationService::HandleRequest(const std::string& connection_id, const Json& request_json) {
    if (!request_json.is_object()) {
        return BuildWsResponse("", SdkStatusCode::InvalidRequest, "invalid request");
    }

    Request request;
    request.request_id = GetOptionalStringField(request_json, "request_id");
    request.method = GetOptionalStringField(request_json, "method");
    request.params = GetOptionalObjectField(request_json, "params");
    request.client = GetOptionalObjectField(request_json, "client");

    if (request.request_id.empty()) {
        return BuildWsResponse("", SdkStatusCode::InvalidRequest, "request_id required");
    }
    if (request.method.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidMethod, "missing method");
    }

    const MethodDescriptor* descriptor = FindMethod(request.method);
    if (descriptor == NULL) {
        return BuildWsResponse(request.request_id, SdkStatusCode::UnsupportedMethod, "unsupported method");
    }

    if (request.method == "system.ping") {
        return HandleSystemPing(request);
    }
    if (request.method == "system.info") {
        return HandleSystemInfo(request);
    }
    if (request.method == "system.capabilities") {
        return HandleSystemCapabilities(request);
    }
    if (request.method == "auth.create_session") {
        return HandleAuthCreateSession(connection_id, request);
    }
    if (request.method == "auth.get_context") {
        return HandleAuthGetContext(connection_id, request);
    }
    if (request.method == "auth.refresh_session") {
        return HandleAuthRefreshSession(connection_id, request);
    }
    if (request.method == "auth.activate_offline") {
        return HandleAuthActivateOffline(connection_id, request);
    }
    if (request.method == "auth.destroy_session") {
        return HandleAuthDestroySession(connection_id, request);
    }
    if (request.method == "device.list") {
        return HandleDeviceList(connection_id, request);
    }
    if (request.method == "device.get") {
        return HandleDeviceGet(connection_id, request);
    }
    if (request.method == "device.open") {
        return HandleDeviceOpen(connection_id, request);
    }
    if (request.method == "device.close") {
        return HandleDeviceClose(connection_id, request);
    }
    if (request.method == "capture.take") {
        return HandleCaptureTake(connection_id, request);
    }
    if (request.method == "capture.get") {
        return HandleCaptureGet(connection_id, request);
    }
    if (request.method == "video.start") {
        return HandleVideoStart(connection_id, request);
    }
    if (request.method == "video.stop") {
        return HandleVideoStop(connection_id, request);
    }
    if (request.method == "video.set_format") {
        return HandleVideoSetFormat(connection_id, request);
    }
    if (request.method == "video.set_profile") {
        return HandleVideoSetProfile(connection_id, request);
    }
    if (request.method == "image.process") {
        return HandleImageProcess(connection_id, request);
    }
    if (request.method == "image.process_page") {
        return HandleImageProcessPage(connection_id, request);
    }
    if (request.method == "image.apply_color_mode") {
        return HandleImageApplyColorMode(connection_id, request);
    }
    if (request.method == "ocr.recognize") {
        return HandleOcrRecognize(connection_id, request);
    }
    if (request.method == "file.convert") {
        return HandleFileConvert(connection_id, request);
    }

    return BuildWsResponse(request.request_id, SdkStatusCode::UnsupportedMethod, "unsupported method");
}

void CommandApplicationService::OnConnectionClosed(const std::string& connection_id) {
    authorization_service_.ClearConnection(connection_id);
    const std::vector<VideoSessionService::StreamBinding> removed = video_session_service_.ClearConnection(connection_id);
    for (std::vector<VideoSessionService::StreamBinding>::const_iterator it = removed.begin();
         it != removed.end();
         ++it) {
        if (providers_.device_provider) {
            SdkVideoStopRequest stop_request;
            stop_request.device_id = it->device_id;
            providers_.device_provider->StopVideo(stop_request);
        }
        if (video_stream_closed_sink_) {
            video_stream_closed_sink_(it->stream_id);
        }
    }
    const std::vector<std::string> opened_devices = ClearOpenedDevices(connection_id);
    for (std::vector<std::string>::const_iterator it = opened_devices.begin(); it != opened_devices.end(); ++it) {
        SdkDeviceCloseRequest close_request;
        close_request.device_id = *it;
        device_facade_.CloseDevice(AuthContext(), close_request);
    }
}

Json CommandApplicationService::BuildCapabilitiesJson() const {
    Json methods = Json::array();
    for (std::vector<MethodDescriptor>::const_iterator it = methods_.begin(); it != methods_.end(); ++it) {
        methods.push_back(Json{
            {"method", it->method},
            {"requires_session", it->requires_session},
            {"summary", it->summary},
        });
    }

    return Json{
        {"auth_model",
         {
             {"connection_requires_token", false},
             {"session_field", "session_token"},
             {"session_binding", "connection_bound"},
             {"video_binding", "session_token + stream_id"},
         }},
        {"methods", std::move(methods)},
    };
}

VideoSessionService::ValidationResult CommandApplicationService::ValidateVideoStream(const std::string& session_token,
                                                                                     const std::string& stream_id) const {
    return video_session_service_.Validate(session_token, stream_id);
}

CommandApplicationService::AssetAccessResult CommandApplicationService::ResolveAsset(const std::string& session_token,
                                                                                    const std::string& task_id,
                                                                                    const std::string& asset_id) const {
    AssetAccessResult result;
    AuthorizationService::SessionResult session_result = authorization_service_.RequireSessionToken(session_token);
    if (!IsOkStatusCode(session_result.code)) {
        result.code = session_result.code;
        result.message = session_result.message;
        return result;
    }

    AssetAccessResult image_asset = ResolveImageAsset(session_result.connection_id, task_id, asset_id);
    if (IsOkStatusCode(image_asset.code)) {
        if (!IsOkStatusCode(authorization_service_.RequireCapability(session_result.connection_id, "image.process").code)) {
            result.code = ToCode(SdkStatusCode::CapabilityNotAllowed);
            result.message = "capability not allowed";
            return result;
        }
        return image_asset;
    }

    if (!IsOkStatusCode(authorization_service_.RequireCapability(session_result.connection_id, "capture.get").code)) {
        result.code = ToCode(SdkStatusCode::CapabilityNotAllowed);
        result.message = "capability not allowed";
        return result;
    }
    const CaptureAssetResult asset_result =
        capture_task_service_.GetAsset(session_result.connection_id, task_id, asset_id);
    result.code = asset_result.code;
    result.message = asset_result.message;
    result.asset = asset_result.asset;
    return result;
}

CommandApplicationService::ImageUploadResult CommandApplicationService::UploadImage(const std::string& session_token,
                                                                                   const std::string& filename,
                                                                                   const std::string& content_type,
                                                                                   const std::string& content) {
    ImageUploadResult result;
    AuthorizationService::SessionResult session_result = authorization_service_.RequireSessionToken(session_token);
    if (!IsOkStatusCode(session_result.code)) {
        result.code = session_result.code;
        result.message = session_result.message;
        return result;
    }
    if (!IsOkStatusCode(authorization_service_.RequireCapability(session_result.connection_id, "image.process").code)) {
        result.code = ToCode(SdkStatusCode::CapabilityNotAllowed);
        result.message = "capability not allowed";
        return result;
    }
    const std::size_t max_upload_bytes = 50U * 1024U * 1024U;
    if (content.empty() || content.size() > max_upload_bytes) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "image upload is empty or too large";
        return result;
    }
    const std::string normalized_type = NormalizeLower(content_type);
    if (!normalized_type.empty() && normalized_type.find("image/") != 0) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "uploaded file is not an image";
        return result;
    }
    if (normalized_type.empty() && !IsSupportedImageExtension(ExtensionFromFilename(filename))) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "uploaded file is not an image";
        return result;
    }

    const std::string task_id = NextImageTaskId();
    const std::string asset_dir = GetSdkOpenTaskAssetDir("image", task_id, "assets");
    if (!EnsureDirectoryRecursive(asset_dir)) {
        result.code = ToCode(SdkStatusCode::InternalError);
        result.message = "failed to create image upload directory";
        return result;
    }
    const std::string extension = ImageExtensionForContentType(content_type, filename);
    const std::string output_path = JoinLocalPath(asset_dir, "original." + extension);
    if (!WriteBinaryFile(output_path, content)) {
        result.code = ToCode(SdkStatusCode::InternalError);
        result.message = "failed to write uploaded image";
        return result;
    }

    SdkCaptureAsset asset;
    asset.asset_id = "asset-original";
    asset.kind = "original";
    asset.path = output_path;
    asset.content_type = content_type.empty() ? ContentTypeForImageExtension(extension) : content_type;
    asset.size = FileSize(output_path);
    asset = AttachImageAssetUrls(task_id, asset);
    RegisterImageAsset(session_result.connection_id, task_id, asset);

    result.upload_id = task_id;
    result.asset = asset;
    return result;
}

std::string CommandApplicationService::NextImageTaskId() {
    const uint64_t seq = next_image_task_seq_.fetch_add(1);
    return "img-" + std::to_string(static_cast<long long>(std::time(NULL))) + "-" + std::to_string(seq);
}

SdkCaptureAsset CommandApplicationService::AttachImageAssetUrls(const std::string& task_id,
                                                                const SdkCaptureAsset& asset) const {
    SdkCaptureAsset copy = asset;
    const std::string base_url = BuildDefaultAssetBaseUrl(config_);
    copy.url = base_url + "/api/assets/" + task_id + "/" + copy.asset_id;
    copy.download_url = copy.url + "/download";
    return copy;
}

void CommandApplicationService::RegisterImageAsset(const std::string& connection_id,
                                                   const std::string& task_id,
                                                   const SdkCaptureAsset& asset) {
    std::lock_guard<std::mutex> lock(image_assets_mu_);
    image_asset_connection_by_task_[task_id] = connection_id;
    image_assets_by_task_[task_id][asset.asset_id] = asset;
}

CommandApplicationService::AssetAccessResult CommandApplicationService::ResolveImageAsset(const std::string& connection_id,
                                                                                         const std::string& task_id,
                                                                                         const std::string& asset_id) const {
    AssetAccessResult result;
    std::lock_guard<std::mutex> lock(image_assets_mu_);
    std::map<std::string, std::string>::const_iterator owner_it = image_asset_connection_by_task_.find(task_id);
    if (owner_it == image_asset_connection_by_task_.end()) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "image asset task not found";
        return result;
    }
    if (owner_it->second != connection_id) {
        result.code = ToCode(SdkStatusCode::CapabilityNotAllowed);
        result.message = "asset belongs to another connection";
        return result;
    }
    std::map<std::string, std::map<std::string, SdkCaptureAsset> >::const_iterator task_it = image_assets_by_task_.find(task_id);
    if (task_it == image_assets_by_task_.end()) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "image asset task not found";
        return result;
    }
    std::map<std::string, SdkCaptureAsset>::const_iterator asset_it = task_it->second.find(asset_id);
    if (asset_it == task_it->second.end()) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "image asset not found";
        return result;
    }
    result.asset = asset_it->second;
    return result;
}

std::size_t CommandApplicationService::ActiveSessionCount() const {
    return authorization_service_.ActiveSessionCount();
}

std::size_t CommandApplicationService::ActiveStreamCount() const {
    return video_session_service_.ActiveStreamCount();
}

Json CommandApplicationService::HandleSystemPing(const Request& request) const {
    return BuildWsResponse(request.request_id, SdkStatusCode::Ok, "ok", Json{{"pong", true}});
}

Json CommandApplicationService::HandleSystemInfo(const Request& request) const {
    return BuildWsResponse(request.request_id, SdkStatusCode::Ok, "ok", status_supplier_ ? status_supplier_() : Json::object());
}

Json CommandApplicationService::HandleSystemCapabilities(const Request& request) const {
    return BuildWsResponse(request.request_id, SdkStatusCode::Ok, "ok", BuildCapabilitiesJson());
}

Json CommandApplicationService::HandleAuthCreateSession(const std::string& connection_id, const Request& request) {
    const std::string token = GetOptionalStringField(request.params, "token");
    const AuthorizationService::SessionResult session_result = authorization_service_.CreateSession(connection_id, token);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    return BuildWsResponse(request.request_id, SdkStatusCode::Ok, "ok", BuildSessionJson(session_result));
}

Json CommandApplicationService::HandleAuthGetContext(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = authorization_service_.GetContext(connection_id);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"session_token", session_result.session_token},
                                {"auth_context", BuildAuthContextJson(session_result.auth_context)}});
}

Json CommandApplicationService::HandleAuthRefreshSession(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = authorization_service_.RefreshSession(connection_id);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    return BuildWsResponse(request.request_id, SdkStatusCode::Ok, "ok", BuildSessionJson(session_result));
}

Json CommandApplicationService::HandleAuthActivateOffline(const std::string& connection_id, const Request& request) {
    const std::string auth_code = GetOptionalStringField(request.params, "auth_code");
    if (auth_code.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "auth_code required");
    }

    const AuthorizationService::SessionResult session_result =
        authorization_service_.ActivateOffline(connection_id, auth_code);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    return BuildWsResponse(request.request_id, SdkStatusCode::Ok, "ok", BuildSessionJson(session_result));
}

Json CommandApplicationService::HandleAuthDestroySession(const std::string& connection_id, const Request& request) {
    const std::vector<VideoSessionService::StreamBinding> removed = video_session_service_.ClearConnection(connection_id);
    for (std::vector<VideoSessionService::StreamBinding>::const_iterator it = removed.begin();
         it != removed.end();
         ++it) {
        if (providers_.device_provider) {
            SdkVideoStopRequest stop_request;
            stop_request.device_id = it->device_id;
            providers_.device_provider->StopVideo(stop_request);
        }
        if (video_stream_closed_sink_) {
            video_stream_closed_sink_(it->stream_id);
        }
    }
    const std::vector<std::string> opened_devices = ClearOpenedDevices(connection_id);
    for (std::vector<std::string>::const_iterator it = opened_devices.begin(); it != opened_devices.end(); ++it) {
        SdkDeviceCloseRequest close_request;
        close_request.device_id = *it;
        device_facade_.CloseDevice(AuthContext(), close_request);
    }
    const AuthorizationService::SessionResult session_result = authorization_service_.DestroySession(connection_id);
    return BuildWsResponse(request.request_id, session_result.code, session_result.message, Json{{"destroyed", true}});
}

Json CommandApplicationService::HandleDeviceList(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }

    const DeviceListResult result = device_facade_.ListDevices(session_result.auth_context);
    if (!IsOkStatusCode(result.code)) {
        return BuildWsResponse(request.request_id, result.code, result.message);
    }

    Json devices = Json::array();
    for (std::vector<SdkDeviceDescriptor>::const_iterator it = result.devices.begin(); it != result.devices.end(); ++it) {
        devices.push_back(BuildDeviceJson(*it));
    }

    return BuildWsResponse(request.request_id, SdkStatusCode::Ok, "ok", Json{{"devices", devices}, {"count", devices.size()}});
}

Json CommandApplicationService::HandleDeviceGet(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    const std::string device_id = GetOptionalStringField(request.params, "device_id");
    if (device_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "device_id required");
    }

    const DeviceGetResult result = device_facade_.GetDevice(session_result.auth_context, device_id);
    if (!IsOkStatusCode(result.code)) {
        return BuildWsResponse(request.request_id, result.code, result.message);
    }
    Json data = BuildDeviceJson(result.device);
    data["provider"] = providers_.device_provider ? providers_.device_provider->ProviderName() : "";
    return BuildWsResponse(request.request_id, SdkStatusCode::Ok, "ok", data);
}

Json CommandApplicationService::HandleDeviceOpen(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    SdkDeviceOpenRequest open_request;
    open_request.device_id = GetOptionalStringField(request.params, "device_id");
    open_request.width = GetOptionalIntField(request.params, "width", 0);
    open_request.height = GetOptionalIntField(request.params, "height", 0);
    open_request.fps = GetOptionalIntField(request.params, "fps", 0);
    open_request.pixel_format = GetOptionalStringField(request.params, "pixel_format");
    if (open_request.pixel_format.empty()) {
        open_request.pixel_format = "bgr24";
    }
    if (open_request.device_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "device_id required");
    }
    if (!IsSupportedVideoPixelFormat(open_request.pixel_format)) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "unsupported pixel_format");
    }
    const SdkDeviceOpenResult result = device_facade_.OpenDevice(session_result.auth_context, open_request);
    if (!IsOkStatusCode(result.code)) {
        return BuildWsResponse(request.request_id, result.code, result.message);
    }
    Json data = BuildDeviceJson(result.device);
    data["device_id"] = result.device.device_id.empty() ? open_request.device_id : result.device.device_id;
    data["opened"] = result.opened;
    data["provider"] = providers_.device_provider ? providers_.device_provider->ProviderName() : "";
    if (result.opened) {
        RememberOpenedDevice(connection_id, open_request.device_id);
    }
    return BuildWsResponse(request.request_id, SdkStatusCode::Ok, "ok", data);
}

Json CommandApplicationService::HandleDeviceClose(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }

    SdkDeviceCloseRequest close_request;
    close_request.device_id = GetOptionalStringField(request.params, "device_id");
    if (close_request.device_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "device_id required");
    }

    bool stopped_stream = false;
    std::string stopped_stream_id;
    SdkVideoStopRequest stop_request;
    stop_request.device_id = close_request.device_id;
    const SdkVideoStopResult stop_result = device_facade_.StopVideo(session_result.auth_context, stop_request);
    if (!IsOkStatusCode(stop_result.code)) {
        return BuildWsResponse(request.request_id, stop_result.code, stop_result.message);
    }
    const VideoSessionService::StreamResult stream_result =
        video_session_service_.StopStream(connection_id, close_request.device_id);
    if (IsOkStatusCode(stream_result.code)) {
        stopped_stream = true;
        stopped_stream_id = stream_result.binding.stream_id;
        if (video_stream_closed_sink_) {
            video_stream_closed_sink_(stream_result.binding.stream_id);
        }
    } else if (stream_result.code != ToCode(SdkStatusCode::StreamNotFound)) {
        return BuildWsResponse(request.request_id, stream_result.code, stream_result.message);
    }

    const SdkDeviceCloseResult close_result = device_facade_.CloseDevice(session_result.auth_context, close_request);
    if (!IsOkStatusCode(close_result.code)) {
        return BuildWsResponse(request.request_id, close_result.code, close_result.message);
    }
    ForgetOpenedDevice(connection_id, close_request.device_id);

    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"device_id", close_request.device_id},
                                {"closed", close_result.closed},
                                {"was_opened", close_result.was_opened},
                                {"stopped_stream", stopped_stream},
                                {"stream_id", stopped_stream_id},
                                {"provider", providers_.device_provider ? providers_.device_provider->ProviderName() : ""}});
}

Json CommandApplicationService::HandleCaptureTake(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    const AuthorizationService::SessionResult quota_result = ConsumeQuota(connection_id, request.method, request.request_id);
    if (!IsOkStatusCode(quota_result.code)) {
        return BuildWsResponse(request.request_id, quota_result.code, quota_result.message);
    }

    const std::string device_id = GetOptionalStringField(request.params, "device_id");
    if (device_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "device_id required");
    }
    const Json profile_json = GetOptionalObjectField(request.params, "profile");
    if (profile_json.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "profile required");
    }

    CaptureTaskStartRequest start_request;
    start_request.connection_id = connection_id;
    start_request.session_token = session_result.session_token;
    start_request.device_id = device_id;
    start_request.output_dir = GetOptionalStringField(request.params, "output_dir");
    start_request.include_base64 = GetOptionalBoolField(request.params, "include_base64", false);
    start_request.timeout_ms = GetOptionalIntField(request.params, "timeout_ms", 15000);
    start_request.auth_context = session_result.auth_context;
    start_request.profile = ParseCaptureProfile(request.params, device_id);
    if (start_request.profile.device_id.empty()) {
        start_request.profile.device_id = device_id;
    }

    const CaptureTaskStartResult result = capture_task_service_.StartTask(start_request);
    if (!IsOkStatusCode(result.code)) {
        return BuildWsResponse(request.request_id, result.code, result.message);
    }
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"accepted", result.accepted},
                                {"task_id", result.task.task_id},
                                {"status", result.task.status}});
}

Json CommandApplicationService::HandleCaptureGet(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    const std::string task_id = GetOptionalStringField(request.params, "task_id");
    if (task_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "task_id required");
    }
    const CaptureTaskSnapshot task = capture_task_service_.GetTask(connection_id, task_id);
    if (!IsOkStatusCode(task.code)) {
        return BuildWsResponse(request.request_id, task.code, task.message);
    }
    return BuildWsResponse(request.request_id, SdkStatusCode::Ok, "ok", BuildCaptureTaskJson(task));
}

Json CommandApplicationService::HandleVideoStart(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }

    SdkVideoStartRequest start_request;
    start_request.device_id = GetOptionalStringField(request.params, "device_id");
    start_request.width = GetOptionalIntField(request.params, "width", 0);
    start_request.height = GetOptionalIntField(request.params, "height", 0);
    start_request.fps = GetOptionalIntField(request.params, "fps", 0);
    start_request.pixel_format = GetOptionalStringField(request.params, "pixel_format");
    if (start_request.pixel_format.empty()) {
        start_request.pixel_format = "bgr24";
    }
    if (start_request.device_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "device_id required");
    }
    if (!IsSupportedVideoPixelFormat(start_request.pixel_format)) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "unsupported pixel_format");
    }
    const Json profile_json = GetOptionalObjectField(request.params, "profile");
    if (!profile_json.empty()) {
        const SdkCaptureProfile profile = ParseCaptureProfile(request.params, start_request.device_id);
        start_request.page_processing = profile.page_processing;
        start_request.single_page_realtime_detect_rects = profile.single_page_realtime_detect_rects;
        start_request.single_page_multi_target_paging = profile.single_page.multi_target_paging;
    }

    const VideoSessionService::StreamResult stream_result =
        video_session_service_.RegisterStream(connection_id,
                                              session_result.session_token,
                                              start_request.device_id,
                                              start_request.pixel_format,
                                              start_request.width > 0 ? start_request.width : 1280,
                                              start_request.height > 0 ? start_request.height : 720,
                                              start_request.fps > 0 ? start_request.fps : 15);
    start_request.stream_id = stream_result.binding.stream_id;

    const SdkVideoStartResult start_result =
        device_facade_.StartVideo(session_result.auth_context,
                                  start_request,
                                  [this](const SdkVideoFrame& frame) {
                                      if (video_frame_sink_) {
                                          video_frame_sink_(frame);
                                      }
                                  });
    if (!IsOkStatusCode(start_result.code)) {
        video_session_service_.StopStreamById(stream_result.binding.stream_id);
        return BuildWsResponse(request.request_id, start_result.code, start_result.message);
    }

    const VideoSessionService::StreamResult updated_stream_result =
        video_session_service_.UpdateStreamFormat(connection_id,
                                                  start_request.device_id,
                                                  start_result.pixel_format,
                                                  start_result.width,
                                                  start_result.height,
                                                  start_result.fps);
    if (!IsOkStatusCode(updated_stream_result.code)) {
        return BuildWsResponse(request.request_id, updated_stream_result.code, updated_stream_result.message);
    }
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"device_id", start_request.device_id},
                                {"stream_id", updated_stream_result.binding.stream_id},
                                {"session_token", updated_stream_result.binding.session_token},
                                {"pixel_format", updated_stream_result.binding.pixel_format},
                                {"width", updated_stream_result.binding.width},
                                {"height", updated_stream_result.binding.height},
                                {"fps", updated_stream_result.binding.fps}});
}

Json CommandApplicationService::HandleVideoStop(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }

    SdkVideoStopRequest stop_request;
    stop_request.device_id = GetOptionalStringField(request.params, "device_id");
    if (stop_request.device_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "device_id required");
    }

    const SdkVideoStopResult stop_result = device_facade_.StopVideo(session_result.auth_context, stop_request);
    if (!IsOkStatusCode(stop_result.code)) {
        return BuildWsResponse(request.request_id, stop_result.code, stop_result.message);
    }
    const VideoSessionService::StreamResult stream_result = video_session_service_.StopStream(connection_id, stop_request.device_id);
    if (!IsOkStatusCode(stream_result.code)) {
        return BuildWsResponse(request.request_id, stream_result.code, stream_result.message);
    }
    if (video_stream_closed_sink_) {
        video_stream_closed_sink_(stream_result.binding.stream_id);
    }
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"device_id", stop_request.device_id},
                                {"stream_id", stream_result.binding.stream_id},
                                {"stopped", true}});
}

Json CommandApplicationService::HandleVideoSetFormat(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }

    SdkVideoFormatRequest format_request;
    format_request.device_id = GetOptionalStringField(request.params, "device_id");
    format_request.pixel_format = GetOptionalStringField(request.params, "pixel_format");
    format_request.width = GetOptionalIntField(request.params, "width", 1280);
    format_request.height = GetOptionalIntField(request.params, "height", 720);
    format_request.fps = GetOptionalIntField(request.params, "fps", 15);
    if (format_request.device_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "device_id required");
    }
    if (format_request.pixel_format.empty()) {
        format_request.pixel_format = "bgr24";
    }
    if (!IsSupportedVideoPixelFormat(format_request.pixel_format)) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "unsupported pixel_format");
    }

    const SdkVideoFormatResult format_result = device_facade_.SetVideoFormat(session_result.auth_context, format_request);
    if (!IsOkStatusCode(format_result.code)) {
        return BuildWsResponse(request.request_id, format_result.code, format_result.message);
    }
    const VideoSessionService::StreamResult stream_result =
        video_session_service_.UpdateStreamFormat(connection_id,
                                                  format_request.device_id,
                                                  format_request.pixel_format,
                                                  format_request.width,
                                                  format_request.height,
                                                  format_request.fps);
    if (!IsOkStatusCode(stream_result.code)) {
        return BuildWsResponse(request.request_id, stream_result.code, stream_result.message);
    }

    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"device_id", format_request.device_id},
                                {"stream_id", stream_result.binding.stream_id},
                                {"pixel_format", stream_result.binding.pixel_format},
                                {"width", stream_result.binding.width},
                                {"height", stream_result.binding.height},
                                {"fps", stream_result.binding.fps}});
}

Json CommandApplicationService::HandleVideoSetProfile(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }

    SdkVideoProfileRequest profile_request;
    profile_request.device_id = GetOptionalStringField(request.params, "device_id");
    if (profile_request.device_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "device_id required");
    }
    const Json profile_json = GetOptionalObjectField(request.params, "profile");
    if (profile_json.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "profile required");
    }

    const SdkCaptureProfile profile = ParseCaptureProfile(request.params, profile_request.device_id);
    profile_request.page_processing = profile.page_processing;
    profile_request.single_page_realtime_detect_rects = profile.single_page_realtime_detect_rects;
    profile_request.single_page_multi_target_paging = profile.single_page.multi_target_paging;

    const SdkVideoProfileResult profile_result = device_facade_.SetVideoProfile(session_result.auth_context, profile_request);
    if (!IsOkStatusCode(profile_result.code)) {
        return BuildWsResponse(request.request_id, profile_result.code, profile_result.message);
    }

    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"device_id", profile_request.device_id},
                                {"page_processing", profile_result.page_processing},
                                {"single_page",
                                 Json{{"realtime_detect_rects", profile_result.single_page_realtime_detect_rects},
                                      {"multi_target_paging", profile_result.single_page_multi_target_paging}}},
                                {"applied", profile_result.applied}});
}

Json CommandApplicationService::HandleImageProcess(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    const AuthorizationService::SessionResult quota_result = ConsumeQuota(connection_id, request.method, request.request_id);
    if (!IsOkStatusCode(quota_result.code)) {
        return BuildWsResponse(request.request_id, quota_result.code, quota_result.message);
    }

    SdkImageProcessRequest process_request;
    process_request.input_upload_id = GetOptionalStringField(request.params, "input_upload_id");
    process_request.input_path = GetOptionalStringField(request.params, "input_path");
    process_request.output_path = GetOptionalStringField(request.params, "output_path");
    process_request.output_dir = GetOptionalStringField(request.params, "output_dir");

    const Json profile_json = GetOptionalObjectField(request.params, "profile");
    if (!profile_json.empty()) {
        const SdkCaptureProfile profile = ParseCaptureProfile(request.params, "");
        process_request.page_processing = profile.page_processing;
        process_request.color_mode = profile.color_mode;
        process_request.output_format = profile.output_format;
        process_request.single_page = profile.single_page;
        process_request.curved_book = profile.curved_book;
        process_request.selected_area_rect = profile.selected_area_rect;
        process_request.selected_area_source_width = profile.selected_area_source_width;
        process_request.selected_area_source_height = profile.selected_area_source_height;
    }

    const std::string page_processing = GetOptionalStringField(request.params, "page_processing");
    if (!page_processing.empty()) {
        process_request.page_processing = page_processing;
    }
    const std::string color_mode = GetOptionalStringField(request.params, "color_mode");
    if (!color_mode.empty()) {
        process_request.color_mode = color_mode;
    }
    const std::string output_format = GetOptionalStringField(request.params, "output_format");
    if (!output_format.empty()) {
        process_request.output_format = output_format;
    } else if (process_request.output_format.empty() && !process_request.output_path.empty()) {
        process_request.output_format = InferOutputFormatFromPath(process_request.output_path);
    } else if (process_request.output_format.empty()) {
        process_request.output_format = "jpg";
    }
    process_request.scan_device_type = GetOptionalIntField(request.params, "scan_device_type", process_request.scan_device_type);

    const Json single_page_json = GetOptionalObjectField(request.params, "single_page");
    if (!single_page_json.empty()) {
        process_request.single_page = ParseSinglePageOptions(single_page_json, process_request.single_page);
    }
    const Json curved_book_json = GetOptionalObjectField(request.params, "curved_book");
    if (!curved_book_json.empty()) {
        process_request.curved_book = ParseCurvedBookOptions(curved_book_json, process_request.curved_book);
    }
    const Json selected_area_json = GetOptionalObjectField(request.params, "selected_area");
    if (!selected_area_json.empty()) {
        process_request.page_processing = "selected_area";
        process_request.selected_area_rect = ParseRect4P(GetOptionalObjectField(selected_area_json, "points"));
        const Json source_json = GetOptionalObjectField(selected_area_json, "source");
        process_request.selected_area_source_width = GetOptionalIntField(source_json, "width", 0);
        process_request.selected_area_source_height = GetOptionalIntField(source_json, "height", 0);
    }

    std::string image_task_id = process_request.input_upload_id;
    if (!process_request.input_upload_id.empty()) {
        AssetAccessResult input_asset = ResolveImageAsset(connection_id, process_request.input_upload_id, "asset-original");
        if (!IsOkStatusCode(input_asset.code)) {
            return BuildWsResponse(request.request_id, input_asset.code, input_asset.message);
        }
        process_request.input_path = input_asset.asset.path;
    }
    if (process_request.input_path.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "input_path or input_upload_id required");
    }
    if (!FileExists(process_request.input_path)) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "input image does not exist");
    }
    if (image_task_id.empty()) {
        image_task_id = NextImageTaskId();
    }
    if (process_request.output_dir.empty()) {
        process_request.output_dir = GetSdkOpenTaskAssetDir("image", image_task_id, "assets");
    }
    if (!EnsureDirectoryRecursive(process_request.output_dir)) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InternalError, "failed to create image process output directory");
    }
    if (process_request.output_path.empty()) {
        process_request.output_path = JoinLocalPath(process_request.output_dir, "final." + (process_request.output_format == "tiff" ? "tiff" : process_request.output_format));
    }

    const SdkImageProcessResult result = graphic_facade_.Process(process_request);
    if (!IsOkStatusCode(result.code)) {
        return BuildWsResponse(request.request_id, result.code, result.message);
    }
    Json outputs = Json::array();
    std::vector<SdkCaptureAsset> assets;
    for (std::vector<SdkImageProcessOutput>::const_iterator it = result.outputs.begin(); it != result.outputs.end(); ++it) {
        SdkImageProcessOutput output = *it;
        output.asset_id = output.index == 0 ? "asset-final" : ("asset-final-" + (output.output_id.empty() ? std::to_string(output.index + 1) : output.output_id));
        output.content_type = ContentTypeForOutputFormat(process_request.output_format);

        SdkCaptureAsset asset;
        asset.asset_id = output.asset_id;
        asset.kind = output.index == 0 ? "final" : ("final_" + (output.output_id.empty() ? std::to_string(output.index + 1) : output.output_id));
        asset.path = output.path;
        asset.content_type = output.content_type;
        asset.width = output.width;
        asset.height = output.height;
        asset.size = output.size > 0 ? output.size : FileSize(output.path);
        asset = AttachImageAssetUrls(image_task_id, asset);
        RegisterImageAsset(connection_id, image_task_id, asset);
        assets.push_back(asset);

        output.url = asset.url;
        output.download_url = asset.download_url;
        output.size = asset.size;
        outputs.push_back(BuildImageProcessOutputJson(output));
    }
    Json assets_json = Json::array();
    for (std::vector<SdkCaptureAsset>::const_iterator it = assets.begin(); it != assets.end(); ++it) {
        assets_json.push_back(BuildAssetJson(*it));
    }
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"task_id", image_task_id},
                                {"input_upload_id", process_request.input_upload_id},
                                {"input_path", process_request.input_path},
                                {"output_path", result.output_path.empty() ? process_request.output_path : result.output_path},
                                {"outputs", outputs},
                                {"assets", assets_json},
                                {"page_processing", process_request.page_processing},
                                {"color_mode", process_request.color_mode},
                                {"output_format", process_request.output_format},
                                {"processed", result.processed},
                                {"provider", providers_.graphic_provider ? providers_.graphic_provider->ProviderName() : ""}});
}

Json CommandApplicationService::HandleImageProcessPage(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    const AuthorizationService::SessionResult quota_result = ConsumeQuota(connection_id, request.method, request.request_id);
    if (!IsOkStatusCode(quota_result.code)) {
        return BuildWsResponse(request.request_id, quota_result.code, quota_result.message);
    }

    SdkPageProcessRequest page_request;
    const std::string input_upload_id = GetOptionalStringField(request.params, "input_upload_id");
    page_request.input_path = GetOptionalStringField(request.params, "input_path");
    page_request.output_dir = GetOptionalStringField(request.params, "output_dir");
    page_request.output_path = GetOptionalStringField(request.params, "output_path");
    page_request.page_processing = GetOptionalStringField(request.params, "page_processing");
    if (page_request.page_processing.empty()) {
        page_request.page_processing = "single_page";
    }
    page_request.scan_device_type = GetOptionalIntField(request.params, "scan_device_type", page_request.scan_device_type);

    const Json profile_json = GetOptionalObjectField(request.params, "profile");
    if (!profile_json.empty()) {
        const SdkCaptureProfile profile = ParseCaptureProfile(request.params, "");
        page_request.page_processing = profile.page_processing;
        page_request.single_page = profile.single_page;
        page_request.curved_book = profile.curved_book;
        page_request.selected_area_rect = profile.selected_area_rect;
        page_request.selected_area_source_width = profile.selected_area_source_width;
        page_request.selected_area_source_height = profile.selected_area_source_height;
    }
    const Json single_page_json = GetOptionalObjectField(request.params, "single_page");
    if (!single_page_json.empty()) {
        page_request.single_page = ParseSinglePageOptions(single_page_json, page_request.single_page);
    }
    const Json curved_book_json = GetOptionalObjectField(request.params, "curved_book");
    if (!curved_book_json.empty()) {
        page_request.curved_book = ParseCurvedBookOptions(curved_book_json, page_request.curved_book);
    }
    const Json selected_area_json = GetOptionalObjectField(request.params, "selected_area");
    if (!selected_area_json.empty()) {
        page_request.page_processing = "selected_area";
        page_request.selected_area_rect = ParseRect4P(GetOptionalObjectField(selected_area_json, "points"));
        const Json source_json = GetOptionalObjectField(selected_area_json, "source");
        page_request.selected_area_source_width = GetOptionalIntField(source_json, "width", 0);
        page_request.selected_area_source_height = GetOptionalIntField(source_json, "height", 0);
    }

    std::string image_task_id = input_upload_id;
    std::string source_extension;
    if (!input_upload_id.empty()) {
        AssetAccessResult input_asset = ResolveImageAsset(connection_id, input_upload_id, "asset-original");
        if (!IsOkStatusCode(input_asset.code)) {
            return BuildWsResponse(request.request_id, input_asset.code, input_asset.message);
        }
        page_request.input_path = input_asset.asset.path;
        source_extension = ImageExtensionForContentType(input_asset.asset.content_type, input_asset.asset.path);
    }
    if (page_request.input_path.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "input_path or input_upload_id required");
    }
    if (!FileExists(page_request.input_path)) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "input image does not exist");
    }
    if (source_extension.empty()) {
        source_extension = ExtensionFromFilename(page_request.input_path);
    }
    if (source_extension.empty() || !IsSupportedImageExtension(source_extension)) {
        source_extension = "jpg";
    }
    const std::string source_format = NormalizeImageFormat(source_extension);
    const std::string source_output_extension = ExtensionForImageFormat(source_format);
    if (!page_request.output_path.empty()) {
        const std::string output_format = NormalizeImageFormat(InferOutputFormatFromPath(page_request.output_path));
        if (!output_format.empty() && output_format != source_format) {
            return BuildWsResponse(request.request_id,
                                   SdkStatusCode::InvalidParams,
                                   "image.process_page output_path must keep the input image format");
        }
    }
    if (image_task_id.empty()) {
        image_task_id = NextImageTaskId();
    }
    if (page_request.output_dir.empty()) {
        page_request.output_dir = GetSdkOpenTaskAssetDir("image", image_task_id, "assets");
    }
    if (!EnsureDirectoryRecursive(page_request.output_dir)) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InternalError, "failed to create image process output directory");
    }
    if (page_request.output_path.empty()) {
        page_request.output_path = JoinLocalPath(page_request.output_dir, "page_processed." + source_output_extension);
    }

    const SdkPageProcessResult page_result = graphic_facade_.ProcessPage(page_request);
    if (!IsOkStatusCode(page_result.code)) {
        return BuildWsResponse(request.request_id, page_result.code, page_result.message);
    }

    Json outputs = Json::array();
    Json assets_json = Json::array();
    std::string first_output_path;
    for (std::vector<SdkPageOutput>::const_iterator it = page_result.outputs.begin(); it != page_result.outputs.end(); ++it) {
        SdkPageOutput page_output = *it;
        std::string final_path = page_output.path;
        const std::string output_format = NormalizeImageFormat(ExtensionFromFilename(page_output.path));
        if (output_format != source_format) {
            final_path = OutputPathForIndexedAsset(page_request.output_dir,
                                                   it->index == 0 ? page_request.output_path : "",
                                                   "page_processed",
                                                   it->index,
                                                   source_output_extension);
            if (IsGraphicConvertFormat(source_format)) {
                SdkFormatConvertRequest format_request;
                format_request.input_path = page_output.path;
                format_request.output_path = final_path;
                format_request.output_format = source_format;
                const SdkFormatConvertResult format_result = graphic_facade_.ConvertImageFormat(format_request);
                if (!IsOkStatusCode(format_result.code)) {
                    return BuildWsResponse(request.request_id, format_result.code, format_result.message);
                }
                final_path = format_result.output_path.empty() ? final_path : format_result.output_path;
            } else {
                return BuildWsResponse(request.request_id,
                                       SdkStatusCode::UnsupportedMethod,
                                       "input image format cannot be preserved for this page processing result");
            }
        }

        SdkImageProcessOutput output;
        output.asset_id = page_output.index == 0 ? "asset-page" : ("asset-page-" + page_output.output_id);
        output.output_id = page_output.output_id;
        output.role = page_output.role.empty() ? "page" : page_output.role;
        output.index = page_output.index;
        output.path = final_path;
        output.content_type = ContentTypeForImageExtension(source_output_extension);
        output.width = page_output.width;
        output.height = page_output.height;
        output.size = FileSize(final_path);

        SdkCaptureAsset asset;
        asset.asset_id = output.asset_id;
        asset.kind = output.index == 0 ? "page_processed" : ("page_processed_" + output.output_id);
        asset.path = output.path;
        asset.content_type = output.content_type;
        asset.width = output.width;
        asset.height = output.height;
        asset.size = output.size;
        asset = AttachImageAssetUrls(image_task_id, asset);
        RegisterImageAsset(connection_id, image_task_id, asset);
        output.url = asset.url;
        output.download_url = asset.download_url;
        assets_json.push_back(BuildAssetJson(asset));
        outputs.push_back(BuildImageProcessOutputJson(output));
        if (first_output_path.empty()) {
            first_output_path = output.path;
        }
        if (page_output.path != final_path && page_output.path != page_request.input_path) {
            std::remove(page_output.path.c_str());
        }
    }

    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"task_id", image_task_id},
                                {"input_upload_id", input_upload_id},
                                {"input_path", page_request.input_path},
                                {"output_path", first_output_path.empty() ? page_result.output_path : first_output_path},
                                {"outputs", outputs},
                                {"assets", assets_json},
                                {"page_processing", page_request.page_processing},
                                {"output_format", source_format},
                                {"processed", page_result.processed},
                                {"provider", providers_.graphic_provider ? providers_.graphic_provider->ProviderName() : ""}});
}

Json CommandApplicationService::HandleImageApplyColorMode(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    const AuthorizationService::SessionResult quota_result = ConsumeQuota(connection_id, request.method, request.request_id);
    if (!IsOkStatusCode(quota_result.code)) {
        return BuildWsResponse(request.request_id, quota_result.code, quota_result.message);
    }

    SdkColorModeRequest color_request;
    const std::string input_upload_id = GetOptionalStringField(request.params, "input_upload_id");
    color_request.input_path = GetOptionalStringField(request.params, "input_path");
    color_request.output_path = GetOptionalStringField(request.params, "output_path");
    color_request.color_mode = GetOptionalStringField(request.params, "color_mode");
    if (color_request.color_mode.empty()) {
        const SdkCaptureProfile profile = ParseCaptureProfile(request.params, "");
        color_request.color_mode = profile.color_mode.empty() ? "no_optimize" : profile.color_mode;
    }

    std::string image_task_id = input_upload_id;
    std::string source_extension;
    if (!input_upload_id.empty()) {
        AssetAccessResult input_asset = ResolveImageAsset(connection_id, input_upload_id, "asset-original");
        if (!IsOkStatusCode(input_asset.code)) {
            return BuildWsResponse(request.request_id, input_asset.code, input_asset.message);
        }
        color_request.input_path = input_asset.asset.path;
        source_extension = ImageExtensionForContentType(input_asset.asset.content_type, input_asset.asset.path);
    }
    if (color_request.input_path.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "input_path or input_upload_id required");
    }
    if (!FileExists(color_request.input_path)) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "input image does not exist");
    }
    if (source_extension.empty()) {
        source_extension = ExtensionFromFilename(color_request.input_path);
    }
    if (source_extension.empty() || !IsSupportedImageExtension(source_extension)) {
        source_extension = "jpg";
    }
    const std::string source_format = NormalizeImageFormat(source_extension);
    const std::string source_output_extension = ExtensionForImageFormat(source_format);
    if (!color_request.output_path.empty()) {
        const std::string output_format = NormalizeImageFormat(InferOutputFormatFromPath(color_request.output_path));
        if (!output_format.empty() && output_format != source_format) {
            return BuildWsResponse(request.request_id,
                                   SdkStatusCode::InvalidParams,
                                   "image.apply_color_mode output_path must keep the input image format");
        }
    }
    if (image_task_id.empty()) {
        image_task_id = NextImageTaskId();
    }
    const std::string output_dir = GetOptionalStringField(request.params, "output_dir").empty()
                                       ? GetSdkOpenTaskAssetDir("image", image_task_id, "assets")
                                       : GetOptionalStringField(request.params, "output_dir");
    if (!EnsureDirectoryRecursive(output_dir)) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InternalError, "failed to create image process output directory");
    }
    if (color_request.output_path.empty()) {
        color_request.output_path = JoinLocalPath(output_dir, "color_processed." + source_output_extension);
    }

    const SdkColorModeResult color_result = graphic_facade_.ApplyColorMode(color_request);
    if (!IsOkStatusCode(color_result.code)) {
        return BuildWsResponse(request.request_id, color_result.code, color_result.message);
    }
    const std::string final_path = color_result.output_path.empty() ? color_request.output_path : color_result.output_path;

    SdkImageProcessOutput output;
    output.asset_id = "asset-color";
    output.output_id = "color-001";
    output.role = "color";
    output.index = 0;
    output.path = final_path;
    output.content_type = ContentTypeForImageExtension(source_output_extension);
    output.size = FileSize(final_path);

    SdkCaptureAsset asset;
    asset.asset_id = output.asset_id;
    asset.kind = "color_processed";
    asset.path = output.path;
    asset.content_type = output.content_type;
    asset.size = output.size;
    asset = AttachImageAssetUrls(image_task_id, asset);
    RegisterImageAsset(connection_id, image_task_id, asset);
    output.url = asset.url;
    output.download_url = asset.download_url;

    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"task_id", image_task_id},
                                {"input_upload_id", input_upload_id},
                                {"input_path", color_request.input_path},
                                {"output_path", final_path},
                                {"outputs", Json::array({BuildImageProcessOutputJson(output)})},
                                {"assets", Json::array({BuildAssetJson(asset)})},
                                {"color_mode", color_request.color_mode},
                                {"output_format", source_format},
                                {"processed", color_result.processed},
                                {"provider", providers_.graphic_provider ? providers_.graphic_provider->ProviderName() : ""}});
}

Json CommandApplicationService::HandleOcrRecognize(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }

    SdkOcrRecognizeRequest ocr_request;
    ocr_request.input_files = GetOptionalStringArrayField(request.params, "input_files");
    ocr_request.output_path = GetOptionalStringField(request.params, "output_path");
    if (ocr_request.input_files.empty() || ocr_request.output_path.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "input_files and output_path required");
    }

    const SdkOcrRecognizeResult result = ocr_facade_.Recognize(ocr_request);
    if (!IsOkStatusCode(result.code)) {
        return BuildWsResponse(request.request_id, result.code, result.message);
    }
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"task_id", result.task_id},
                                {"input_count", ocr_request.input_files.size()},
                                {"output_path", ocr_request.output_path},
                                {"provider", providers_.ocr_provider ? providers_.ocr_provider->ProviderName() : ""}});
}

Json CommandApplicationService::HandleFileConvert(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    const AuthorizationService::SessionResult quota_result = ConsumeQuota(connection_id, request.method, request.request_id);
    if (!IsOkStatusCode(quota_result.code)) {
        return BuildWsResponse(request.request_id, quota_result.code, quota_result.message);
    }

    SdkFileConvertRequest convert_request;
    convert_request.input_upload_id = GetOptionalStringField(request.params, "input_upload_id");
    convert_request.input_path = GetOptionalStringField(request.params, "input_path");
    convert_request.output_path = GetOptionalStringField(request.params, "output_path");
    convert_request.output_format = NormalizeImageFormat(GetOptionalStringField(request.params, "output_format"));
    if (convert_request.output_format.empty() && !convert_request.output_path.empty()) {
        convert_request.output_format = NormalizeImageFormat(InferOutputFormatFromPath(convert_request.output_path));
    }

    std::string image_task_id = convert_request.input_upload_id;
    if (!convert_request.input_upload_id.empty()) {
        AssetAccessResult input_asset = ResolveImageAsset(connection_id, convert_request.input_upload_id, "asset-original");
        if (!IsOkStatusCode(input_asset.code)) {
            return BuildWsResponse(request.request_id, input_asset.code, input_asset.message);
        }
        convert_request.input_path = input_asset.asset.path;
    }
    if (convert_request.input_path.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "input_path or input_upload_id required");
    }

    const std::string input_extension = NormalizeImageFormat(ExtensionFromFilename(convert_request.input_path));
    if (IsSupportedImageExtension(input_extension) && convert_request.output_format.empty()) {
        return BuildWsResponse(request.request_id,
                               SdkStatusCode::InvalidParams,
                               "file.convert image conversion requires output_format or output_path");
    }
    if (IsSupportedImageExtension(input_extension) && !IsGraphicConvertFormat(convert_request.output_format)) {
        return BuildWsResponse(request.request_id,
                               SdkStatusCode::UnsupportedMethod,
                               "unsupported image output format: " + convert_request.output_format);
    }
    if (IsSupportedImageExtension(input_extension)) {
        if (image_task_id.empty()) {
            image_task_id = NextImageTaskId();
        }
        if (convert_request.output_path.empty()) {
            const std::string output_dir = GetSdkOpenTaskAssetDir("file", image_task_id, "assets");
            if (!EnsureDirectoryRecursive(output_dir)) {
                return BuildWsResponse(request.request_id, SdkStatusCode::InternalError, "failed to create file convert output directory");
            }
            convert_request.output_path =
                JoinLocalPath(output_dir, "converted." + ExtensionForImageFormat(convert_request.output_format));
        }

        SdkFormatConvertRequest format_request;
        format_request.input_path = convert_request.input_path;
        format_request.output_path = convert_request.output_path;
        format_request.output_format = convert_request.output_format;
        const SdkFormatConvertResult result = graphic_facade_.ConvertImageFormat(format_request);
        if (!IsOkStatusCode(result.code)) {
            return BuildWsResponse(request.request_id, result.code, result.message);
        }
        const std::string final_path = result.output_path.empty() ? convert_request.output_path : result.output_path;

        SdkCaptureAsset asset;
        asset.asset_id = "asset-converted";
        asset.kind = "converted";
        asset.path = final_path;
        asset.content_type = ContentTypeForImageExtension(ExtensionForImageFormat(convert_request.output_format));
        asset.size = FileSize(final_path);
        asset = AttachImageAssetUrls(image_task_id, asset);
        RegisterImageAsset(connection_id, image_task_id, asset);

        return BuildWsResponse(request.request_id,
                               SdkStatusCode::Ok,
                               "ok",
                               Json{{"task_id", image_task_id},
                                    {"input_upload_id", convert_request.input_upload_id},
                                    {"input_path", convert_request.input_path},
                                    {"output_path", final_path},
                                    {"output_format", convert_request.output_format},
                                    {"converted", result.converted},
                                    {"asset", BuildAssetJson(asset)},
                                    {"provider", providers_.graphic_provider ? providers_.graphic_provider->ProviderName() : ""}});
    }

    const SdkFileConvertResult result = ofd_facade_.Convert(convert_request);
    if (!IsOkStatusCode(result.code)) {
        return BuildWsResponse(request.request_id, result.code, result.message);
    }
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"input_path", convert_request.input_path},
                                {"output_path", result.output_path.empty() ? convert_request.output_path : result.output_path},
                                {"output_format", convert_request.output_format},
                                {"accepted", result.accepted},
                                {"provider", providers_.ofd_provider ? providers_.ofd_provider->ProviderName() : ""}});
}

AuthorizationService::SessionResult CommandApplicationService::RequireCapability(const std::string& connection_id,
                                                                                 const std::string& capability) const {
    AuthorizationService::SessionResult result = authorization_service_.RequireCapability(connection_id, capability);
    if (IsOkStatusCode(result.code) || (capability != "image.process_page" && capability != "image.apply_color_mode")) {
        return result;
    }
    return authorization_service_.RequireCapability(connection_id, "image.process");
}

AuthorizationService::SessionResult CommandApplicationService::ConsumeQuota(const std::string& connection_id,
                                                                            const std::string& capability,
                                                                            const std::string& request_id,
                                                                            int units) {
    AuthorizationService::SessionResult result =
        authorization_service_.ConsumeQuota(connection_id, capability, request_id, units);
    if (IsOkStatusCode(result.code) || (capability != "image.process_page" && capability != "image.apply_color_mode")) {
        return result;
    }
    return authorization_service_.ConsumeQuota(connection_id, "image.process", request_id, units);
}

Json CommandApplicationService::BuildSessionJson(const AuthorizationService::SessionResult& session_result) const {
    return Json{
        {"session_token", session_result.session_token},
        {"expires_in", session_result.expires_in},
        {"auth_context", BuildAuthContextJson(session_result.auth_context)},
    };
}

Json CommandApplicationService::BuildAuthContextJson(const AuthContext& auth_context) const {
    Json device_scope = Json::array();
    for (std::vector<SdkDeviceGrant>::const_iterator it = auth_context.device_scope.begin();
         it != auth_context.device_scope.end();
         ++it) {
        device_scope.push_back(Json{
            {"vid", it->vid},
            {"pid", it->pid},
        });
    }

    Json capabilities = Json::array();
    for (std::vector<std::string>::const_iterator it = auth_context.capabilities.begin();
         it != auth_context.capabilities.end();
         ++it) {
        capabilities.push_back(*it);
    }

    Json quota_buckets = Json::array();
    for (std::vector<AuthQuotaBucket>::const_iterator it = auth_context.quota_buckets.begin();
         it != auth_context.quota_buckets.end();
         ++it) {
        Json methods = Json::array();
        for (std::vector<std::string>::const_iterator method_it = it->methods.begin(); method_it != it->methods.end();
             ++method_it) {
            methods.push_back(*method_it);
        }
        quota_buckets.push_back(Json{
            {"bucket", it->bucket},
            {"methods", methods},
            {"limit", it->limit},
            {"remaining", it->remaining},
            {"enforcement", it->enforcement},
        });
    }

    return Json{
        {"is_valid", auth_context.is_valid},
        {"account_type", ToAccountTypeString(auth_context.account_type)},
        {"account_type_code", auth_context.account_type_code},
        {"auth_scene", auth_context.auth_scene},
        {"license_mode", auth_context.license_mode},
        {"entitlement_state", auth_context.entitlement_state},
        {"machine_code", auth_context.machine_code},
        {"device_scope", device_scope},
        {"expires_at", auth_context.expires_at},
        {"capabilities", capabilities},
        {"quota_buckets", quota_buckets},
    };
}

Json CommandApplicationService::BuildDeviceJson(const SdkDeviceDescriptor& device) const {
    Json resolutions = Json::array();
    for (std::vector<SdkVideoResolution>::const_iterator it = device.resolutions.begin();
         it != device.resolutions.end();
         ++it) {
        resolutions.push_back(Json{
            {"width", it->width},
            {"height", it->height},
            {"real_width", it->real_width},
            {"real_height", it->real_height},
            {"fps", it->fps},
            {"pixel_format", it->pixel_format},
            {"is_default", it->is_default},
        });
    }

    return Json{
        {"device_id", device.device_id},
        {"model", device.model},
        {"display_name", device.display_name},
        {"vid", device.vid},
        {"pid", device.pid},
        {"status", device.status},
        {"authorized", device.authorized},
        {"supports_video", device.supports_video},
        {"features", Json{{"image_transfer_protocol", device.image_transfer_protocol}}},
        {"resolutions", resolutions},
    };
}

const CommandApplicationService::MethodDescriptor* CommandApplicationService::FindMethod(const std::string& method) const {
    for (std::vector<MethodDescriptor>::const_iterator it = methods_.begin(); it != methods_.end(); ++it) {
        if (it->method == method) {
            return &(*it);
        }
    }
    return NULL;
}

void CommandApplicationService::RememberOpenedDevice(const std::string& connection_id, const std::string& device_id) {
    if (connection_id.empty() || device_id.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(opened_devices_mu_);
    opened_devices_by_connection_[connection_id].insert(device_id);
}

void CommandApplicationService::ForgetOpenedDevice(const std::string& connection_id, const std::string& device_id) {
    std::lock_guard<std::mutex> lock(opened_devices_mu_);
    std::map<std::string, std::set<std::string> >::iterator it = opened_devices_by_connection_.find(connection_id);
    if (it == opened_devices_by_connection_.end()) {
        return;
    }
    it->second.erase(device_id);
    if (it->second.empty()) {
        opened_devices_by_connection_.erase(it);
    }
}

std::vector<std::string> CommandApplicationService::ClearOpenedDevices(const std::string& connection_id) {
    std::vector<std::string> devices;
    std::lock_guard<std::mutex> lock(opened_devices_mu_);
    std::map<std::string, std::set<std::string> >::iterator it = opened_devices_by_connection_.find(connection_id);
    if (it == opened_devices_by_connection_.end()) {
        return devices;
    }
    for (std::set<std::string>::const_iterator device_it = it->second.begin(); device_it != it->second.end(); ++device_it) {
        devices.push_back(*device_it);
    }
    opened_devices_by_connection_.erase(it);
    return devices;
}

} // namespace sdk
} // namespace editor
