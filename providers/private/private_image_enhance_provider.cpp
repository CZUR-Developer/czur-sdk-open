// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "private_image_enhance_provider.h"

#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <sys/stat.h>
#include <sys/types.h>

#if defined(_WIN32)
#include <direct.h>
#endif

#include <opencv2/opencv.hpp>
#include "czcv/czcv_all_header.hpp"
#include "czcv_cfg_compat.h"
#include "graphic/czcv_result_ownership.h"
#include "graphic/graphic_algorithm_runtime.h"
#include "graphic/image_codec_lock.h"
#include "imgengine.h"
#include "image_write_utils.h"
#include "nlohmann/json.hpp"
#include "online_image_enhance_client.h"

namespace editor {
namespace sdk {
namespace private_impl {

namespace {

char PathSeparator() {
#if defined(_WIN32)
    return '\\';
#else
    return '/';
#endif
}

bool IsPathSeparator(char ch) {
    return ch == '/' || ch == '\\';
}

bool DirectoryExists(const std::string& path) {
    struct stat st;
    return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool EnsureDirectoryRecursive(const std::string& path) {
    if (path.empty()) {
        return false;
    }
    if (DirectoryExists(path)) {
        return true;
    }
    std::string current;
    std::size_t index = 0;
    if (IsPathSeparator(path[0])) {
        current = path.substr(0, 1);
        index = 1;
#if defined(_WIN32)
    } else if (path.size() >= 3 && path[1] == ':' && IsPathSeparator(path[2])) {
        current = path.substr(0, 3);
        index = 3;
#endif
    }
    while (index <= path.size()) {
        const std::size_t next = path.find_first_of("/\\", index);
        const std::string part = path.substr(index, next == std::string::npos ? std::string::npos : next - index);
        if (!part.empty()) {
            current = current.empty() || IsPathSeparator(current[current.size() - 1])
                          ? current + part
                          : current + PathSeparator() + part;
#if defined(_WIN32)
            if (!DirectoryExists(current) && ::_mkdir(current.c_str()) != 0 && !DirectoryExists(current)) {
#else
            if (!DirectoryExists(current) && ::mkdir(current.c_str(), 0755) != 0 && !DirectoryExists(current)) {
#endif
                return false;
            }
        }
        if (next == std::string::npos) {
            break;
        }
        index = next + 1;
    }
    return DirectoryExists(path);
}

std::string JoinPath(const std::string& dir, const std::string& name) {
    if (dir.empty() || dir == ".") {
        return name;
    }
    if (IsPathSeparator(dir[dir.size() - 1])) {
        return dir + name;
    }
    return dir + PathSeparator() + name;
}

std::string ExtensionFromPath(const std::string& path) {
    const std::string::size_type slash_pos = path.find_last_of("/\\");
    const std::string leaf = slash_pos == std::string::npos ? path : path.substr(slash_pos + 1);
    const std::string::size_type dot_pos = leaf.find_last_of('.');
    if (dot_pos == std::string::npos || dot_pos + 1 >= leaf.size()) {
        return "jpg";
    }
    std::string extension = leaf.substr(dot_pos + 1);
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (extension == "jpeg") {
        return "jpg";
    }
    if (extension == "tif") {
        return "tiff";
    }
    return extension;
}

std::string OutputPath(const std::string& output_dir, const std::string& step_type, int index, const std::string& input_path) {
    std::ostringstream name;
    name << step_type << "-" << std::setw(3) << std::setfill('0') << index << "." << ExtensionFromPath(input_path);
    return JoinPath(output_dir, name.str());
}

std::string OutputPathWithExtension(const std::string& output_dir, const std::string& step_type, int index, const std::string& extension) {
    std::ostringstream name;
    name << step_type << "-" << std::setw(3) << std::setfill('0') << index << "." << extension;
    return JoinPath(output_dir, name.str());
}

bool CopyFile(const std::string& input_path, const std::string& output_path) {
    std::ifstream in(input_path.c_str(), std::ios::binary);
    std::ofstream out(output_path.c_str(), std::ios::binary);
    if (!in || !out) {
        return false;
    }
    out << in.rdbuf();
    return static_cast<bool>(out);
}

nlohmann::json ParseParams(const std::string& params_json) {
    if (params_json.empty()) {
        return nlohmann::json::object();
    }
    try {
        nlohmann::json value = nlohmann::json::parse(params_json);
        return value.is_object() ? value : nlohmann::json::object();
    } catch (...) {
        return nlohmann::json::object();
    }
}

std::string DumpJson(const nlohmann::json& value) {
    try {
        return value.dump();
    } catch (...) {
        return "{}";
    }
}

cv::Mat ReadImageLocked(const std::string& path, int flags) {
    std::lock_guard<std::mutex> lock(graphic::ImageCodecMutex());
    return cv::imread(path, flags);
}

int ResolvePageDpi(const SdkImageEnhancePage& page) {
    return ResolveImageDpi(page.path, 0);
}

bool EnsureGraphicAlgorithms(SdkImageEnhanceStepResult& result) {
    SdkStatusCode init_code = SdkStatusCode::Ok;
    std::string init_message;
    if (graphic::GraphicAlgorithmRuntime::Instance().EnsureInitialized(init_code, init_message)) {
        return true;
    }
    result.code = ToCode(init_code);
    result.message = init_message.empty() ? "graphic service initialization failed" : init_message;
    return false;
}

cv::Mat ToBgrWithWhiteBackground(const cv::Mat& source) {
    if (source.empty()) {
        return cv::Mat();
    }
    if (source.channels() == 1) {
        cv::Mat bgr;
        cv::cvtColor(source, bgr, cv::COLOR_GRAY2BGR);
        return bgr;
    }
    if (source.channels() == 3) {
        return source.clone();
    }
    if (source.channels() == 4) {
        std::vector<cv::Mat> channels;
        cv::split(source, channels);
        cv::Mat bgr;
        cv::cvtColor(source, bgr, cv::COLOR_BGRA2BGR);
        cv::Mat alpha;
        channels[3].convertTo(alpha, CV_32FC1, 1.0 / 255.0);
        cv::Mat alpha_3;
        cv::cvtColor(alpha, alpha_3, cv::COLOR_GRAY2BGR);
        cv::Mat bgr_float;
        bgr.convertTo(bgr_float, CV_32FC3);
        cv::Mat white(bgr.size(), CV_32FC3, cv::Scalar(255, 255, 255));
        cv::Mat inverse_alpha;
        cv::subtract(cv::Scalar::all(1.0), alpha_3, inverse_alpha);
        cv::Mat composed = bgr_float.mul(alpha_3) + white.mul(inverse_alpha);
        cv::Mat output;
        composed.convertTo(output, CV_8UC3);
        return output;
    }
    return source.clone();
}

double NormalizeAngle(double angle) {
    angle = std::fmod(angle, 360.0);
    if (angle < 0.0) {
        angle += 360.0;
    }
    return angle;
}

bool NearlyEqual(double lhs, double rhs) {
    return std::fabs(lhs - rhs) < 0.0001;
}

cv::Mat RotateImage(const cv::Mat& source, double raw_angle) {
    const double angle = NormalizeAngle(raw_angle);
    cv::Mat image = ToBgrWithWhiteBackground(source);
    if (image.empty()) {
        return cv::Mat();
    }
    cv::Mat rotated;
    if (NearlyEqual(angle, 0.0)) {
        rotated = image.clone();
    } else if (NearlyEqual(angle, 90.0)) {
        cv::rotate(image, rotated, cv::ROTATE_90_CLOCKWISE);
    } else if (NearlyEqual(angle, 180.0)) {
        cv::rotate(image, rotated, cv::ROTATE_180);
    } else if (NearlyEqual(angle, 270.0)) {
        cv::rotate(image, rotated, cv::ROTATE_90_COUNTERCLOCKWISE);
    } else {
        const cv::Point2f center(image.cols / 2.0f, image.rows / 2.0f);
        cv::Mat rotate_matrix = cv::getRotationMatrix2D(center, -angle, 1.0);
        const cv::Rect2f bbox = cv::RotatedRect(cv::Point2f(), image.size(), static_cast<float>(angle)).boundingRect2f();
        rotate_matrix.at<double>(0, 2) += bbox.width / 2.0 - center.x;
        rotate_matrix.at<double>(1, 2) += bbox.height / 2.0 - center.y;
        cv::warpAffine(image, rotated, rotate_matrix, bbox.size(), cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(255, 255, 255));
    }
    return rotated;
}

ImgProc::ImageStandardizeParams ParseStandardizeParams(const nlohmann::json& params) {
    ImgProc::ImageStandardizeParams parsed;
    parsed.width_mm = params.value("width", 210.0);
    parsed.height_mm = params.value("height", 297.0);
    parsed.dpi = params.value("dpi", 300);
    parsed.horizontal_align = params.value("horizontal", 1);
    parsed.vertical_align = params.value("vertical", 1);
    parsed.fill_mode = params.value("fill", 1);
    parsed.fill_value = params.value("fillValue", 1.0);
    parsed.useOriginalPaperScale = params.value("useOriginalPaperScale", false);
    parsed.originalWidth_mm = params.value("originalWidth", 0.0);
    parsed.originalHeight_mm = params.value("originalHeight", 0.0);
    parsed.quality = params.value("quality", 95);

    int r = 255;
    int g = 255;
    int b = 255;
    if (params.contains("color")) {
        const nlohmann::json color = params["color"];
        if (color.is_array() && color.size() >= 3) {
            r = color[0].get<int>();
            g = color[1].get<int>();
            b = color[2].get<int>();
        } else if (color.is_string()) {
            const std::string value = color.get<std::string>();
            if (value.find("rgb(") == 0 && !value.empty() && value[value.size() - 1] == ')') {
                std::string values = value.substr(4, value.size() - 5);
                std::istringstream stream(values);
                std::string token;
                std::vector<int> rgb;
                while (std::getline(stream, token, ',')) {
                    token.erase(0, token.find_first_not_of(" \t"));
                    token.erase(token.find_last_not_of(" \t") + 1);
                    if (!token.empty()) {
                        rgb.push_back(std::atoi(token.c_str()));
                    }
                }
                if (rgb.size() >= 3) {
                    r = rgb[0];
                    g = rgb[1];
                    b = rgb[2];
                }
            }
        }
    }
    parsed.background_color[0] = r;
    parsed.background_color[1] = g;
    parsed.background_color[2] = b;
    return parsed;
}

czcv::FileEnhanceParams ParseFileEnhanceParams(const nlohmann::json& params, const cv::Size& image_size) {
    czcv::FileEnhanceParams parsed;
    parsed.auto_flag = params.value("auto_flag", true);
    parsed.stroke_width = params.value("stroke_width", 0);
    parsed.blur_level = params.value("blur_level", 0);
    parsed.color_thresh = params.value("color_thresh", 50);
    parsed.texture_degree = params.value("texture_degree", 1.2f);
    parsed.specify_color = params.value("specify_color", false);

    auto parse_scalar = [](const nlohmann::json& value, const cv::Scalar& fallback) {
        if (value.is_array() && value.size() >= 3) {
            return cv::Scalar(value[0].get<double>(), value[1].get<double>(), value[2].get<double>());
        }
        return fallback;
    };
    parsed.target_color = params.contains("target_color")
        ? parse_scalar(params["target_color"], cv::Scalar(67, 67, 222))
        : cv::Scalar(67, 67, 222);
    parsed.src_color = params.contains("src_color")
        ? parse_scalar(params["src_color"], cv::Scalar(0, 0, 0))
        : cv::Scalar(0, 0, 0);

    parsed.rois.clear();
    if (params.contains("rois") && params["rois"].is_array()) {
        for (nlohmann::json::const_iterator it = params["rois"].begin(); it != params["rois"].end(); ++it) {
            if (!it->is_array() || it->size() < 4) {
                continue;
            }
            const int x = std::max(0, (*it)[0].get<int>());
            const int y = std::max(0, (*it)[1].get<int>());
            const int width = std::max(0, (*it)[2].get<int>());
            const int height = std::max(0, (*it)[3].get<int>());
            cv::Rect roi(x, y, width, height);
            roi &= cv::Rect(0, 0, image_size.width, image_size.height);
            if (roi.width > 0 && roi.height > 0) {
                parsed.rois.push_back(roi);
            }
        }
    }
    return parsed;
}

SdkImageEnhanceCapability Capability(const std::string& type,
                                     const std::string& title,
                                     const std::string& description,
                                     const std::string& title_zh_cn,
                                     const std::string& description_zh_cn,
                                     const std::string& category,
                                     const std::string& runtime,
                                     const std::string& page_effect,
                                     const std::string& defaults_json,
                                     const std::string& schema_json,
                                     int order_hint,
                                     bool available = true,
                                     const std::string& unavailable_reason = "",
                                     const std::string& unavailable_reason_zh_cn = "") {
    SdkImageEnhanceCapability capability;
    capability.type = type;
    capability.title = title;
    capability.description = description;
    capability.i18n_key = "image_enhance." + type;
    capability.title_zh_cn = title_zh_cn;
    capability.description_zh_cn = description_zh_cn;
    capability.category = category;
    capability.runtime = runtime;
    capability.available = available;
    capability.unavailable_reason = unavailable_reason;
    capability.unavailable_reason_zh_cn = unavailable_reason_zh_cn;
    capability.requires_capability = runtime == "online" ? "image.enhance.online" : "image.enhance";
    capability.quota_unit = "page";
    capability.source_types.push_back("image");
    capability.source_types.push_back("images");
    capability.min_pages = 1;
    capability.max_pages = 1000;
    capability.page_effect = page_effect;
    capability.metadata = page_effect == "detect" || page_effect == "filter";
    capability.order_hint = order_hint;
    capability.defaults_json = defaults_json;
    capability.schema_json = schema_json;
    return capability;
}

SdkImageEnhanceStepResult Unsupported(const std::string& type) {
    SdkImageEnhanceStepResult result;
    result.code = ToCode(SdkStatusCode::UnsupportedMethod);
    result.message = "image enhance capability is not available: " + type;
    return result;
}

SdkImageEnhanceStepResult RotatePages(const SdkImageEnhanceStepRequest& request) {
    const nlohmann::json params = ParseParams(request.step.params_json);
    const std::string mode = params.value("mode", "auto");
    const double raw_angle = params.value("angle", 0.0);
    SdkImageEnhanceStepResult result;
    result.processed = true;
    EnsureDirectoryRecursive(request.output_dir);

    if (mode != "manual") {
        if (!EnsureGraphicAlgorithms(result)) {
            return result;
        }
    }

    int output_index = 1;
    for (std::vector<SdkImageEnhancePage>::const_iterator it = request.pages.begin(); it != request.pages.end(); ++it) {
        cv::Mat image = ReadImageLocked(it->path, cv::IMREAD_UNCHANGED);
        if (image.empty()) {
            result.code = ToCode(SdkStatusCode::ProviderCallFailed);
            result.message = "failed to read image for rotate";
            return result;
        }
        cv::Mat rotated;
        nlohmann::json metadata;
        metadata["mode"] = mode;
        metadata["angle"] = NormalizeAngle(raw_angle);
        if (mode == "manual") {
            rotated = RotateImage(image, raw_angle);
        } else {
            rotated = ToBgrWithWhiteBackground(image);
            const bool deskew = params.value("deskew", true);
            const float max_angle = params.value("max_angle", 20.0f);
            const int border_mode = params.value("border_mode", 1);
            const int border_val = params.value("border_val", 0);
            const int dl_flag = params.value("dl_flag", 0);
            float deskew_angle = 0.0f;
            {
                std::lock_guard<std::mutex> algorithm_lock(graphic::GraphicAlgorithmRuntime::Instance().AlgorithmMutex());
                czcv::recog_text_rotation(rotated, true);
                if (deskew) {
                    deskew_angle = czcv::deskew_page_by_text_line(rotated, max_angle, border_mode, border_val, dl_flag);
                }
            }
            metadata["deskew"] = deskew;
            metadata["deskew_angle"] = deskew_angle;
        }
        SdkImageEnhancePage page = *it;
        page.output_index = output_index;
        page.path = OutputPath(request.output_dir, request.step.type, output_index, it->path);
        page.metadata_json = DumpJson(metadata);
        if (!WriteImageWithDpiLocked(page.path, rotated, ResolvePageDpi(*it))) {
            result.code = ToCode(SdkStatusCode::ProviderCallFailed);
            result.message = "failed to write rotated image";
            return result;
        }
        result.pages.push_back(page);
        ++output_index;
    }
    nlohmann::json metadata;
    metadata["mode"] = mode;
    metadata["angle"] = NormalizeAngle(raw_angle);
    result.metadata_json = DumpJson(metadata);
    return result;
}

SdkImageEnhanceStepResult DetectBlankPages(const SdkImageEnhanceStepRequest& request) {
    const nlohmann::json params = ParseParams(request.step.params_json);
    SdkImageEnhanceStepResult result;
    if (params.count("threshold") != 0 ||
        (params.count("mode") != 0 && !params["mode"].is_string())) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "blank_page_detect accepts mode (strict, standard, loose); threshold is not supported";
        return result;
    }
    const std::string mode = params.value("mode", "standard");
    if (mode != "strict" && mode != "standard" && mode != "loose") {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "blank_page_detect mode must be strict, standard, or loose";
        return result;
    }
    const std::string action = params.value("action", "drop");
    // 同步采编王三个档位对应的两个 CZCV 检测参数。
    const float part_v_thresh = mode == "strict" ? 80.f : (mode == "loose" ? 500.f : 100.f);
    const float std_thresh = mode == "strict" ? 1.f : (mode == "loose" ? 20.f : 10.f);
    if (!EnsureGraphicAlgorithms(result)) {
        return result;
    }
    result.processed = true;
    EnsureDirectoryRecursive(request.output_dir);
    int output_index = 1;
    std::vector<int> dropped_pages;
    for (std::vector<SdkImageEnhancePage>::const_iterator it = request.pages.begin(); it != request.pages.end(); ++it) {
        cv::Mat image = ReadImageLocked(it->path, cv::IMREAD_COLOR);
        if (image.empty()) {
            result.code = ToCode(SdkStatusCode::ProviderCallFailed);
            result.message = "failed to read image for blank page detection";
            return result;
        }
        bool blank = false;
        {
            // CZCV 配置是共享状态，设置档位与检测必须在同一锁内，避免并发串档。
            std::lock_guard<std::mutex> algorithm_lock(graphic::GraphicAlgorithmRuntime::Instance().AlgorithmMutex());
            editor::graphic::setCzcvFloatParam("_blank_page_part_v_thresh_", part_v_thresh);
            editor::graphic::setCzcvFloatParam("_blank_page_std_thresh_", std_thresh);
            blank = czcv::is_blank_page(image) == czcv::CZCV_PageType::CZCV_PAGE_BLANK;
        }
        nlohmann::json page_metadata;
        page_metadata["blank"] = blank;
        page_metadata["mode"] = mode;
        if (blank && action == "drop") {
            dropped_pages.push_back(it->source_index);
            continue;
        }
        SdkImageEnhancePage page = *it;
        page.output_index = output_index;
        page.metadata_json = DumpJson(page_metadata);
        page.path = OutputPath(request.output_dir, request.step.type, output_index, it->path);
        if (!CopyFile(it->path, page.path)) {
            result.code = ToCode(SdkStatusCode::ProviderCallFailed);
            result.message = "failed to copy blank page detection output";
            return result;
        }
        result.pages.push_back(page);
        ++output_index;
    }
    nlohmann::json metadata;
    metadata["action"] = action;
    metadata["mode"] = mode;
    metadata["dropped_pages"] = dropped_pages;
    result.metadata_json = DumpJson(metadata);
    return result;
}

SdkImageEnhanceStepResult NormalizeSpec(const SdkImageEnhanceStepRequest& request) {
    const nlohmann::json params = ParseParams(request.step.params_json);
    ImgProc::ImageStandardizeParams standardize_params = ParseStandardizeParams(params);
    if (standardize_params.width_mm <= 0 || standardize_params.height_mm <= 0 || standardize_params.dpi <= 0) {
        SdkImageEnhanceStepResult result;
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "normalize_spec width, height, and dpi must be positive";
        return result;
    }
    const bool auto_format = params.value("auto", false);
    SdkImageEnhanceStepResult result;
    result.processed = true;
    EnsureDirectoryRecursive(request.output_dir);
    ImgProc::ImageEngine engine;
    int output_index = 1;
    for (std::vector<SdkImageEnhancePage>::const_iterator it = request.pages.begin(); it != request.pages.end(); ++it) {
        ImgProc::ImageStandardizeParams page_params = standardize_params;
        cv::Mat image = ReadImageLocked(it->path, cv::IMREAD_UNCHANGED);
        if (image.empty()) {
            result.code = ToCode(SdkStatusCode::ProviderCallFailed);
            result.message = "failed to read image for normalize_spec";
            return result;
        }
        if (auto_format && image.cols > image.rows) {
            if (page_params.useOriginalPaperScale) {
                std::swap(page_params.originalWidth_mm, page_params.originalHeight_mm);
            } else {
                std::swap(page_params.width_mm, page_params.height_mm);
            }
        }
        SdkImageEnhancePage page = *it;
        page.output_index = output_index;
        page.path = OutputPath(request.output_dir, request.step.type, output_index, it->path);
        nlohmann::json metadata;
        metadata["auto"] = auto_format;
        metadata["width_mm"] = page_params.width_mm;
        metadata["height_mm"] = page_params.height_mm;
        metadata["dpi"] = page_params.dpi;
        page.metadata_json = DumpJson(metadata);
        const ImgProc::ImgProcErrorCode code = engine.standardizeImageToFile(it->path, page.path, page_params);
        if (code != ImgProc::ImgProcErrorCode::SUCCESS) {
            result.code = ToCode(SdkStatusCode::ProviderCallFailed);
            result.message = "failed to write normalized image";
            return result;
        }
        result.pages.push_back(page);
        ++output_index;
    }
    nlohmann::json metadata;
    metadata["auto"] = auto_format;
    metadata["width_mm"] = standardize_params.width_mm;
    metadata["height_mm"] = standardize_params.height_mm;
    metadata["dpi"] = standardize_params.dpi;
    result.metadata_json = DumpJson(metadata);
    return result;
}

SdkImageEnhanceStepResult CropEnhance(const SdkImageEnhanceStepRequest& request) {
    const nlohmann::json params = ParseParams(request.step.params_json);
    const std::string mode = params.value("mode", "crop");
    const int crop_mode = mode == "padding" ? 1 : params.value("crop", 0);
    SdkImageEnhanceStepResult result;
    result.processed = true;
    EnsureDirectoryRecursive(request.output_dir);
    ImgProc::ImageEngine engine;
    int output_index = 1;
    for (std::vector<SdkImageEnhancePage>::const_iterator it = request.pages.begin(); it != request.pages.end(); ++it) {
        cv::Mat image = ReadImageLocked(it->path, cv::IMREAD_UNCHANGED);
        if (image.empty()) {
            result.code = ToCode(SdkStatusCode::ProviderCallFailed);
            result.message = "failed to read image for crop_enhance";
            return result;
        }

        const double top_percent = std::max(0.0, std::min(30.0, params.value("top_percent", 0.0)));
        const double bottom_percent = std::max(0.0, std::min(30.0, params.value("bottom_percent", 0.0)));
        const double left_percent = std::max(0.0, std::min(30.0, params.value("left_percent", 0.0)));
        const double right_percent = std::max(0.0, std::min(30.0, params.value("right_percent", 0.0)));

        const int x = static_cast<int>(image.cols * left_percent / 100.0);
        const int y = static_cast<int>(image.rows * top_percent / 100.0);
        int width = image.cols - x - static_cast<int>(image.cols * right_percent / 100.0);
        int height = image.rows - y - static_cast<int>(image.rows * bottom_percent / 100.0);
        width = std::max(1, width);
        height = std::max(1, height);

        SdkImageEnhancePage page = *it;
        page.output_index = output_index;
        page.path = OutputPath(request.output_dir, request.step.type, output_index, it->path);
        nlohmann::json metadata;
        metadata["mode"] = crop_mode == 1 ? "padding" : "crop";
        metadata["top_percent"] = top_percent;
        metadata["bottom_percent"] = bottom_percent;
        metadata["left_percent"] = left_percent;
        metadata["right_percent"] = right_percent;
        metadata["x"] = x;
        metadata["y"] = y;
        metadata["width"] = width;
        metadata["height"] = height;
        page.metadata_json = DumpJson(metadata);
        const ImgProc::ImgProcErrorCode code = engine.professionalCropToFile(it->path, page.path, x, y, width, height, crop_mode);
        if (code != ImgProc::ImgProcErrorCode::SUCCESS) {
            result.code = ToCode(SdkStatusCode::ProviderCallFailed);
            result.message = "failed to write crop_enhance image";
            return result;
        }
        if (!WriteDpiToExistingImage(page.path, 0, 0, ResolvePageDpi(*it))) {
            result.code = ToCode(SdkStatusCode::ProviderCallFailed);
            result.message = "failed to preserve crop_enhance dpi";
            return result;
        }
        result.pages.push_back(page);
        ++output_index;
    }
    nlohmann::json metadata;
    metadata["mode"] = crop_mode == 1 ? "padding" : "crop";
    result.metadata_json = DumpJson(metadata);
    return result;
}

SdkImageEnhanceStepResult EnhanceRedGreenHead(const SdkImageEnhanceStepRequest& request) {
    const nlohmann::json params = ParseParams(request.step.params_json);
    SdkImageEnhanceStepResult result;
    result.processed = true;
    if (!EnsureGraphicAlgorithms(result)) {
        return result;
    }
    EnsureDirectoryRecursive(request.output_dir);
    int output_index = 1;
    for (std::vector<SdkImageEnhancePage>::const_iterator it = request.pages.begin(); it != request.pages.end(); ++it) {
        cv::Mat image = ToBgrWithWhiteBackground(ReadImageLocked(it->path, cv::IMREAD_UNCHANGED));
        if (image.empty()) {
            result.code = ToCode(SdkStatusCode::ProviderCallFailed);
            result.message = "failed to read image for red_green_head";
            return result;
        }
        cv::Mat czcv_output;
        czcv::FileEnhanceParams enhance_params = ParseFileEnhanceParams(params, image.size());
        {
            std::lock_guard<std::mutex> algorithm_lock(graphic::GraphicAlgorithmRuntime::Instance().AlgorithmMutex());
            czcv::file_enhance(image, czcv_output, enhance_params);
        }
        cv::Mat output = graphic::RehomeCzcvOutputMat(czcv_output);
        const bool fallback = output.empty();
        if (fallback) {
            output = image;
        }
        SdkImageEnhancePage page = *it;
        page.output_index = output_index;
        page.path = OutputPath(request.output_dir, request.step.type, output_index, it->path);
        nlohmann::json metadata;
        metadata["auto_flag"] = enhance_params.auto_flag;
        metadata["roi_count"] = enhance_params.rois.size();
        metadata["fallback"] = fallback;
        page.metadata_json = DumpJson(metadata);
        if (!WriteImageWithDpiLocked(page.path, output, ResolvePageDpi(*it))) {
            result.code = ToCode(SdkStatusCode::ProviderCallFailed);
            result.message = "failed to write red_green_head enhanced image";
            return result;
        }
        result.pages.push_back(page);
        ++output_index;
    }
    nlohmann::json metadata;
    metadata["auto_flag"] = params.value("auto_flag", true);
    result.metadata_json = DumpJson(metadata);
    return result;
}

std::string OnlineCapabilityForStep(const std::string& type) {
    if (type == "document_rectify_enhance") {
        return "doc_crop_enhance";
    }
    if (type == "remove_background_texture") {
        return "doc_repair";
    }
    if (type == "doc_crop_enhance" ||
        type == "remove_handwriting" ||
        type == "doc_repair" ||
        type == "remove_moire") {
        return type;
    }
    return "";
}

SdkImageEnhanceStepResult RunOnlineEnhance(const SdkImageEnhanceStepRequest& request) {
    const std::string remote_capability = OnlineCapabilityForStep(request.step.type);
    if (remote_capability.empty()) {
        return Unsupported(request.step.type);
    }

    SdkImageEnhanceStepResult result;
    result.processed = true;
    EnsureDirectoryRecursive(request.output_dir);

    int output_index = 1;
    nlohmann::json step_metadata;
    step_metadata["capability"] = remote_capability;
    step_metadata["runtime"] = "online";
    for (std::vector<SdkImageEnhancePage>::const_iterator it = request.pages.begin(); it != request.pages.end(); ++it) {
        SdkImageEnhancePage page = *it;
        page.output_index = output_index;
        page.path = OutputPathWithExtension(request.output_dir, request.step.type, output_index, "jpg");

        OnlineImageEnhanceOptions options;
        options.capability = remote_capability;
        options.input_path = it->path;
        options.output_path = page.path;
        options.api_key = request.online_api_key;
        options.base_url = request.online_base_url;
        const OnlineImageEnhanceResult online_result = OnlineImageEnhanceClient::EnhanceImage(options);
        if (!IsOkStatusCode(online_result.code)) {
            result.code = online_result.code;
            result.message = online_result.message;
            return result;
        }
        page.metadata_json = online_result.metadata_json;
        result.pages.push_back(page);

        const nlohmann::json page_metadata = ParseParams(page.metadata_json);
        if (page_metadata.contains("quotaLimit")) {
            step_metadata["quotaLimit"] = page_metadata["quotaLimit"];
        }
        if (page_metadata.contains("quotaUsed")) {
            step_metadata["quotaUsed"] = page_metadata["quotaUsed"];
        }
        if (page_metadata.contains("quotaRemaining")) {
            step_metadata["quotaRemaining"] = page_metadata["quotaRemaining"];
        }
        ++output_index;
    }
    result.metadata_json = DumpJson(step_metadata);
    return result;
}

} // namespace

std::string PrivateImageEnhanceProvider::ProviderName() const {
    return "private-image-enhance-provider";
}

SdkImageEnhanceCapabilityResult PrivateImageEnhanceProvider::ListCapabilities() {
    SdkImageEnhanceCapabilityResult result;
    result.provider = ProviderName();
    result.kind = "mixed";
    result.available = true;
    const bool online_available = false;
    const std::string online_unavailable_reason = "online enhance api key is not configured";
    const std::string online_unavailable_reason_zh_cn = "在线增强 API Key 未配置";
    result.capabilities.push_back(Capability("crop_enhance",
                                             "Crop enhancement",
                                             "Crops pages by top, bottom, left, and right percentages, or keeps the original canvas and fills the outside area with white.",
                                             "裁剪增强",
                                             "按上下左右百分比裁剪页面，支持直接裁剪或裁剪区域外留白。",
                                             "cleanup",
                                             "offline",
                                             "transform",
                                             R"({"mode":"crop","top_percent":0,"bottom_percent":0,"left_percent":0,"right_percent":0})",
                                             R"({"mode":{"type":"enum","values":["crop","padding"],"default":"crop"},"top_percent":{"type":"number","min":0,"max":30,"default":0},"bottom_percent":{"type":"number","min":0,"max":30,"default":0},"left_percent":{"type":"number","min":0,"max":30,"default":0},"right_percent":{"type":"number","min":0,"max":30,"default":0}})",
                                             10));
    result.capabilities.push_back(Capability("normalize_spec",
                                             "Normalize specification",
                                             "Standardizes pages to a unified paper size, DPI, background, alignment, and fill rule for consistent export.",
                                             "统一规格",
                                             "将页面按统一宽高、DPI 和适配方式处理，便于批量导出规格一致。",
                                             "normalize",
                                             "offline",
                                             "transform",
                                             R"json({"auto":true,"format":"A4-auto","width":210,"height":297,"dpi":300,"color":"rgb(255, 255, 255)","horizontal":1,"vertical":1,"fill":1,"fillValue":1.0,"useOriginalPaperScale":false,"originalWidth":0,"originalHeight":0,"quality":95})json",
                                             R"json({"auto":{"type":"boolean","default":true},"format":{"type":"string","default":"A4-auto"},"width":{"type":"number","min":1,"default":210,"unit":"mm"},"height":{"type":"number","min":1,"default":297,"unit":"mm"},"dpi":{"type":"integer","min":1,"default":300},"color":{"type":"string","default":"rgb(255, 255, 255)"},"horizontal":{"type":"integer","min":0,"max":2,"default":1},"vertical":{"type":"integer","min":0,"max":2,"default":1},"fill":{"type":"integer","min":0,"max":3,"default":1},"fillValue":{"type":"number","default":1.0},"useOriginalPaperScale":{"type":"boolean","default":false},"originalWidth":{"type":"number","min":0,"default":0,"unit":"mm"},"originalHeight":{"type":"number","min":0,"default":0,"unit":"mm"},"quality":{"type":"integer","min":1,"max":100,"default":95}})json",
                                             20));
    result.capabilities.push_back(Capability("rotate",
                                             "Rotate",
                                             "Rotates pages manually by any angle, or automatically rotates and deskews pages by text direction.",
                                             "旋转",
                                             "支持任意手动角度旋转，也可按文字方向自动转正并纠偏。",
                                             "normalize",
                                             "offline",
                                             "transform",
                                             R"({"mode":"auto","angle":0,"deskew":true,"max_angle":20,"border_mode":1,"border_val":0,"dl_flag":0})",
                                             R"({"mode":{"type":"enum","values":["auto","manual"],"default":"auto"},"angle":{"type":"number","default":0,"unit":"degree"},"deskew":{"type":"boolean","default":true},"max_angle":{"type":"number","default":20},"border_mode":{"type":"integer","default":1},"border_val":{"type":"integer","default":0},"dl_flag":{"type":"integer","default":0}})",
                                             30));
    result.capabilities.push_back(Capability("blank_page_detect",
                                             "Blank page detection",
                                             "Detects blank pages and can mark or remove them from the output sequence.",
                                             "空白页检测",
                                             "检测空白页，可选择标记空白页或从输出序列中移除。",
                                             "detect",
                                             "offline",
                                             "filter",
                                             R"({"action":"drop","mode":"standard"})",
                                             R"({"action":{"type":"enum","values":["mark","drop"],"default":"drop"},"mode":{"type":"enum","values":["strict","standard","loose"],"default":"standard"}})",
                                             40));
    result.capabilities.push_back(Capability("red_green_head",
                                             "Red/green head enhancement",
                                             "Enhances red or green header documents with the same offline algorithm used by the desktop client.",
                                             "红绿头增强",
                                             "使用客户端同款离线算法增强红头、绿头文件。",
                                             "enhance",
                                             "offline",
                                             "transform",
                                             R"({"auto_flag":true,"rois":[],"stroke_width":0,"blur_level":0,"color_thresh":50,"texture_degree":1.2,"specify_color":false,"src_color":[0,0,0],"target_color":[67,67,222]})",
                                             R"({"auto_flag":{"type":"boolean","default":true},"rois":{"type":"array","default":[]},"stroke_width":{"type":"integer","default":0},"blur_level":{"type":"integer","default":0},"color_thresh":{"type":"integer","default":50},"texture_degree":{"type":"number","default":1.2},"specify_color":{"type":"boolean","default":false},"src_color":{"type":"array","default":[0,0,0]},"target_color":{"type":"array","default":[67,67,222]}})",
                                             50));
    result.capabilities.push_back(Capability("doc_crop_enhance",
                                             "Document rectification enhancement",
                                             "Online service for document perspective correction, cleanup, and visual enhancement.",
                                             "文档矫正增强",
                                             "在线服务能力，用于文档透视矫正、清理和视觉增强。",
                                             "enhance",
                                             "online",
                                             "transform",
                                             "{}",
                                             "{}",
                                             100,
                                             online_available,
                                             online_unavailable_reason,
                                             online_unavailable_reason_zh_cn));
    result.capabilities.push_back(Capability("remove_handwriting",
                                             "Remove handwriting",
                                             "Online service for removing handwriting traces from document images.",
                                             "文档去手写",
                                             "在线服务能力，用于去除文档图片中的手写痕迹。",
                                             "enhance",
                                             "online",
                                             "transform",
                                             "{}",
                                             "{}",
                                             110,
                                             online_available,
                                             online_unavailable_reason,
                                             online_unavailable_reason_zh_cn));
    result.capabilities.push_back(Capability("doc_repair",
                                             "Remove background texture",
                                             "Online service for reducing paper texture, watermark-like background, and patterned noise.",
                                             "文档图片去底纹",
                                             "在线服务能力，用于减弱纸张底纹、水印类背景和纹理噪声。",
                                             "enhance",
                                             "online",
                                             "transform",
                                             "{}",
                                             "{}",
                                             120,
                                             online_available,
                                             online_unavailable_reason,
                                             online_unavailable_reason_zh_cn));
    result.capabilities.push_back(Capability("remove_moire",
                                             "Remove moire",
                                             "Online service for removing moire patterns from photographed screens, printed textures, and similar image noise.",
                                             "图片去摩尔纹",
                                             "在线服务能力，用于去除屏幕翻拍、印刷纹理等摩尔纹。",
                                             "enhance",
                                             "online",
                                             "transform",
                                             "{}",
                                             "{}",
                                             130,
                                             online_available,
                                             online_unavailable_reason,
                                             online_unavailable_reason_zh_cn));
    return result;
}

SdkImageEnhanceStepResult PrivateImageEnhanceProvider::RunStep(const SdkImageEnhanceStepRequest& request) {
    if (request.step.type == "rotate") {
        return RotatePages(request);
    }
    if (request.step.type == "blank_page_detect") {
        return DetectBlankPages(request);
    }
    if (request.step.type == "normalize_spec") {
        return NormalizeSpec(request);
    }
    if (request.step.type == "crop_enhance") {
        return CropEnhance(request);
    }
    if (request.step.type == "red_green_head") {
        return EnhanceRedGreenHead(request);
    }
    if (!OnlineCapabilityForStep(request.step.type).empty()) {
        return RunOnlineEnhance(request);
    }
    return Unsupported(request.step.type);
}

} // namespace private_impl
} // namespace sdk
} // namespace editor
