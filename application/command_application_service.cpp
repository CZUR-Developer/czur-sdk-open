// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "command_application_service.h"

#include <algorithm>
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

bool HasOnlineImageEnhanceApiKey(const AuthorizationService::SessionResult& session_result) {
    return !session_result.token.empty();
}

void ApplyOnlineImageEnhanceAvailability(SdkImageEnhanceCapabilityResult* result, bool online_available) {
    if (result == NULL) {
        return;
    }
    for (std::vector<SdkImageEnhanceCapability>::iterator it = result->capabilities.begin();
         it != result->capabilities.end();
         ++it) {
        if (it->runtime != "online") {
            continue;
        }
        it->available = online_available;
        if (online_available) {
            it->unavailable_reason.clear();
            it->unavailable_reason_zh_cn.clear();
        } else {
            it->unavailable_reason = "online enhance api key is not configured";
            it->unavailable_reason_zh_cn = "在线增强 API Key 未配置";
        }
    }
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

std::string ParentPath(const std::string& path) {
    const std::string::size_type slash_pos = path.find_last_of("/\\");
    if (slash_pos == std::string::npos) {
        return ".";
    }
    if (slash_pos == 0) {
        return path.substr(0, 1);
    }
    return path.substr(0, slash_pos);
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

bool IsSupportedUploadExtension(const std::string& extension) {
    std::string value = NormalizeLower(extension);
    if (value == "jpeg") {
        value = "jpg";
    } else if (value == "tif") {
        value = "tiff";
    }
    return IsSupportedImageExtension(value) || value == "pdf" || value == "ofd";
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

std::string ContentTypeForUploadExtension(const std::string& extension) {
    std::string value = NormalizeLower(extension);
    if (value == "tif") {
        value = "tiff";
    } else if (value == "jpeg") {
        value = "jpg";
    }
    if (value == "pdf") {
        return "application/pdf";
    }
    if (value == "ofd") {
        return "application/vnd.ofd";
    }
    return ContentTypeForImageExtension(value);
}

std::string UploadExtensionForContentType(const std::string& content_type, const std::string& filename) {
    const std::string type = NormalizeLower(content_type);
    std::string extension = NormalizeLower(ExtensionFromFilename(filename));
    if (extension == "jpeg") {
        extension = "jpg";
    } else if (extension == "tif") {
        extension = "tiff";
    }
    if (type.find("pdf") != std::string::npos || extension == "pdf") {
        return "pdf";
    }
    if (type.find("ofd") != std::string::npos || extension == "ofd") {
        return "ofd";
    }
    return ImageExtensionForContentType(content_type, filename);
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

bool ReadJsonFile(const std::string& path, Json* out_json) {
    if (out_json == NULL) {
        return false;
    }
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        return false;
    }
    try {
        in >> *out_json;
        return true;
    } catch (...) {
        return false;
    }
}

bool WriteJsonFile(const std::string& path, const Json& value) {
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << value.dump(2);
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

Json BuildOcrTaskJson(const SdkOcrTaskSnapshot& task) {
    Json output_paths = Json::array();
    for (std::vector<std::string>::const_iterator it = task.output_paths.begin(); it != task.output_paths.end(); ++it) {
        output_paths.push_back(*it);
    }
    return Json{{"task_id", task.task_id},
                {"status", task.status},
                {"progress", task.progress},
                {"output_path", task.output_path},
                {"output_paths", output_paths},
                {"format", task.format},
                {"exportType", task.export_type},
                {"message", task.message},
                {"error", task.error}};
}

Json BuildOcrTextBlockJson(const SdkOcrTextBlock& block) {
    return Json{{"text", block.text},
                {"x", block.x},
                {"y", block.y},
                {"width", block.width},
                {"height", block.height},
                {"confidence", block.confidence},
                {"font_size", block.font_size}};
}

Json BuildBarcodeJson(const SdkBarcodeResult& barcode) {
    Json points = Json::array();
    for (std::vector<SdkPoint2f>::const_iterator it = barcode.points.begin(); it != barcode.points.end(); ++it) {
        points.push_back(Json{{"x", it->x}, {"y", it->y}});
    }
    return Json{{"format", barcode.format},
                {"format_name", barcode.format_name},
                {"text", barcode.text},
                {"points", points}};
}

Json JsonFromEncodedValue(const std::string& value_json) {
    if (value_json.empty()) {
        return nullptr;
    }
    try {
        return Json::parse(value_json);
    } catch (...) {
        return value_json;
    }
}

std::string EncodeJsonValue(const Json& value) {
    return value.dump();
}

SdkImageEnhancePipeline ParseImageEnhancePipeline(const Json& pipeline_json) {
    SdkImageEnhancePipeline pipeline;
    if (pipeline_json.empty()) {
        return pipeline;
    }
    const std::string version = GetOptionalStringField(pipeline_json, "version");
    if (!version.empty()) {
        pipeline.version = version;
    }
    const Json target_json = GetOptionalObjectField(pipeline_json, "target");
    if (!target_json.empty()) {
        const std::string type = GetOptionalStringField(target_json, "type");
        const std::string format = GetOptionalStringField(target_json, "format");
        const std::string export_type = GetOptionalStringField(target_json, "export_type");
        if (!type.empty()) {
            pipeline.target.type = type;
        }
        if (!format.empty()) {
            pipeline.target.format = format;
        }
        if (!export_type.empty()) {
            pipeline.target.export_type = export_type;
        }
        pipeline.target.output_path = GetOptionalStringField(target_json, "path");
        if (pipeline.target.output_path.empty()) {
            pipeline.target.output_path = GetOptionalStringField(target_json, "output_path");
        }
        pipeline.target.output_dir = GetOptionalStringField(target_json, "dir");
        if (pipeline.target.output_dir.empty()) {
            pipeline.target.output_dir = GetOptionalStringField(target_json, "output_dir");
        }
        pipeline.target.quality = GetOptionalIntField(target_json, "quality", pipeline.target.quality);
        const std::string tiff_color = GetOptionalStringField(target_json, "tiff_color");
        const std::string tiff_compression = GetOptionalStringField(target_json, "tiff_compression");
        if (!tiff_color.empty()) {
            pipeline.target.tiff_color = tiff_color;
        }
        if (!tiff_compression.empty()) {
            pipeline.target.tiff_compression = tiff_compression;
        }
    }
    const Json options_json = GetOptionalObjectField(pipeline_json, "options");
    pipeline.keep_intermediate = GetOptionalBoolField(options_json, "keep_intermediate", pipeline.keep_intermediate);
    pipeline.include_metadata = GetOptionalBoolField(options_json, "include_metadata", pipeline.include_metadata);
    const Json::const_iterator steps_it = pipeline_json.find("steps");
    if (steps_it != pipeline_json.end() && steps_it->is_array()) {
        int index = 1;
        for (Json::const_iterator it = steps_it->begin(); it != steps_it->end(); ++it) {
            if (!it->is_object()) {
                continue;
            }
            SdkImageEnhanceStep step;
            step.id = GetOptionalStringField(*it, "id");
            if (step.id.empty()) {
                step.id = "step-" + std::to_string(index);
            }
            step.type = GetOptionalStringField(*it, "type");
            step.provider = GetOptionalStringField(*it, "provider");
            if (step.provider.empty()) {
                step.provider = "auto";
            }
            step.enabled = GetOptionalBoolField(*it, "enabled", true);
            step.on_error = GetOptionalStringField(*it, "on_error");
            if (step.on_error.empty()) {
                step.on_error = "fail";
            }
            const Json params_json = GetOptionalObjectField(*it, "params");
            step.params_json = params_json.empty() ? "{}" : EncodeJsonValue(params_json);
            if (!step.type.empty()) {
                pipeline.steps.push_back(step);
                ++index;
            }
        }
    }
    return pipeline;
}

std::string ImageEnhanceWorkflowDir() {
    return JoinPath(GetSdkOpenWorkDir(), "profiles");
}

std::string ImageEnhanceWorkflowStorePath() {
    return JoinPath(ImageEnhanceWorkflowDir(), "image_enhance_workflows.json");
}

Json LoadImageEnhanceWorkflowStore() {
    Json store;
    if (!ReadJsonFile(ImageEnhanceWorkflowStorePath(), &store) || !store.is_object()) {
        store = Json::object();
    }
    if (store.find("workflows") == store.end() || !store["workflows"].is_array()) {
        store["workflows"] = Json::array();
    }
    return store;
}

bool SaveImageEnhanceWorkflowStore(const Json& store) {
    if (!EnsureDirectoryRecursive(ImageEnhanceWorkflowDir())) {
        return false;
    }
    return WriteJsonFile(ImageEnhanceWorkflowStorePath(), store);
}

std::string CurrentTimestampString() {
    return std::to_string(static_cast<long long>(std::time(NULL)));
}

std::string NextWorkflowId() {
    static uint64_t seq = 1;
    return "wf-" + CurrentTimestampString() + "-" + std::to_string(static_cast<long long>(seq++));
}

Json NormalizeImageEnhanceWorkflow(Json workflow) {
    if (!workflow.is_object()) {
        workflow = Json::object();
    }
    if (!workflow.contains("workflow_id") || !workflow["workflow_id"].is_string() || workflow["workflow_id"].get<std::string>().empty()) {
        workflow["workflow_id"] = NextWorkflowId();
    }
    if (!workflow.contains("name") || !workflow["name"].is_string() || workflow["name"].get<std::string>().empty()) {
        workflow["name"] = "Untitled workflow";
    }
    if (!workflow.contains("description") || !workflow["description"].is_string()) {
        workflow["description"] = "";
    }
    if (!workflow.contains("pipeline") || !workflow["pipeline"].is_object()) {
        workflow["pipeline"] = Json{{"version", "image.enhance.pipeline.v1"},
                                    {"steps", Json::array()},
                                    {"target", Json{{"type", "images"}, {"format", "jpg"}, {"export_type", "single-page"}}}};
    }
    if (!workflow["pipeline"].contains("target") || !workflow["pipeline"]["target"].is_object()) {
        workflow["pipeline"]["target"] = Json{{"type", "images"}, {"format", "jpg"}, {"export_type", "single-page"}};
    }
    const std::string now = CurrentTimestampString();
    if (!workflow.contains("created_at") || !workflow["created_at"].is_string()) {
        workflow["created_at"] = now;
    }
    workflow["updated_at"] = now;
    return workflow;
}

Json BuildSaneStatusJson(const SdkSaneStatusResult& result, const std::string& provider) {
    return Json{{"available", result.available},
                {"platform", result.platform},
                {"supported_platforms", result.supported_platforms},
                {"sane_version", result.sane_version},
                {"sane_major", result.sane_major},
                {"sane_minor", result.sane_minor},
                {"reason", result.reason},
                {"provider", provider}};
}

Json BuildSaneDeviceJson(const SdkSaneDevice& device) {
    return Json{{"device_id", device.device_id},
                {"device_name", device.device_name},
                {"vendor", device.vendor},
                {"model", device.model},
                {"type", device.type},
                {"backend", device.backend},
                {"status", device.status},
                {"discovery_source", device.discovery_source},
                {"openable", device.openable}};
}

Json BuildSaneDevicesJson(const std::vector<SdkSaneDevice>& devices) {
    Json root = Json::array();
    for (std::vector<SdkSaneDevice>::const_iterator it = devices.begin(); it != devices.end(); ++it) {
        root.push_back(BuildSaneDeviceJson(*it));
    }
    return root;
}

Json BuildSaneDeviceEventJson(const SdkSaneDeviceEvent& event) {
    return Json{{"generation", event.generation},
                {"devices", BuildSaneDevicesJson(event.devices)},
                {"detected_devices", BuildSaneDevicesJson(event.detected_devices)},
                {"detected_count", event.detected_devices.size()},
                {"added_devices", BuildSaneDevicesJson(event.added_devices)},
                {"removed_devices", BuildSaneDevicesJson(event.removed_devices)}};
}

Json BuildSaneOptionConstraintJson(const SdkSaneOptionConstraint& constraint) {
    Json values = Json::array();
    for (std::vector<std::string>::const_iterator it = constraint.values_json.begin();
         it != constraint.values_json.end();
         ++it) {
        values.push_back(JsonFromEncodedValue(*it));
    }
    return Json{{"type", constraint.type},
                {"min", constraint.min},
                {"max", constraint.max},
                {"quant", constraint.quant},
                {"values", values}};
}

Json BuildSaneOptionJson(const SdkSaneOption& option) {
    return Json{{"index", option.index},
                {"name", option.name},
                {"title", option.title},
                {"description", option.description},
                {"group", option.group},
                {"type", option.type},
                {"unit", option.unit},
                {"value", JsonFromEncodedValue(option.value_json)},
                {"constraint", BuildSaneOptionConstraintJson(option.constraint)},
                {"readonly", option.readonly},
                {"settable", option.settable},
                {"automatic", option.automatic},
                {"inactive", option.inactive},
                {"advanced", option.advanced},
                {"requires_reload", option.requires_reload}};
}

Json BuildSaneOptionSetResultJson(const SdkSaneOptionSetResultItem& item) {
    return Json{{"key", item.key},
                {"index", item.index},
                {"status", item.status},
                {"message", item.message},
                {"value", JsonFromEncodedValue(item.value_json)},
                {"inexact", item.inexact},
                {"requires_reload", item.requires_reload}};
}

Json BuildSaneProfileJson(const SdkSaneProfile& profile) {
    Json options = Json::object();
    for (std::vector<SdkSaneOptionSetItem>::const_iterator it = profile.options.begin();
         it != profile.options.end();
         ++it) {
        const std::string key = !it->key.empty() ? it->key : std::to_string(it->index);
        options[key] = JsonFromEncodedValue(it->value_json);
    }
    return Json{{"profile_id", profile.profile_id},
                {"device_key", profile.device_key},
                {"name", profile.name},
                {"options", options},
                {"created_at", profile.created_at},
                {"updated_at", profile.updated_at}};
}

void EnsureSaneImageAssets(SdkSaneScanTask* task) {
    if (task == NULL || !task->assets.empty() || task->output_paths.empty()) {
        return;
    }
    const std::string output_type = NormalizeLower(task->output_type);
    const std::string output_format = NormalizeLower(task->output_format);
    const bool image_output = output_type == "images" ||
                              output_format == "jpg" ||
                              output_format == "jpeg" ||
                              output_format == "png" ||
                              output_format == "bmp" ||
                              output_format == "webp" ||
                              output_format == "tif" ||
                              output_format == "tiff";
    if (!image_output) {
        return;
    }
    for (std::vector<std::string>::const_iterator it = task->output_paths.begin();
         it != task->output_paths.end();
         ++it) {
        const std::string extension = ExtensionFromFilename(*it);
        if (!IsSupportedImageExtension(extension)) {
            continue;
        }
        std::ostringstream asset_id;
        asset_id << "asset-sane-page-" << std::setw(3) << std::setfill('0')
                 << static_cast<long long>(task->assets.size() + 1);
        SdkCaptureAsset asset;
        asset.asset_id = asset_id.str();
        asset.kind = "sane_scan_page";
        asset.path = *it;
        asset.content_type = ContentTypeForImageExtension(extension);
        asset.size = FileSize(*it);
        task->assets.push_back(asset);
    }
}

Json BuildSaneScanTaskJson(const SdkSaneScanTask& task) {
    Json output_paths = Json::array();
    for (std::vector<std::string>::const_iterator it = task.output_paths.begin(); it != task.output_paths.end(); ++it) {
        output_paths.push_back(*it);
    }
    Json assets = Json::array();
    for (std::vector<SdkCaptureAsset>::const_iterator it = task.assets.begin(); it != task.assets.end(); ++it) {
        assets.push_back(BuildAssetJson(*it));
    }
    return Json{{"task_id", task.task_id},
                {"connection_id", task.connection_id},
                {"session_id", task.session_id},
                {"status", task.status},
                {"phase", task.phase},
                {"progress", task.progress},
                {"page_count", task.page_count},
                {"current_page", task.current_page},
                {"output_type", task.output_type},
                {"output_format", task.output_format},
                {"output_dir", task.output_dir},
                {"export_type", task.export_type},
                {"output_path", task.output_path},
                {"output_paths", output_paths},
                {"last_page_path", task.last_page_path},
                {"assets", assets},
                {"message", task.message},
                {"error", task.error},
                {"started_at", task.started_at},
                {"updated_at", task.updated_at},
                {"cancel_requested", task.cancel_requested}};
}

std::vector<SdkSaneOptionSetItem> ParseSaneOptionSetItems(const Json& value) {
    std::vector<SdkSaneOptionSetItem> options;
    if (value.is_object()) {
        for (Json::const_iterator it = value.begin(); it != value.end(); ++it) {
            SdkSaneOptionSetItem item;
            item.key = it.key();
            item.value_json = EncodeJsonValue(*it);
            options.push_back(item);
        }
    } else if (value.is_array()) {
        for (Json::const_iterator it = value.begin(); it != value.end(); ++it) {
            if (!it->is_object()) {
                continue;
            }
            SdkSaneOptionSetItem item;
            item.key = GetOptionalStringField(*it, "key");
            if (item.key.empty()) {
                item.key = GetOptionalStringField(*it, "name");
            }
            item.index = GetOptionalIntField(*it, "index", -1);
            if (it->find("value") != it->end()) {
                item.value_json = EncodeJsonValue((*it)["value"]);
            } else if (it->find("value_json") != it->end() && (*it)["value_json"].is_string()) {
                item.value_json = (*it)["value_json"].get<std::string>();
            }
            if (!item.key.empty() || item.index >= 0) {
                options.push_back(item);
            }
        }
    }
    return options;
}

SdkSaneProfileRequest ParseSaneProfileRequest(const Json& params) {
    SdkSaneProfileRequest request;
    request.session_id = GetOptionalStringField(params, "session_id");
    request.device_id = GetOptionalStringField(params, "device_id");
    request.device_name = GetOptionalStringField(params, "device_name");
    request.device_key = GetOptionalStringField(params, "device_key");
    request.profile_id = GetOptionalStringField(params, "profile_id");
    request.name = GetOptionalStringField(params, "name");
    const Json options = params.find("options") == params.end() ? Json() : params["options"];
    request.options = ParseSaneOptionSetItems(options);
    return request;
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

bool IsFileConvertFormat(const std::string& format) {
    const std::string value = NormalizeImageFormat(format);
    return value == "jpg" || value == "png" || value == "tiff" || value == "pdf" || value == "ofd";
}

bool IsFileConvertDocumentSourceFormat(const std::string& format) {
    const std::string value = NormalizeImageFormat(format);
    return value == "pdf" || value == "ofd" || value == "tiff";
}

bool IsFileConvertSourceType(const std::string& source_type) {
    const std::string value = NormalizeImageFormat(source_type);
    return value == "image" || value == "images" || value == "base64" ||
           value == "pdf" || value == "ofd" || value == "tiff";
}

bool IsFileConvertImageSourceType(const std::string& source_type) {
    const std::string value = NormalizeImageFormat(source_type);
    return value == "image" || value == "images" || value == "base64";
}

std::string ExtensionForFileConvertFormat(const std::string& format) {
    const std::string value = NormalizeImageFormat(format);
    if (value == "jpg") {
        return "jpg";
    }
    if (value == "tiff") {
        return "tiff";
    }
    return value;
}

std::string ContentTypeForFileConvertFormat(const std::string& format) {
    const std::string value = NormalizeImageFormat(format);
    if (value == "pdf") {
        return "application/pdf";
    }
    if (value == "ofd") {
        return "application/vnd.ofd";
    }
    return ContentTypeForImageExtension(value);
}

std::string NormalizeExportType(std::string value) {
    value = NormalizeLower(value);
    if (value == "single_page") {
        return "single-page";
    }
    if (value == "multi_page") {
        return "multi-page";
    }
    return value;
}

std::string DefaultExportTypeForFileConvert(const std::string& output_format) {
    const std::string value = NormalizeImageFormat(output_format);
    if (value == "jpg" || value == "png") {
        return "single-page";
    }
    return "multi-page";
}

std::string DataUrlImageExtension(const std::string& value) {
    const std::string lower = NormalizeLower(value.substr(0, std::min<std::size_t>(value.size(), 64)));
    if (lower.find("data:image/png") == 0) {
        return "png";
    }
    if (lower.find("data:image/tiff") == 0 || lower.find("data:image/tif") == 0) {
        return "tiff";
    }
    if (lower.find("data:image/bmp") == 0) {
        return "bmp";
    }
    if (lower.find("data:image/webp") == 0) {
        return "webp";
    }
    return "jpg";
}

bool DecodeBase64ImagePayload(std::string value, std::string* decoded) {
    if (decoded == NULL) {
        return false;
    }
    const std::string::size_type comma_pos = value.find(',');
    if (value.find("data:") == 0 && comma_pos != std::string::npos) {
        value = value.substr(comma_pos + 1);
    }

    std::string compact;
    compact.reserve(value.size());
    for (std::string::const_iterator it = value.begin(); it != value.end(); ++it) {
        if (!std::isspace(static_cast<unsigned char>(*it))) {
            compact.push_back(*it);
        }
    }

    const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<int> table(256, -1);
    for (std::size_t i = 0; i < alphabet.size(); ++i) {
        table[static_cast<unsigned char>(alphabet[i])] = static_cast<int>(i);
    }

    int value_accumulator = 0;
    int bit_count = -8;
    decoded->clear();
    for (std::string::const_iterator it = compact.begin(); it != compact.end(); ++it) {
        const unsigned char ch = static_cast<unsigned char>(*it);
        if (ch == '=') {
            break;
        }
        if (table[ch] == -1) {
            return false;
        }
        value_accumulator = (value_accumulator << 6) + table[ch];
        bit_count += 6;
        if (bit_count >= 0) {
            decoded->push_back(static_cast<char>((value_accumulator >> bit_count) & 0xFF));
            bit_count -= 8;
        }
    }
    return !decoded->empty();
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

CommandApplicationService::CommandApplicationService(const SdkConfig& config,
                                                     const ProviderBundle& providers,
                                                     std::shared_ptr<RuntimeConfigService> runtime_config)
    : config_(config),
      runtime_config_(runtime_config),
      providers_(providers),
      authorization_service_(providers_, [this]() {
          return runtime_config_ ? runtime_config_->AuthzBaseUrl() : std::string();
      }),
      device_facade_(providers_),
      graphic_facade_(providers_),
      ocr_facade_(providers_),
      ofd_facade_(providers_),
      recognition_facade_(providers_),
      sane_facade_(providers_),
      capture_task_service_(providers_, BuildDefaultAssetBaseUrl(config_)),
      image_enhance_task_service_(providers_, BuildDefaultAssetBaseUrl(config_)),
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
    methods_.push_back(MakeMethod("image.enhance_capabilities", true, "List image enhancement pipeline capabilities"));
    methods_.push_back(MakeMethod("image.enhance", true, "Submit one image enhancement pipeline task"));
    methods_.push_back(MakeMethod("image.enhance_get", true, "Get one image enhancement task snapshot"));
    methods_.push_back(MakeMethod("image.enhance_cancel", true, "Cancel one image enhancement task"));
    methods_.push_back(MakeMethod("image.enhance_workflow_list", true, "List saved image enhancement workflows"));
    methods_.push_back(MakeMethod("image.enhance_workflow_get", true, "Get one saved image enhancement workflow"));
    methods_.push_back(MakeMethod("image.enhance_workflow_save", true, "Save one image enhancement workflow"));
    methods_.push_back(MakeMethod("image.enhance_workflow_delete", true, "Delete one image enhancement workflow"));
    methods_.push_back(MakeMethod("ocr.recognize", true, "Submit one OCR request"));
    methods_.push_back(MakeMethod("ocr.get", true, "Get one OCR task snapshot"));
    methods_.push_back(MakeMethod("ocr.cancel", true, "Cancel one OCR task"));
    methods_.push_back(MakeMethod("ocr.extract_text", true, "Extract OCR text blocks from one image"));
    methods_.push_back(MakeMethod("recognition.barcode_detect", true, "Detect barcode or QR code from one image"));
    methods_.push_back(MakeMethod("file.convert", true, "Submit one file conversion request"));
    methods_.push_back(MakeMethod("sane.status", true, "Get Linux-only SANE runtime status"));
    methods_.push_back(MakeMethod("sane.list", true, "List SANE scanner devices on Linux"));
    methods_.push_back(MakeMethod("sane.watch_start", true, "Start SANE device hotplug monitoring"));
    methods_.push_back(MakeMethod("sane.watch_stop", true, "Stop SANE device hotplug monitoring"));
    methods_.push_back(MakeMethod("sane.open", true, "Open one SANE scanner session"));
    methods_.push_back(MakeMethod("sane.close", true, "Close one SANE scanner session"));
    methods_.push_back(MakeMethod("sane.get_options", true, "Get normalized SANE scanner options"));
    methods_.push_back(MakeMethod("sane.set_options", true, "Set normalized SANE scanner options"));
    methods_.push_back(MakeMethod("sane.profile_list", true, "List saved SANE option profiles"));
    methods_.push_back(MakeMethod("sane.profile_save", true, "Save one SANE option profile"));
    methods_.push_back(MakeMethod("sane.profile_apply", true, "Apply one saved SANE option profile"));
    methods_.push_back(MakeMethod("sane.profile_delete", true, "Delete one saved SANE option profile"));
    methods_.push_back(MakeMethod("sane.scan", true, "Submit one SANE scan task"));
    methods_.push_back(MakeMethod("sane.scan_get", true, "Get one SANE scan task snapshot"));
    methods_.push_back(MakeMethod("sane.scan_cancel", true, "Cancel one SANE scan task"));
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
    capture_task_service_.SetEventSink(sink);
    image_enhance_task_service_.SetEventSink(sink);
    {
        std::lock_guard<std::mutex> lock(command_event_sink_mu_);
        command_event_sink_ = std::move(sink);
    }
    sane_facade_.SetDeviceEventSink([this](const SdkSaneDeviceEvent& event) {
        DispatchSaneDeviceEvent(event);
    });
    sane_facade_.SetScanTaskEventSink([this](const SdkSaneScanTaskEvent& event) {
        DispatchSaneScanTaskEvent(event);
    });
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
    if (request.method == "image.enhance_capabilities") {
        return HandleImageEnhanceCapabilities(connection_id, request);
    }
    if (request.method == "image.enhance") {
        return HandleImageEnhance(connection_id, request);
    }
    if (request.method == "image.enhance_get") {
        return HandleImageEnhanceGet(connection_id, request);
    }
    if (request.method == "image.enhance_cancel") {
        return HandleImageEnhanceCancel(connection_id, request);
    }
    if (request.method == "image.enhance_workflow_list") {
        return HandleImageEnhanceWorkflowList(connection_id, request);
    }
    if (request.method == "image.enhance_workflow_get") {
        return HandleImageEnhanceWorkflowGet(connection_id, request);
    }
    if (request.method == "image.enhance_workflow_save") {
        return HandleImageEnhanceWorkflowSave(connection_id, request);
    }
    if (request.method == "image.enhance_workflow_delete") {
        return HandleImageEnhanceWorkflowDelete(connection_id, request);
    }
    if (request.method == "ocr.recognize") {
        return HandleOcrRecognize(connection_id, request);
    }
    if (request.method == "ocr.get") {
        return HandleOcrGet(connection_id, request);
    }
    if (request.method == "ocr.cancel") {
        return HandleOcrCancel(connection_id, request);
    }
    if (request.method == "ocr.extract_text") {
        return HandleOcrExtractText(connection_id, request);
    }
    if (request.method == "recognition.barcode_detect") {
        return HandleBarcodeDetect(connection_id, request);
    }
    if (request.method == "file.convert") {
        return HandleFileConvert(connection_id, request);
    }
    if (request.method == "sane.status") {
        return HandleSaneStatus(connection_id, request);
    }
    if (request.method == "sane.list") {
        return HandleSaneList(connection_id, request);
    }
    if (request.method == "sane.watch_start") {
        return HandleSaneWatchStart(connection_id, request);
    }
    if (request.method == "sane.watch_stop") {
        return HandleSaneWatchStop(connection_id, request);
    }
    if (request.method == "sane.open") {
        return HandleSaneOpen(connection_id, request);
    }
    if (request.method == "sane.close") {
        return HandleSaneClose(connection_id, request);
    }
    if (request.method == "sane.get_options") {
        return HandleSaneGetOptions(connection_id, request);
    }
    if (request.method == "sane.set_options") {
        return HandleSaneSetOptions(connection_id, request);
    }
    if (request.method == "sane.profile_list") {
        return HandleSaneProfileList(connection_id, request);
    }
    if (request.method == "sane.profile_save") {
        return HandleSaneProfileSave(connection_id, request);
    }
    if (request.method == "sane.profile_apply") {
        return HandleSaneProfileApply(connection_id, request);
    }
    if (request.method == "sane.profile_delete") {
        return HandleSaneProfileDelete(connection_id, request);
    }
    if (request.method == "sane.scan") {
        return HandleSaneScan(connection_id, request);
    }
    if (request.method == "sane.scan_get") {
        return HandleSaneScanGet(connection_id, request);
    }
    if (request.method == "sane.scan_cancel") {
        return HandleSaneScanCancel(connection_id, request);
    }

    return BuildWsResponse(request.request_id, SdkStatusCode::UnsupportedMethod, "unsupported method");
}

void CommandApplicationService::OnConnectionClosed(const std::string& connection_id) {
    SdkSaneWatchRequest sane_watch_request;
    sane_watch_request.connection_id = connection_id;
    sane_watch_request.enabled = false;
    sane_facade_.WatchStop(sane_watch_request);

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

void CommandApplicationService::DispatchSaneDeviceEvent(const SdkSaneDeviceEvent& event) {
    CommandEventSink sink;
    {
        std::lock_guard<std::mutex> lock(command_event_sink_mu_);
        sink = command_event_sink_;
    }
    if (!sink || event.connection_id.empty() || event.event_name.empty()) {
        return;
    }
    sink(event.connection_id,
         BuildWsEvent(event.event_name,
                      BuildSaneDeviceEventJson(event),
                      event.code,
                      event.message));
}

void CommandApplicationService::DispatchSaneScanTaskEvent(const SdkSaneScanTaskEvent& event) {
    SdkSaneScanTask task = event.task;
    EnsureSaneImageAssets(&task);
    for (std::vector<SdkCaptureAsset>::iterator it = task.assets.begin(); it != task.assets.end(); ++it) {
        *it = AttachImageAssetUrls(task.task_id, *it);
        RegisterImageAsset(task.connection_id, task.task_id, *it);
    }

    SdkImageEnhancePipeline sane_pipeline;
    std::string online_api_key;
    std::string online_base_url;
    {
        std::lock_guard<std::mutex> lock(sane_tasks_mu_);
        std::map<std::string, SdkImageEnhancePipeline>::const_iterator pipeline_it = sane_pipelines_by_task_.find(task.task_id);
        if (pipeline_it != sane_pipelines_by_task_.end()) {
            sane_pipeline = pipeline_it->second;
        }
        std::map<std::string, std::string>::const_iterator api_key_it = sane_online_api_keys_by_task_.find(task.task_id);
        if (api_key_it != sane_online_api_keys_by_task_.end()) {
            online_api_key = api_key_it->second;
        }
        std::map<std::string, std::string>::const_iterator base_url_it = sane_online_base_urls_by_task_.find(task.task_id);
        if (base_url_it != sane_online_base_urls_by_task_.end()) {
            online_base_url = base_url_it->second;
        }
    }
    if (task.status == "completed" && !sane_pipeline.steps.empty() && !task.output_paths.empty()) {
        task.status = "enhancing";
        task.phase = "enhancing";
        task.progress = 90;
        task.message = "enhancing scan pages";
        {
            std::lock_guard<std::mutex> lock(sane_tasks_mu_);
            sane_tasks_[task.task_id] = task;
        }
        CommandEventSink enhance_sink;
        {
            std::lock_guard<std::mutex> lock(command_event_sink_mu_);
            enhance_sink = command_event_sink_;
        }
        if (enhance_sink && !task.connection_id.empty()) {
            enhance_sink(task.connection_id,
                         BuildWsEvent("sane.scan_changed",
                                      Json{{"task_id", task.task_id},
                                           {"task", BuildSaneScanTaskJson(task)},
                                           {"provider", providers_.sane_provider ? providers_.sane_provider->ProviderName() : ""}},
                                      ToCode(SdkStatusCode::Ok),
                                      task.message));
        }

        std::vector<SdkImageEnhancePage> pages;
        for (std::size_t i = 0; i < task.output_paths.size(); ++i) {
            SdkImageEnhancePage page;
            page.source_index = static_cast<int>(i + 1);
            page.output_index = static_cast<int>(i + 1);
            page.path = task.output_paths[i];
            pages.push_back(page);
        }
        bool enhance_failed = false;
        std::string enhance_error;
        if (!providers_.image_enhance_provider) {
            enhance_failed = true;
            enhance_error = "image enhance provider is not available";
        }
        for (std::size_t step_index = 0; !enhance_failed && step_index < sane_pipeline.steps.size(); ++step_index) {
            const SdkImageEnhanceStep& step = sane_pipeline.steps[step_index];
            if (!step.enabled) {
                continue;
            }
            SdkImageEnhanceStepRequest step_request;
            step_request.task_id = task.task_id;
            step_request.step = step;
            step_request.pages = pages;
            step_request.output_dir = JoinLocalPath(task.output_dir.empty() ? GetSdkOpenTaskAssetDir("sane", task.task_id, "enhance")
                                                                            : task.output_dir,
                                                    "enhance-step-" + std::to_string(static_cast<long long>(step_index + 1)));
            step_request.online_api_key = online_api_key;
            step_request.online_base_url = online_base_url;
            EnsureDirectoryRecursive(step_request.output_dir);
            const SdkImageEnhanceStepResult step_result = providers_.image_enhance_provider->RunStep(step_request);
            if (!IsOkStatusCode(step_result.code)) {
                if (step.on_error == "skip") {
                    continue;
                }
                enhance_failed = true;
                enhance_error = step_result.message;
                break;
            }
            pages = step_result.pages;
        }

        if (enhance_failed || pages.empty()) {
            task.status = "failed";
            task.phase = "failed";
            task.progress = 100;
            task.message = enhance_failed ? enhance_error : "image enhance produced no output pages";
            task.error = task.message;
        } else {
            task.output_paths.clear();
            task.assets.clear();
            for (std::vector<SdkImageEnhancePage>::const_iterator it = pages.begin(); it != pages.end(); ++it) {
                task.output_paths.push_back(it->path);
                SdkCaptureAsset asset;
                asset.asset_id = "asset-sane-enhanced-page-" + std::to_string(static_cast<long long>(task.assets.size() + 1));
                asset.kind = "sane_scan_enhanced_page";
                asset.path = it->path;
                asset.content_type = ContentTypeForImageExtension(ExtensionFromFilename(it->path));
                asset.size = FileSize(it->path);
                asset = AttachImageAssetUrls(task.task_id, asset);
                RegisterImageAsset(task.connection_id, task.task_id, asset);
                task.assets.push_back(asset);
            }
            task.output_path = task.output_paths.empty() ? "" : task.output_paths.front();
            task.page_count = static_cast<int>(task.output_paths.size());
            task.status = "completed";
            task.phase = "enhanced";
            task.progress = 94;
            task.message = "scan pages enhanced";
        }
    }

    const std::string document_target = NormalizeImageFormat(task.output_type);
    if (task.status == "completed" &&
        (document_target == "pdf" || document_target == "ofd" || document_target == "tiff") &&
        !task.output_paths.empty()) {
        SdkSaneScanTask converting_task = task;
        converting_task.status = "converting";
        converting_task.phase = "converting";
        converting_task.progress = 95;
        converting_task.message = "converting scan pages";
        {
            std::lock_guard<std::mutex> lock(sane_tasks_mu_);
            sane_tasks_[converting_task.task_id] = converting_task;
        }

        CommandEventSink sink;
        {
            std::lock_guard<std::mutex> lock(command_event_sink_mu_);
            sink = command_event_sink_;
        }
        if (sink && !converting_task.connection_id.empty()) {
            sink(converting_task.connection_id,
                 BuildWsEvent("sane.scan_changed",
                              Json{{"task_id", converting_task.task_id},
                                   {"task", BuildSaneScanTaskJson(converting_task)},
                                   {"provider", providers_.sane_provider ? providers_.sane_provider->ProviderName() : ""}},
                              ToCode(SdkStatusCode::Ok),
                              "converting scan pages"));
        }

        SdkFileConvertRequest convert_request;
        convert_request.input_paths = task.output_paths;
        convert_request.source_type = "images";
        convert_request.source_format = "image";
        convert_request.target_type = document_target;
        convert_request.output_format = document_target;
        convert_request.export_type = task.export_type.empty() ? "multi-page" : task.export_type;
        convert_request.output_dir = task.output_dir.empty()
                                         ? GetSdkOpenTaskAssetDir("sane", task.task_id, "outputs")
                                         : task.output_dir;
        convert_request.output_path = task.output_path;
        if (convert_request.export_type != "single-page" && convert_request.output_path.empty()) {
            convert_request.output_path = JoinLocalPath(convert_request.output_dir, "scan." + ExtensionForFileConvertFormat(document_target));
        }

        if (!EnsureDirectoryRecursive(convert_request.output_dir)) {
            task.status = "failed";
            task.phase = "failed";
            task.progress = 100;
            task.message = "failed to create SANE conversion output directory";
            task.error = task.message;
        } else {
            const SdkFileConvertResult convert_result = ofd_facade_.Convert(convert_request);
            if (IsOkStatusCode(convert_result.code)) {
                task.output_type = document_target;
                task.output_format = document_target;
                task.output_dir = convert_request.output_dir;
                task.output_path = convert_result.output_path;
                task.output_paths = convert_result.output_paths;
                task.assets.clear();
                for (std::vector<std::string>::const_iterator it = task.output_paths.begin();
                     it != task.output_paths.end();
                     ++it) {
                    SdkCaptureAsset asset;
                    asset.asset_id = "asset-sane-output-" + std::to_string(static_cast<long long>(task.assets.size() + 1));
                    asset.kind = "sane_scan_output";
                    asset.path = *it;
                    asset.content_type = ContentTypeForFileConvertFormat(document_target);
                    asset.size = FileSize(*it);
                    asset = AttachImageAssetUrls(task.task_id, asset);
                    RegisterImageAsset(task.connection_id, task.task_id, asset);
                    task.assets.push_back(asset);
                }
                task.status = "completed";
                task.phase = "completed";
                task.progress = 100;
                task.message = "scan converted";
                task.error.clear();
            } else {
                task.status = "failed";
                task.phase = "failed";
                task.progress = 100;
                task.message = convert_result.message;
                task.error = convert_result.message;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(sane_tasks_mu_);
        sane_tasks_[task.task_id] = task;
    }

    CommandEventSink sink;
    {
        std::lock_guard<std::mutex> lock(command_event_sink_mu_);
        sink = command_event_sink_;
    }
    if (!sink || task.connection_id.empty()) {
        return;
    }
    sink(task.connection_id,
         BuildWsEvent(event.event_name.empty() ? "sane.scan_changed" : event.event_name,
                      Json{{"task_id", task.task_id},
                           {"task", BuildSaneScanTaskJson(task)},
                           {"provider", providers_.sane_provider ? providers_.sane_provider->ProviderName() : ""}},
                      event.code,
                      task.message.empty() ? event.message : task.message));
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
        {"platform_capabilities",
         Json{{"sane",
               Json{{"supported_platforms", Json::array({"linux"})},
                    {"linux_only", true},
                    {"provider", providers_.sane_provider ? providers_.sane_provider->ProviderName() : ""}}}}},
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
        const bool can_read_image_asset =
            IsOkStatusCode(authorization_service_.RequireCapability(session_result.connection_id, "image.process").code) ||
            IsOkStatusCode(authorization_service_.RequireCapability(session_result.connection_id, "image.enhance").code) ||
            IsOkStatusCode(authorization_service_.RequireCapability(session_result.connection_id, "file.convert").code) ||
            IsOkStatusCode(authorization_service_.RequireCapability(session_result.connection_id, "sane.scan").code);
        if (!can_read_image_asset) {
            result.code = ToCode(SdkStatusCode::CapabilityNotAllowed);
            result.message = "capability not allowed";
            return result;
        }
        return image_asset;
    }

    if (IsOkStatusCode(authorization_service_.RequireCapability(session_result.connection_id, "image.enhance").code)) {
        const SdkImageEnhanceTaskSnapshot enhance_task = image_enhance_task_service_.GetTask(session_result.connection_id, task_id);
        if (IsOkStatusCode(enhance_task.code)) {
            for (std::vector<SdkCaptureAsset>::const_iterator it = enhance_task.assets.begin(); it != enhance_task.assets.end(); ++it) {
                if (it->asset_id == asset_id) {
                    result.asset = *it;
                    return result;
                }
            }
        }
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
    const bool can_upload_for_image =
        IsOkStatusCode(authorization_service_.RequireCapability(session_result.connection_id, "image.process").code);
    const bool can_upload_for_enhance =
        IsOkStatusCode(authorization_service_.RequireCapability(session_result.connection_id, "image.enhance").code);
    const bool can_upload_for_file =
        IsOkStatusCode(authorization_service_.RequireCapability(session_result.connection_id, "file.convert").code);
    if (!can_upload_for_image && !can_upload_for_enhance && !can_upload_for_file) {
        result.code = ToCode(SdkStatusCode::CapabilityNotAllowed);
        result.message = "capability not allowed";
        return result;
    }
    const std::size_t max_upload_bytes = 50U * 1024U * 1024U;
    if (content.empty() || content.size() > max_upload_bytes) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "uploaded file is empty or too large";
        return result;
    }
    const std::string normalized_type = NormalizeLower(content_type);
    const std::string extension_from_name = NormalizeLower(ExtensionFromFilename(filename));
    const bool has_supported_typed_content =
        normalized_type.find("image/") == 0 ||
        normalized_type.find("pdf") != std::string::npos ||
        normalized_type.find("ofd") != std::string::npos;
    const bool is_supported_content_type =
        normalized_type.empty() ||
        has_supported_typed_content ||
        normalized_type == "application/octet-stream";
    if (!is_supported_content_type) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "uploaded file must be an image, PDF, OFD, or TIFF";
        return result;
    }
    if (extension_from_name.empty() ? !has_supported_typed_content : !IsSupportedUploadExtension(extension_from_name)) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "uploaded file must be an image, PDF, OFD, or TIFF";
        return result;
    }

    const std::string task_id = NextImageTaskId();
    const std::string asset_dir = GetSdkOpenTaskAssetDir("image", task_id, "assets");
    if (!EnsureDirectoryRecursive(asset_dir)) {
        result.code = ToCode(SdkStatusCode::InternalError);
        result.message = "failed to create image upload directory";
        return result;
    }
    const std::string extension = UploadExtensionForContentType(content_type, filename);
    const std::string output_path = JoinLocalPath(asset_dir, "original." + extension);
    if (!WriteBinaryFile(output_path, content)) {
        result.code = ToCode(SdkStatusCode::InternalError);
        result.message = "failed to write uploaded file";
        return result;
    }

    SdkCaptureAsset asset;
    asset.asset_id = "asset-original";
    asset.kind = "original";
    asset.path = output_path;
    asset.content_type = content_type.empty() || content_type == "application/octet-stream" ? ContentTypeForUploadExtension(extension) : content_type;
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
    start_request.pipeline = ParseImageEnhancePipeline(GetOptionalObjectField(request.params, "pipeline"));
    start_request.online_api_key = session_result.token;
    start_request.online_base_url = runtime_config_ ? runtime_config_->OnlineImageEnhanceBaseUrl() : "";
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

Json CommandApplicationService::HandleImageEnhanceCapabilities(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, "image.enhance");
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    Json providers = Json::array();
    if (providers_.image_enhance_provider) {
        SdkImageEnhanceCapabilityResult result = providers_.image_enhance_provider->ListCapabilities();
        if (!IsOkStatusCode(result.code)) {
            return BuildWsResponse(request.request_id, result.code, result.message);
        }
        ApplyOnlineImageEnhanceAvailability(&result, HasOnlineImageEnhanceApiKey(session_result));
        providers.push_back(BuildImageEnhanceCapabilityProviderJson(result));
    }
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"providers", providers},
                                {"pipeline_version", "image.enhance.pipeline.v1"}});
}

Json CommandApplicationService::HandleImageEnhance(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }

    SdkImageEnhanceTaskRequest enhance_request;
    enhance_request.connection_id = connection_id;
    enhance_request.output_dir = GetOptionalStringField(request.params, "output_dir");
    enhance_request.online_api_key = session_result.token;
    enhance_request.online_base_url = runtime_config_ ? runtime_config_->OnlineImageEnhanceBaseUrl() : "";
    enhance_request.authz_base_url = runtime_config_ ? runtime_config_->AuthzBaseUrl() : "";
    Json source_json = GetOptionalObjectField(request.params, "source");
    if (source_json.empty()) {
        source_json = request.params;
    }

    std::vector<std::string> upload_ids = GetOptionalStringArrayField(source_json, "input_upload_ids");
    const std::string single_upload_id = GetOptionalStringField(source_json, "input_upload_id");
    if (!single_upload_id.empty()) {
        upload_ids.push_back(single_upload_id);
    }
    enhance_request.input_paths = GetOptionalStringArrayField(source_json, "input_paths");
    const std::string single_input_path = GetOptionalStringField(source_json, "input_path");
    if (!single_input_path.empty()) {
        enhance_request.input_paths.push_back(single_input_path);
    }

    for (std::vector<std::string>::const_iterator it = upload_ids.begin(); it != upload_ids.end(); ++it) {
        AssetAccessResult input_asset = ResolveImageAsset(connection_id, *it, "asset-original");
        if (!IsOkStatusCode(input_asset.code)) {
            return BuildWsResponse(request.request_id, input_asset.code, input_asset.message);
        }
        enhance_request.input_paths.push_back(input_asset.asset.path);
    }
    if (enhance_request.input_paths.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "image.enhance source is empty");
    }

    enhance_request.pipeline = ParseImageEnhancePipeline(GetOptionalObjectField(request.params, "pipeline"));
    const SdkImageEnhanceTaskResult result = image_enhance_task_service_.StartTask(enhance_request);
    if (!IsOkStatusCode(result.code)) {
        return BuildWsResponse(request.request_id, result.code, result.message);
    }
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"accepted", result.accepted},
                                {"task_id", result.task_id},
                                {"status", result.task.status},
                                {"task", BuildImageEnhanceTaskJson(result.task)}});
}

Json CommandApplicationService::HandleImageEnhanceGet(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    const std::string task_id = GetOptionalStringField(request.params, "task_id");
    if (task_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "task_id required");
    }
    const SdkImageEnhanceTaskSnapshot task = image_enhance_task_service_.GetTask(connection_id, task_id);
    if (!IsOkStatusCode(task.code)) {
        return BuildWsResponse(request.request_id, task.code, task.message);
    }
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"task_id", task_id},
                                {"task", BuildImageEnhanceTaskJson(task)}});
}

Json CommandApplicationService::HandleImageEnhanceCancel(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    SdkImageEnhanceCancelRequest cancel_request;
    cancel_request.task_id = GetOptionalStringField(request.params, "task_id");
    if (cancel_request.task_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "task_id required");
    }
    const SdkImageEnhanceTaskResult result = image_enhance_task_service_.CancelTask(connection_id, cancel_request);
    if (!IsOkStatusCode(result.code)) {
        return BuildWsResponse(request.request_id, result.code, result.message);
    }
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"accepted", result.accepted},
                                {"task_id", result.task_id},
                                {"task", BuildImageEnhanceTaskJson(result.task)}});
}

Json CommandApplicationService::HandleImageEnhanceWorkflowList(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, "image.enhance");
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    const Json store = LoadImageEnhanceWorkflowStore();
    const Json workflows = store.find("workflows") != store.end() && store["workflows"].is_array() ? store["workflows"] : Json::array();
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"workflows", workflows},
                                {"count", workflows.size()},
                                {"provider", providers_.image_enhance_provider ? providers_.image_enhance_provider->ProviderName() : ""}});
}

Json CommandApplicationService::HandleImageEnhanceWorkflowGet(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, "image.enhance");
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    const std::string workflow_id = GetOptionalStringField(request.params, "workflow_id");
    if (workflow_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "workflow_id required");
    }
    const Json store = LoadImageEnhanceWorkflowStore();
    const Json workflows = store.find("workflows") != store.end() && store["workflows"].is_array() ? store["workflows"] : Json::array();
    for (Json::const_iterator it = workflows.begin(); it != workflows.end(); ++it) {
        if (it->is_object() && GetOptionalStringField(*it, "workflow_id") == workflow_id) {
            return BuildWsResponse(request.request_id, SdkStatusCode::Ok, "ok", Json{{"workflow", *it}});
        }
    }
    return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "image enhance workflow not found");
}

Json CommandApplicationService::HandleImageEnhanceWorkflowSave(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, "image.enhance");
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    Json workflow = GetOptionalObjectField(request.params, "workflow");
    if (workflow.empty()) {
        workflow = request.params;
    }
    workflow = NormalizeImageEnhanceWorkflow(workflow);
    const std::string workflow_id = GetOptionalStringField(workflow, "workflow_id");
    Json store = LoadImageEnhanceWorkflowStore();
    Json workflows = store.find("workflows") != store.end() && store["workflows"].is_array() ? store["workflows"] : Json::array();
    bool updated = false;
    for (Json::iterator it = workflows.begin(); it != workflows.end(); ++it) {
        if (it->is_object() && GetOptionalStringField(*it, "workflow_id") == workflow_id) {
            if (it->contains("created_at") && (*it)["created_at"].is_string()) {
                workflow["created_at"] = (*it)["created_at"];
            }
            *it = workflow;
            updated = true;
            break;
        }
    }
    if (!updated) {
        workflows.push_back(workflow);
    }
    store["workflows"] = workflows;
    if (!SaveImageEnhanceWorkflowStore(store)) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InternalError, "failed to save image enhance workflow");
    }
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"saved", true},
                                {"updated", updated},
                                {"workflow", workflow}});
}

Json CommandApplicationService::HandleImageEnhanceWorkflowDelete(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, "image.enhance");
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    const std::string workflow_id = GetOptionalStringField(request.params, "workflow_id");
    if (workflow_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "workflow_id required");
    }
    Json store = LoadImageEnhanceWorkflowStore();
    Json workflows = store.find("workflows") != store.end() && store["workflows"].is_array() ? store["workflows"] : Json::array();
    Json kept = Json::array();
    bool deleted = false;
    for (Json::const_iterator it = workflows.begin(); it != workflows.end(); ++it) {
        if (it->is_object() && GetOptionalStringField(*it, "workflow_id") == workflow_id) {
            deleted = true;
            continue;
        }
        kept.push_back(*it);
    }
    if (!deleted) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "image enhance workflow not found");
    }
    store["workflows"] = kept;
    if (!SaveImageEnhanceWorkflowStore(store)) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InternalError, "failed to delete image enhance workflow");
    }
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"deleted", true},
                                {"workflow_id", workflow_id},
                                {"count", kept.size()}});
}

Json CommandApplicationService::HandleOcrRecognize(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }

    SdkOcrRecognizeRequest ocr_request;
    ocr_request.input_upload_ids = GetOptionalStringArrayField(request.params, "input_upload_ids");
    const std::string single_upload_id = GetOptionalStringField(request.params, "input_upload_id");
    if (!single_upload_id.empty()) {
        ocr_request.input_upload_ids.push_back(single_upload_id);
    }
    ocr_request.input_files = GetOptionalStringArrayField(request.params, "input_files");
    const std::string single_input_path = GetOptionalStringField(request.params, "input_path");
    if (!single_input_path.empty()) {
        ocr_request.input_files.push_back(single_input_path);
    }
    ocr_request.output_path = GetOptionalStringField(request.params, "output_path");
    ocr_request.output_dir = GetOptionalStringField(request.params, "output_dir");
    ocr_request.format = NormalizeLower(GetOptionalStringField(request.params, "format"));
    if (ocr_request.format.empty() && !ocr_request.output_path.empty()) {
        ocr_request.format = NormalizeLower(InferOutputFormatFromPath(ocr_request.output_path));
    }
    if (ocr_request.format.empty()) {
        ocr_request.format = "docx";
    }
    if (ocr_request.format == "jpeg" || ocr_request.format == "jpg") {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "ocr format must be txt/pdf/docx/xlsx/ofd/json");
    }
    if (ocr_request.format != "txt" && ocr_request.format != "pdf" &&
        ocr_request.format != "docx" && ocr_request.format != "xlsx" && ocr_request.format != "ofd" &&
        ocr_request.format != "json") {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "unsupported ocr format");
    }
    ocr_request.export_type = GetOptionalStringField(request.params, "exportType");
    if (ocr_request.export_type.empty()) {
        ocr_request.export_type = GetOptionalStringField(request.params, "export_type");
    }
    ocr_request.export_type = NormalizeLower(ocr_request.export_type);
    if (ocr_request.export_type == "single_page") {
        ocr_request.export_type = "single-page";
    } else if (ocr_request.export_type == "multi_page") {
        ocr_request.export_type = "multi-page";
    }
    if (ocr_request.export_type.empty()) {
        ocr_request.export_type = "multi-page";
    }
    if (ocr_request.export_type != "multi-page" && ocr_request.export_type != "single-page") {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "exportType must be multi-page or single-page");
    }

    Json ext_params = Json::object();
    const Json params_json = GetOptionalObjectField(request.params, "params");
    if (!params_json.empty()) {
        ext_params = params_json;
    }
    const Json ext_params_json = GetOptionalObjectField(request.params, "ext_params");
    if (!ext_params_json.empty()) {
        for (Json::const_iterator it = ext_params_json.begin(); it != ext_params_json.end(); ++it) {
            ext_params[it.key()] = it.value();
        }
    }
    const std::vector<const char*> ext_keys = {"encoding", "paperSize", "exportType", "ocrPreference", "quality", "exportFormat"};
    for (std::vector<const char*>::const_iterator it = ext_keys.begin(); it != ext_keys.end(); ++it) {
        const auto json_it = request.params.find(*it);
        if (json_it != request.params.end()) {
            ext_params[*it] = *json_it;
        }
    }
    ext_params["format"] = ocr_request.format;
    ext_params["exportType"] = ocr_request.export_type;
    ocr_request.ext_params_json = ext_params.dump();

    for (std::vector<std::string>::const_iterator it = ocr_request.input_upload_ids.begin();
         it != ocr_request.input_upload_ids.end();
         ++it) {
        AssetAccessResult input_asset = ResolveImageAsset(connection_id, *it, "asset-original");
        if (!IsOkStatusCode(input_asset.code)) {
            return BuildWsResponse(request.request_id, input_asset.code, input_asset.message);
        }
        ocr_request.input_files.push_back(input_asset.asset.path);
    }

    if (ocr_request.input_files.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "input required");
    }
    if (ocr_request.export_type == "multi-page" && ocr_request.output_path.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "output_path required");
    }
    if (ocr_request.export_type == "single-page") {
        if (ocr_request.output_dir.empty() && !ocr_request.output_path.empty()) {
            ocr_request.output_dir = ExtensionFromFilename(ocr_request.output_path).empty()
                                         ? ocr_request.output_path
                                         : ParentPath(ocr_request.output_path);
        }
        if (ocr_request.output_dir.empty()) {
            return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "output_dir or output_path required");
        }
        if (!EnsureDirectoryRecursive(ocr_request.output_dir)) {
            return BuildWsResponse(request.request_id, SdkStatusCode::InternalError, "failed to create ocr output directory");
        }
        if (ocr_request.output_path.empty()) {
            ocr_request.output_path = ocr_request.output_dir;
        }
    }
    for (std::vector<std::string>::const_iterator it = ocr_request.input_files.begin();
         it != ocr_request.input_files.end();
         ++it) {
        if (!FileExists(*it)) {
            return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "input file not found: " + *it);
        }
    }

    const SdkOcrRecognizeResult result = ocr_facade_.Recognize(ocr_request);
    if (!IsOkStatusCode(result.code)) {
        return BuildWsResponse(request.request_id, result.code, result.message);
    }
    Json output_paths = Json::array();
    for (std::vector<std::string>::const_iterator it = result.task.output_paths.begin(); it != result.task.output_paths.end(); ++it) {
        output_paths.push_back(*it);
    }
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"task_id", result.task_id},
                                {"task", BuildOcrTaskJson(result.task)},
                                {"input_count", ocr_request.input_files.size()},
                                {"output_path", ocr_request.output_path},
                                {"output_dir", ocr_request.output_dir},
                                {"output_paths", output_paths},
                                {"format", ocr_request.format},
                                {"exportType", ocr_request.export_type},
                                {"provider", providers_.ocr_provider ? providers_.ocr_provider->ProviderName() : ""}});
}

Json CommandApplicationService::HandleOcrGet(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    SdkOcrGetRequest get_request;
    get_request.task_id = GetOptionalStringField(request.params, "task_id");
    if (get_request.task_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "task_id required");
    }
    const SdkOcrGetResult result = ocr_facade_.GetTask(get_request);
    if (!IsOkStatusCode(result.code)) {
        return BuildWsResponse(request.request_id, result.code, result.message);
    }
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"task", BuildOcrTaskJson(result.task)},
                                {"provider", providers_.ocr_provider ? providers_.ocr_provider->ProviderName() : ""}});
}

Json CommandApplicationService::HandleOcrCancel(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    SdkOcrCancelRequest cancel_request;
    cancel_request.task_id = GetOptionalStringField(request.params, "task_id");
    if (cancel_request.task_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "task_id required");
    }
    const SdkOcrCancelResult result = ocr_facade_.Cancel(cancel_request);
    if (!IsOkStatusCode(result.code)) {
        return BuildWsResponse(request.request_id, result.code, result.message);
    }
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"cancelled", result.cancelled},
                                {"task", BuildOcrTaskJson(result.task)},
                                {"provider", providers_.ocr_provider ? providers_.ocr_provider->ProviderName() : ""}});
}

Json CommandApplicationService::HandleOcrExtractText(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }

    SdkOcrExtractTextRequest extract_request;
    extract_request.input_upload_id = GetOptionalStringField(request.params, "input_upload_id");
    extract_request.input_path = GetOptionalStringField(request.params, "input_path");
    if (!extract_request.input_upload_id.empty()) {
        AssetAccessResult input_asset = ResolveImageAsset(connection_id, extract_request.input_upload_id, "asset-original");
        if (!IsOkStatusCode(input_asset.code)) {
            return BuildWsResponse(request.request_id, input_asset.code, input_asset.message);
        }
        extract_request.input_path = input_asset.asset.path;
    }
    if (extract_request.input_path.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "input_upload_id or input_path required");
    }
    if (!FileExists(extract_request.input_path)) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "input file not found");
    }

    const SdkOcrExtractTextResult result = ocr_facade_.ExtractText(extract_request);
    if (!IsOkStatusCode(result.code)) {
        return BuildWsResponse(request.request_id, result.code, result.message);
    }
    Json blocks = Json::array();
    for (std::vector<SdkOcrTextBlock>::const_iterator it = result.blocks.begin(); it != result.blocks.end(); ++it) {
        blocks.push_back(BuildOcrTextBlockJson(*it));
    }
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"recognized", result.recognized},
                                {"input_path", result.input_path},
                                {"width", result.width},
                                {"height", result.height},
                                {"blocks", blocks},
                                {"provider", providers_.ocr_provider ? providers_.ocr_provider->ProviderName() : ""}});
}

Json CommandApplicationService::HandleBarcodeDetect(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }

    SdkBarcodeDetectRequest detect_request;
    detect_request.input_upload_id = GetOptionalStringField(request.params, "input_upload_id");
    detect_request.input_path = GetOptionalStringField(request.params, "input_path");
    detect_request.formats = GetOptionalStringArrayField(request.params, "formats");
    if (detect_request.formats.empty()) {
        detect_request.formats = GetOptionalStringArrayField(request.params, "detect_type");
    }
    if (!detect_request.input_upload_id.empty()) {
        AssetAccessResult input_asset = ResolveImageAsset(connection_id, detect_request.input_upload_id, "asset-original");
        if (!IsOkStatusCode(input_asset.code)) {
            return BuildWsResponse(request.request_id, input_asset.code, input_asset.message);
        }
        detect_request.input_path = input_asset.asset.path;
    }
    if (detect_request.input_path.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "input_upload_id or input_path required");
    }
    if (!FileExists(detect_request.input_path)) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "input file not found");
    }

    const SdkBarcodeDetectResult result = recognition_facade_.DetectBarcode(detect_request);
    if (!IsOkStatusCode(result.code)) {
        return BuildWsResponse(request.request_id, result.code, result.message);
    }
    Json barcodes = Json::array();
    for (std::vector<SdkBarcodeResult>::const_iterator it = result.barcodes.begin(); it != result.barcodes.end(); ++it) {
        barcodes.push_back(BuildBarcodeJson(*it));
    }
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"detected", result.detected},
                                {"count", result.barcodes.size()},
                                {"input_path", result.input_path},
                                {"width", result.width},
                                {"height", result.height},
                                {"barcodes", barcodes},
                                {"provider", providers_.recognition_provider ? providers_.recognition_provider->ProviderName() : ""}});
}

Json CommandApplicationService::HandleSaneStatus(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    const SdkSaneStatusResult result = sane_facade_.GetStatus();
    return BuildWsResponse(request.request_id,
                           result.code,
                           result.message,
                           BuildSaneStatusJson(result, providers_.sane_provider ? providers_.sane_provider->ProviderName() : ""));
}

Json CommandApplicationService::HandleSaneList(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    SdkSaneListRequest list_request;
    list_request.refresh = GetOptionalBoolField(request.params, "refresh", true);
    list_request.include_detected = GetOptionalBoolField(request.params, "include_detected", false);
    const SdkSaneListResult result = sane_facade_.ListDevices(list_request);
    Json payload{{"devices", BuildSaneDevicesJson(result.devices)},
                 {"count", result.devices.size()},
                 {"generation", result.generation},
                 {"provider", providers_.sane_provider ? providers_.sane_provider->ProviderName() : ""}};
    if (list_request.include_detected) {
        payload["detected_devices"] = BuildSaneDevicesJson(result.detected_devices);
        payload["detected_count"] = result.detected_devices.size();
    }
    return BuildWsResponse(request.request_id,
                           result.code,
                           result.message,
                           payload);
}

Json CommandApplicationService::HandleSaneWatchStart(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    SdkSaneWatchRequest watch_request;
    watch_request.connection_id = connection_id;
    watch_request.enabled = true;
    const SdkSaneWatchResult result = sane_facade_.WatchStart(watch_request);
    return BuildWsResponse(request.request_id,
                           result.code,
                           result.message,
                           Json{{"watching", result.watching},
                                {"generation", result.generation},
                                {"provider", providers_.sane_provider ? providers_.sane_provider->ProviderName() : ""}});
}

Json CommandApplicationService::HandleSaneWatchStop(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    SdkSaneWatchRequest watch_request;
    watch_request.connection_id = connection_id;
    watch_request.enabled = false;
    const SdkSaneWatchResult result = sane_facade_.WatchStop(watch_request);
    return BuildWsResponse(request.request_id,
                           result.code,
                           result.message,
                           Json{{"watching", result.watching},
                                {"generation", result.generation},
                                {"provider", providers_.sane_provider ? providers_.sane_provider->ProviderName() : ""}});
}

Json CommandApplicationService::HandleSaneOpen(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    SdkSaneOpenRequest open_request;
    open_request.device_id = GetOptionalStringField(request.params, "device_id");
    open_request.device_name = GetOptionalStringField(request.params, "device_name");
    open_request.profile_id = GetOptionalStringField(request.params, "profile_id");
    const Json options = request.params.find("options") == request.params.end() ? Json() : request.params["options"];
    const std::vector<SdkSaneOptionSetItem> items = ParseSaneOptionSetItems(options);
    for (std::vector<SdkSaneOptionSetItem>::const_iterator it = items.begin(); it != items.end(); ++it) {
        open_request.option_keys.push_back(!it->key.empty() ? it->key : std::to_string(it->index));
        open_request.option_values_json.push_back(it->value_json);
    }
    if (open_request.device_id.empty() && open_request.device_name.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "device_id or device_name required");
    }
    const SdkSaneOpenResult result = sane_facade_.OpenDevice(open_request);
    return BuildWsResponse(request.request_id,
                           result.code,
                           result.message,
                           Json{{"opened", result.opened},
                                {"session_id", result.session_id},
                                {"device", BuildSaneDeviceJson(result.device)},
                                {"provider", providers_.sane_provider ? providers_.sane_provider->ProviderName() : ""}});
}

Json CommandApplicationService::HandleSaneClose(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    SdkSaneCloseRequest close_request;
    close_request.session_id = GetOptionalStringField(request.params, "session_id");
    if (close_request.session_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "session_id required");
    }
    const SdkSaneCloseResult result = sane_facade_.CloseDevice(close_request);
    return BuildWsResponse(request.request_id,
                           result.code,
                           result.message,
                           Json{{"closed", result.closed}, {"was_opened", result.was_opened}});
}

Json CommandApplicationService::HandleSaneGetOptions(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    SdkSaneGetOptionsRequest options_request;
    options_request.session_id = GetOptionalStringField(request.params, "session_id");
    if (options_request.session_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "session_id required");
    }
    const SdkSaneGetOptionsResult result = sane_facade_.GetOptions(options_request);
    Json options = Json::array();
    for (std::vector<SdkSaneOption>::const_iterator it = result.options.begin(); it != result.options.end(); ++it) {
        options.push_back(BuildSaneOptionJson(*it));
    }
    return BuildWsResponse(request.request_id,
                           result.code,
                           result.message,
                           Json{{"options", options},
                                {"count", result.options.size()},
                                {"provider", providers_.sane_provider ? providers_.sane_provider->ProviderName() : ""}});
}

Json CommandApplicationService::HandleSaneSetOptions(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    SdkSaneSetOptionsRequest set_request;
    set_request.session_id = GetOptionalStringField(request.params, "session_id");
    const Json options = request.params.find("options") == request.params.end() ? Json() : request.params["options"];
    set_request.options = ParseSaneOptionSetItems(options);
    if (set_request.session_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "session_id required");
    }
    if (set_request.options.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "options required");
    }
    const SdkSaneSetOptionsResult result = sane_facade_.SetOptions(set_request);
    Json results = Json::array();
    for (std::vector<SdkSaneOptionSetResultItem>::const_iterator it = result.results.begin(); it != result.results.end(); ++it) {
        results.push_back(BuildSaneOptionSetResultJson(*it));
    }
    return BuildWsResponse(request.request_id,
                           result.code,
                           result.message,
                           Json{{"applied", result.applied},
                                {"requires_reload", result.requires_reload},
                                {"results", results},
                                {"provider", providers_.sane_provider ? providers_.sane_provider->ProviderName() : ""}});
}

Json CommandApplicationService::HandleSaneProfileList(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    const SdkSaneProfileListResult result = sane_facade_.ListProfiles(ParseSaneProfileRequest(request.params));
    Json profiles = Json::array();
    for (std::vector<SdkSaneProfile>::const_iterator it = result.profiles.begin(); it != result.profiles.end(); ++it) {
        profiles.push_back(BuildSaneProfileJson(*it));
    }
    return BuildWsResponse(request.request_id,
                           result.code,
                           result.message,
                           Json{{"profiles", profiles},
                                {"count", result.profiles.size()},
                                {"provider", providers_.sane_provider ? providers_.sane_provider->ProviderName() : ""}});
}

Json CommandApplicationService::HandleSaneProfileSave(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    SdkSaneProfileRequest profile_request = ParseSaneProfileRequest(request.params);
    if (profile_request.name.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "name required");
    }
    const SdkSaneProfileResult result = sane_facade_.SaveProfile(profile_request);
    return BuildWsResponse(request.request_id,
                           result.code,
                           result.message,
                           Json{{"saved", result.saved},
                                {"profile", BuildSaneProfileJson(result.profile)},
                                {"provider", providers_.sane_provider ? providers_.sane_provider->ProviderName() : ""}});
}

Json CommandApplicationService::HandleSaneProfileApply(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    SdkSaneProfileRequest profile_request = ParseSaneProfileRequest(request.params);
    if (profile_request.profile_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "profile_id required");
    }
    const SdkSaneProfileResult result = sane_facade_.ApplyProfile(profile_request);
    return BuildWsResponse(request.request_id,
                           result.code,
                           result.message,
                           Json{{"applied", result.applied},
                                {"profile", BuildSaneProfileJson(result.profile)},
                                {"provider", providers_.sane_provider ? providers_.sane_provider->ProviderName() : ""}});
}

Json CommandApplicationService::HandleSaneProfileDelete(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    SdkSaneProfileRequest profile_request = ParseSaneProfileRequest(request.params);
    if (profile_request.profile_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "profile_id required");
    }
    const SdkSaneProfileResult result = sane_facade_.DeleteProfile(profile_request);
    return BuildWsResponse(request.request_id,
                           result.code,
                           result.message,
                           Json{{"deleted", result.deleted},
                                {"profile", BuildSaneProfileJson(result.profile)},
                                {"provider", providers_.sane_provider ? providers_.sane_provider->ProviderName() : ""}});
}

Json CommandApplicationService::HandleSaneScan(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = ConsumeQuota(connection_id, request.method, request.request_id);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    SdkSaneScanRequest scan_request;
    scan_request.connection_id = connection_id;
    scan_request.session_id = GetOptionalStringField(request.params, "session_id");
    scan_request.output_type = GetOptionalStringField(request.params, "output_type");
    scan_request.output_format = GetOptionalStringField(request.params, "output_format");
    scan_request.output_path = GetOptionalStringField(request.params, "output_path");
    scan_request.output_dir = GetOptionalStringField(request.params, "output_dir");
    scan_request.export_type = NormalizeExportType(GetOptionalStringField(request.params, "export_type"));
    const Json pipeline_json = GetOptionalObjectField(request.params, "pipeline");
    const SdkImageEnhancePipeline scan_pipeline = ParseImageEnhancePipeline(pipeline_json);
    const Json output = GetOptionalObjectField(request.params, "output");
    if (!output.empty()) {
        const std::string output_type = GetOptionalStringField(output, "type");
        const std::string output_format = GetOptionalStringField(output, "format");
        const std::string output_path = GetOptionalStringField(output, "path");
        const std::string output_dir = GetOptionalStringField(output, "dir");
        const std::string export_type = GetOptionalStringField(output, "export_type");
        if (!output_type.empty()) scan_request.output_type = output_type;
        if (!output_format.empty()) scan_request.output_format = output_format;
        if (!output_path.empty()) scan_request.output_path = output_path;
        if (!output_dir.empty()) scan_request.output_dir = output_dir;
        if (!export_type.empty()) scan_request.export_type = NormalizeExportType(export_type);
    }
    if (scan_request.output_type.empty()) {
        scan_request.output_type = "images";
    }
    if (scan_request.output_format.empty()) {
        scan_request.output_format = scan_request.output_type == "images" ? "jpg" : scan_request.output_type;
    }
    if (scan_request.export_type.empty()) {
        scan_request.export_type = "multi-page";
    }
    const Json pipeline_target_json = GetOptionalObjectField(pipeline_json, "target");
    if (!scan_pipeline.steps.empty() && !GetOptionalStringField(pipeline_target_json, "type").empty()) {
        if (scan_pipeline.target.type == "pdf" ||
            scan_pipeline.target.type == "ofd" ||
            scan_pipeline.target.type == "tiff" ||
            scan_pipeline.target.type == "images" ||
            scan_pipeline.target.type == "jpg" ||
            scan_pipeline.target.type == "png") {
            scan_request.output_type = scan_pipeline.target.type == "jpg" || scan_pipeline.target.type == "png"
                                           ? "images"
                                           : scan_pipeline.target.type;
            scan_request.output_format = scan_pipeline.target.type == "images"
                                             ? scan_pipeline.target.format
                                             : scan_request.output_type;
            if (!scan_pipeline.target.export_type.empty()) {
                scan_request.export_type = NormalizeExportType(scan_pipeline.target.export_type);
            }
            if (!scan_pipeline.target.output_path.empty()) {
                scan_request.output_path = scan_pipeline.target.output_path;
            }
            if (!scan_pipeline.target.output_dir.empty()) {
                scan_request.output_dir = scan_pipeline.target.output_dir;
            }
        }
    }
    const Json options = request.params.find("options") == request.params.end() ? Json() : request.params["options"];
    scan_request.options = ParseSaneOptionSetItems(options);
    if (scan_request.session_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "session_id required");
    }
    SdkSaneScanResult result = sane_facade_.Scan(scan_request);
    if (!result.task_id.empty()) {
        EnsureSaneImageAssets(&result.task);
        for (std::vector<SdkCaptureAsset>::iterator it = result.task.assets.begin();
             it != result.task.assets.end();
             ++it) {
            *it = AttachImageAssetUrls(result.task_id, *it);
            RegisterImageAsset(connection_id, result.task_id, *it);
        }
        std::lock_guard<std::mutex> lock(sane_tasks_mu_);
        sane_tasks_[result.task_id] = result.task;
        if (!scan_pipeline.steps.empty()) {
            sane_pipelines_by_task_[result.task_id] = scan_pipeline;
            sane_online_api_keys_by_task_[result.task_id] = session_result.token;
            sane_online_base_urls_by_task_[result.task_id] = runtime_config_ ? runtime_config_->OnlineImageEnhanceBaseUrl() : "";
        }
    }
    return BuildWsResponse(request.request_id,
                           result.code,
                           result.message,
                           Json{{"accepted", result.accepted},
                                {"task_id", result.task_id},
                                {"task", BuildSaneScanTaskJson(result.task)},
                                {"provider", providers_.sane_provider ? providers_.sane_provider->ProviderName() : ""}});
}

Json CommandApplicationService::HandleSaneScanGet(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    SdkSaneScanGetRequest get_request;
    get_request.task_id = GetOptionalStringField(request.params, "task_id");
    if (get_request.task_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "task_id required");
    }
    {
        std::lock_guard<std::mutex> lock(sane_tasks_mu_);
        std::map<std::string, SdkSaneScanTask>::iterator it = sane_tasks_.find(get_request.task_id);
        if (it != sane_tasks_.end()) {
            EnsureSaneImageAssets(&it->second);
            for (std::vector<SdkCaptureAsset>::iterator asset_it = it->second.assets.begin();
                 asset_it != it->second.assets.end();
                 ++asset_it) {
                *asset_it = AttachImageAssetUrls(get_request.task_id, *asset_it);
                RegisterImageAsset(connection_id, get_request.task_id, *asset_it);
            }
            return BuildWsResponse(request.request_id,
                                   SdkStatusCode::Ok,
                                   "ok",
                                   Json{{"task_id", get_request.task_id},
                                        {"accepted", true},
                                        {"task", BuildSaneScanTaskJson(it->second)},
                                        {"provider", providers_.sane_provider ? providers_.sane_provider->ProviderName() : ""}});
        }
    }
    SdkSaneScanResult result = sane_facade_.GetScan(get_request);
    EnsureSaneImageAssets(&result.task);
    for (std::vector<SdkCaptureAsset>::iterator it = result.task.assets.begin();
         it != result.task.assets.end();
         ++it) {
        *it = AttachImageAssetUrls(result.task_id, *it);
        RegisterImageAsset(connection_id, result.task_id, *it);
    }
    return BuildWsResponse(request.request_id,
                           result.code,
                           result.message,
                           Json{{"accepted", result.accepted},
                                {"task_id", result.task_id},
                                {"task", BuildSaneScanTaskJson(result.task)},
                                {"provider", providers_.sane_provider ? providers_.sane_provider->ProviderName() : ""}});
}

Json CommandApplicationService::HandleSaneScanCancel(const std::string& connection_id, const Request& request) {
    const AuthorizationService::SessionResult session_result = RequireCapability(connection_id, request.method);
    if (!IsOkStatusCode(session_result.code)) {
        return BuildWsResponse(request.request_id, session_result.code, session_result.message);
    }
    SdkSaneScanCancelRequest cancel_request;
    cancel_request.task_id = GetOptionalStringField(request.params, "task_id");
    if (cancel_request.task_id.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "task_id required");
    }
    const SdkSaneScanResult result = sane_facade_.CancelScan(cancel_request);
    {
        std::lock_guard<std::mutex> lock(sane_tasks_mu_);
        std::map<std::string, SdkSaneScanTask>::iterator it = sane_tasks_.find(cancel_request.task_id);
        if (it != sane_tasks_.end()) {
            it->second.status = "cancelled";
            it->second.message = "cancelled";
        }
    }
    return BuildWsResponse(request.request_id,
                           result.code,
                           result.message,
                           Json{{"accepted", result.accepted},
                                {"task_id", result.task_id},
                                {"task", BuildSaneScanTaskJson(result.task)},
                                {"provider", providers_.sane_provider ? providers_.sane_provider->ProviderName() : ""}});
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
    bool export_type_set = false;
    const Json source_json = GetOptionalObjectField(request.params, "source");
    const Json target_json = GetOptionalObjectField(request.params, "target");
    const Json options_json = GetOptionalObjectField(request.params, "options");

    convert_request.input_upload_id = GetOptionalStringField(request.params, "input_upload_id");
    convert_request.input_upload_ids = GetOptionalStringArrayField(request.params, "input_upload_ids");
    convert_request.input_path = GetOptionalStringField(request.params, "input_path");
    convert_request.input_paths = GetOptionalStringArrayField(request.params, "input_paths");
    convert_request.output_path = GetOptionalStringField(request.params, "output_path");
    convert_request.output_dir = GetOptionalStringAnyField(request.params, {"output_dir", "target_dir"}, "");
    convert_request.output_format = NormalizeImageFormat(GetOptionalStringField(request.params, "output_format"));
    const std::string flat_export_type = GetOptionalStringAnyField(request.params, {"export_type", "exportType"}, "");
    if (!flat_export_type.empty()) {
        convert_request.export_type = NormalizeExportType(flat_export_type);
        export_type_set = true;
    }
    const std::string flat_pages = GetOptionalStringField(request.params, "pages");
    if (!flat_pages.empty()) {
        convert_request.pages = flat_pages;
    }

    if (!source_json.empty()) {
        convert_request.source_type = NormalizeImageFormat(GetOptionalStringField(source_json, "type"));
        convert_request.source_format = NormalizeImageFormat(GetOptionalStringField(source_json, "format"));
        if (!GetOptionalStringField(source_json, "input_upload_id").empty()) {
            convert_request.input_upload_id = GetOptionalStringField(source_json, "input_upload_id");
        }
        const std::vector<std::string> upload_ids = GetOptionalStringArrayField(source_json, "input_upload_ids");
        if (!upload_ids.empty()) {
            convert_request.input_upload_ids = upload_ids;
        }
        if (!GetOptionalStringField(source_json, "input_path").empty()) {
            convert_request.input_path = GetOptionalStringField(source_json, "input_path");
        }
        const std::vector<std::string> input_paths = GetOptionalStringArrayField(source_json, "input_paths");
        if (!input_paths.empty()) {
            convert_request.input_paths = input_paths;
        }
        const std::string source_pages = GetOptionalStringField(source_json, "pages");
        if (!source_pages.empty()) {
            convert_request.pages = source_pages;
        }
    }
    if (convert_request.source_type.empty()) {
        convert_request.source_type = "image";
    }

    if (!target_json.empty()) {
        convert_request.target_type = NormalizeImageFormat(GetOptionalStringField(target_json, "type"));
        if (!GetOptionalStringField(target_json, "path").empty()) {
            convert_request.output_path = GetOptionalStringField(target_json, "path");
        }
        if (!GetOptionalStringField(target_json, "dir").empty()) {
            convert_request.output_dir = GetOptionalStringField(target_json, "dir");
        }
    }
    if (!options_json.empty()) {
        const std::string export_type = GetOptionalStringField(options_json, "export_type");
        if (!export_type.empty()) {
            convert_request.export_type = NormalizeExportType(export_type);
            export_type_set = true;
        }
        convert_request.quality = GetOptionalIntField(options_json, "quality", convert_request.quality);
        convert_request.render_dpi = GetOptionalIntField(options_json, "render_dpi", convert_request.render_dpi);
        const std::string option_pages = GetOptionalStringField(options_json, "pages");
        if (!option_pages.empty()) {
            convert_request.pages = option_pages;
        }
        const std::string tiff_color = GetOptionalStringField(options_json, "tiff_color");
        if (!tiff_color.empty()) {
            convert_request.tiff_color = NormalizeLower(tiff_color);
        }
        const std::string tiff_compression = GetOptionalStringField(options_json, "tiff_compression");
        if (!tiff_compression.empty()) {
            convert_request.tiff_compression = NormalizeLower(tiff_compression);
        }
    }

    if (convert_request.output_format.empty() && !convert_request.target_type.empty()) {
        convert_request.output_format = convert_request.target_type;
    }
    if (convert_request.output_format.empty() && !convert_request.output_path.empty()) {
        convert_request.output_format = NormalizeImageFormat(InferOutputFormatFromPath(convert_request.output_path));
    }
    convert_request.target_type = convert_request.output_format;

    if (!export_type_set) {
        convert_request.export_type = DefaultExportTypeForFileConvert(convert_request.output_format);
    }
    if (convert_request.export_type != "multi-page" && convert_request.export_type != "single-page") {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "export_type must be multi-page or single-page");
    }
    if (convert_request.output_format.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "target.type, output_format, or output_path required");
    }
    if (!IsFileConvertFormat(convert_request.output_format)) {
        return BuildWsResponse(request.request_id,
                               SdkStatusCode::UnsupportedMethod,
                               "unsupported output format: " + convert_request.output_format);
    }

    std::string image_task_id = convert_request.input_upload_id;
    if (image_task_id.empty()) {
        image_task_id = NextImageTaskId();
    }
    if (!convert_request.input_upload_id.empty()) {
        if (std::find(convert_request.input_upload_ids.begin(),
                      convert_request.input_upload_ids.end(),
                      convert_request.input_upload_id) == convert_request.input_upload_ids.end()) {
            convert_request.input_upload_ids.insert(convert_request.input_upload_ids.begin(), convert_request.input_upload_id);
        }
    }
    for (std::vector<std::string>::const_iterator it = convert_request.input_upload_ids.begin();
         it != convert_request.input_upload_ids.end();
         ++it) {
        AssetAccessResult input_asset = ResolveImageAsset(connection_id, *it, "asset-original");
        if (!IsOkStatusCode(input_asset.code)) {
            return BuildWsResponse(request.request_id, input_asset.code, input_asset.message);
        }
        convert_request.input_paths.push_back(input_asset.asset.path);
    }

    if (!source_json.empty() || request.params.find("base64") != request.params.end()) {
        const std::string base64_payload =
            !GetOptionalStringField(source_json, "base64").empty()
                ? GetOptionalStringField(source_json, "base64")
                : GetOptionalStringField(request.params, "base64");
        if (!base64_payload.empty()) {
            std::string decoded;
            if (!DecodeBase64ImagePayload(base64_payload, &decoded)) {
                return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "invalid base64 image");
            }
            const std::string input_dir = GetSdkOpenTaskAssetDir("file", image_task_id, "input");
            if (!EnsureDirectoryRecursive(input_dir)) {
                return BuildWsResponse(request.request_id, SdkStatusCode::InternalError, "failed to create file convert input directory");
            }
            const std::string base64_input_path = JoinLocalPath(input_dir, "base64." + DataUrlImageExtension(base64_payload));
            if (!WriteBinaryFile(base64_input_path, decoded)) {
                return BuildWsResponse(request.request_id, SdkStatusCode::InternalError, "failed to write base64 image");
            }
            convert_request.source_type = "base64";
            convert_request.input_paths.push_back(base64_input_path);
        }
    }

    if (!convert_request.input_path.empty() &&
        std::find(convert_request.input_paths.begin(), convert_request.input_paths.end(), convert_request.input_path) == convert_request.input_paths.end()) {
        convert_request.input_paths.insert(convert_request.input_paths.begin(), convert_request.input_path);
    }
    if (convert_request.input_paths.empty()) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "input_path, input_paths, input_upload_id, input_upload_ids, or source.base64 required");
    }
    convert_request.input_path = convert_request.input_paths.front();

    if (convert_request.source_format.empty()) {
        convert_request.source_format = IsFileConvertDocumentSourceFormat(convert_request.source_type)
                                            ? convert_request.source_type
                                            : NormalizeImageFormat(ExtensionFromFilename(convert_request.input_path));
    }
    if (IsFileConvertDocumentSourceFormat(convert_request.source_format) &&
        IsFileConvertImageSourceType(convert_request.source_type) &&
        convert_request.input_upload_ids.empty() &&
        convert_request.input_upload_id.empty()) {
        convert_request.source_type = convert_request.source_format;
    }

    if (!IsFileConvertSourceType(convert_request.source_type)) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "source.type must be image, images, base64, pdf, ofd, or tiff");
    }
    if (IsFileConvertDocumentSourceFormat(convert_request.source_type) && convert_request.input_paths.size() > 1) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "pdf, ofd, and tiff document sources accept one input file");
    }

    for (std::vector<std::string>::const_iterator it = convert_request.input_paths.begin();
         it != convert_request.input_paths.end();
         ++it) {
        if (!FileExists(*it)) {
            return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "input file does not exist: " + *it);
        }
        const std::string input_extension = NormalizeImageFormat(ExtensionFromFilename(*it));
        if (IsFileConvertImageSourceType(convert_request.source_type)) {
            if (!IsSupportedImageExtension(input_extension)) {
                return BuildWsResponse(request.request_id, SdkStatusCode::UnsupportedMethod, "unsupported image input format: " + input_extension);
            }
        } else if (input_extension != convert_request.source_type) {
            return BuildWsResponse(request.request_id,
                                   SdkStatusCode::UnsupportedMethod,
                                   "source.type does not match input extension: " + input_extension);
        }
    }
    if ((convert_request.output_format == "jpg" || convert_request.output_format == "png") &&
        convert_request.export_type == "multi-page") {
        return BuildWsResponse(request.request_id, SdkStatusCode::InvalidParams, "jpg and png targets only support export_type=single-page");
    }
    if (convert_request.output_dir.empty()) {
        convert_request.output_dir = GetSdkOpenTaskAssetDir("file", image_task_id, "assets");
    }
    if (!EnsureDirectoryRecursive(convert_request.output_dir)) {
        return BuildWsResponse(request.request_id, SdkStatusCode::InternalError, "failed to create file convert output directory");
    }
    if (convert_request.output_path.empty() && convert_request.export_type == "multi-page") {
        convert_request.output_path =
            JoinLocalPath(convert_request.output_dir,
                          "converted." + ExtensionForFileConvertFormat(convert_request.output_format));
    }

    const bool use_graphic_convert =
        IsFileConvertImageSourceType(convert_request.source_type) &&
        convert_request.input_paths.size() == 1 &&
        IsGraphicConvertFormat(convert_request.output_format) &&
        convert_request.export_type == "single-page";
    if (use_graphic_convert) {
        if (convert_request.output_path.empty()) {
            convert_request.output_path =
                JoinLocalPath(convert_request.output_dir, "converted." + ExtensionForImageFormat(convert_request.output_format));
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
                                    {"input_upload_ids", convert_request.input_upload_ids},
                                    {"input_path", convert_request.input_path},
                                    {"input_paths", convert_request.input_paths},
                                    {"output_path", final_path},
                                    {"output_paths", Json::array({final_path})},
                                    {"output_format", convert_request.output_format},
                                    {"export_type", convert_request.export_type},
                                    {"source_format", convert_request.source_format},
                                    {"source_page_count", 1},
                                    {"selected_page_count", 1},
                                    {"accepted", 1},
                                    {"converted", result.converted ? 1 : 0},
                                    {"asset", BuildAssetJson(asset)},
                                    {"assets", Json::array({BuildAssetJson(asset)})},
                                    {"outputs", Json::array({Json{{"path", final_path},
                                                                  {"format", convert_request.output_format},
                                                                  {"asset", BuildAssetJson(asset)}}})},
                                    {"provider", providers_.graphic_provider ? providers_.graphic_provider->ProviderName() : ""}});
    }

    const SdkFileConvertResult result = ofd_facade_.Convert(convert_request);
    if (!IsOkStatusCode(result.code)) {
        return BuildWsResponse(request.request_id, result.code, result.message);
    }
    std::vector<std::string> output_paths = result.output_paths;
    if (output_paths.empty() && !result.output_path.empty()) {
        output_paths.push_back(result.output_path);
    }
    if (output_paths.empty() && !convert_request.output_path.empty()) {
        output_paths.push_back(convert_request.output_path);
    }
    Json output_paths_json = Json::array();
    Json assets_json = Json::array();
    Json outputs_json = Json::array();
    for (std::size_t i = 0; i < output_paths.size(); ++i) {
        output_paths_json.push_back(output_paths[i]);
        SdkCaptureAsset asset;
        std::ostringstream asset_id;
        asset_id << "asset-converted";
        if (output_paths.size() > 1) {
            asset_id << "-" << std::setw(3) << std::setfill('0') << (i + 1);
        }
        asset.asset_id = asset_id.str();
        asset.kind = "converted";
        asset.path = output_paths[i];
        asset.content_type = ContentTypeForFileConvertFormat(convert_request.output_format);
        asset.size = FileSize(output_paths[i]);
        asset = AttachImageAssetUrls(image_task_id, asset);
        RegisterImageAsset(connection_id, image_task_id, asset);
        const Json asset_json = BuildAssetJson(asset);
        assets_json.push_back(asset_json);
        outputs_json.push_back(Json{{"path", output_paths[i]},
                                    {"format", convert_request.output_format},
                                    {"asset", asset_json}});
    }
    const std::string final_output_path =
        !result.output_path.empty() ? result.output_path : (!output_paths.empty() ? output_paths.front() : convert_request.output_path);
    return BuildWsResponse(request.request_id,
                           SdkStatusCode::Ok,
                           "ok",
                           Json{{"input_path", convert_request.input_path},
                                {"input_paths", convert_request.input_paths},
                                {"output_path", final_output_path},
                                {"output_paths", output_paths_json},
                                {"output_format", convert_request.output_format},
                                {"export_type", convert_request.export_type},
                                {"source_format", result.source_format.empty() ? convert_request.source_format : result.source_format},
                                {"source_page_count", result.source_page_count},
                                {"selected_page_count", result.selected_page_count},
                                {"accepted", result.accepted},
                                {"converted", result.converted},
                                {"asset", assets_json.empty() ? Json::object() : assets_json.front()},
                                {"assets", assets_json},
                                {"outputs", outputs_json},
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
