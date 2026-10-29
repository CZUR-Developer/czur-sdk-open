#include "graphic/single_page_processor.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>

#include "czcv/czcv_all_header.hpp"
#include "graphic/graphic_algorithm_runtime.h"
#include "graphic/image_codec_lock.h"
#include "graphic/czcv_result_ownership.h"
#include "image_write_utils.h"
#include "opencv2/opencv.hpp"
#include "private_sdk_support.h"
#include "support/private_sdk_logger.h"

namespace editor {
namespace sdk {
namespace private_impl {
namespace graphic {

namespace {

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

cv::Mat ReadImageLocked(const std::string& path, int flags) {
    std::lock_guard<std::mutex> lock(ImageCodecMutex());
    return cv::imread(path, flags);
}

std::string ParentDir(const std::string& path) {
    const std::string::size_type pos = path.find_last_of("/\\");
    if (pos == std::string::npos || pos == 0) {
        return pos == 0 ? "/" : ".";
    }
    return path.substr(0, pos);
}

bool IsPathSeparator(char ch) {
    return ch == '/' || ch == '\\';
}

std::string JoinPath(const std::string& dir, const std::string& name) {
    if (dir.empty() || dir == ".") {
        return name;
    }
    if (IsPathSeparator(dir[dir.size() - 1])) {
        return dir + name;
    }
    return dir + "/" + name;
}

bool HasValidRect(const SdkRect4P& rect) {
    const float min_x = std::min(std::min(rect.left_top.x, rect.right_top.x), std::min(rect.right_down.x, rect.left_down.x));
    const float max_x = std::max(std::max(rect.left_top.x, rect.right_top.x), std::max(rect.right_down.x, rect.left_down.x));
    const float min_y = std::min(std::min(rect.left_top.y, rect.right_top.y), std::min(rect.right_down.y, rect.left_down.y));
    const float max_y = std::max(std::max(rect.left_top.y, rect.right_top.y), std::max(rect.right_down.y, rect.left_down.y));
    return max_x - min_x >= 8.0f && max_y - min_y >= 8.0f;
}

czcv::CZCV_Rect4P ConvertRect(const SdkRect4P& rect, float scale_x, float scale_y) {
    czcv::CZCV_Rect4P converted;
    converted.leftTopP.x = rect.left_top.x * scale_x;
    converted.leftTopP.y = rect.left_top.y * scale_y;
    converted.rightTopP.x = rect.right_top.x * scale_x;
    converted.rightTopP.y = rect.right_top.y * scale_y;
    converted.rightDownP.x = rect.right_down.x * scale_x;
    converted.rightDownP.y = rect.right_down.y * scale_y;
    converted.leftDownP.x = rect.left_down.x * scale_x;
    converted.leftDownP.y = rect.left_down.y * scale_y;
    return converted;
}

void ConfigureCutParams(czcv::CutParams& params,
                        const SdkPageProcessRequest& request,
                        int width,
                        int height) {
    params.srcW = width;
    params.srcH = height;
    params.dstW = width;
    params.dstH = height;
    params.outChannels = 3;
    params.isMultiplePage = request.single_page.multi_target_paging;
    params.deviceType = static_cast<czcv::ScanDeviceType>(request.scan_device_type);
    params.widthCutMargin = request.single_page.crop_border.enabled ? request.single_page.crop_border.width : 0;
    params.heightCutMargin = request.single_page.crop_border.enabled ? request.single_page.crop_border.height : 0;
    params.repairType = request.single_page.id_card_round_corner ? czcv::CZCV_REPAIR_4_CORNER_WHITE : czcv::CZCV_REPAIR_NOTHING;
    params.colorFilterMode = czcv::CZCV_FILTER_NOTHING;
}

void RehomeCzcvDetectedRects(czcv::CutParams& params) {
    RehomeCzcvStdVector(params.detectedRects);
}

void ProcessExtendParams(cv::Mat& image, const SdkSinglePageOptions& options) {
    cv::Mat czcv_output;
    if (options.smart_black_edge_optimize) {
        czcv::page_filling_black_egde(image, czcv_output);
        cv::Mat output = RehomeCzcvOutputMat(czcv_output);
        if (!output.empty()) {
            image = output;
        }
    }
    if (options.auto_rotate) {
        czcv::recog_text_rotation(image, true);
        czcv::deskew_page_by_text_line(image, 20, 1, 0, 0);
    }
}

void FillImageInfo(SdkPageOutput& output) {
    if (!FileExists(output.path)) {
        return;
    }
    cv::Mat image = ReadImageLocked(output.path, cv::IMREAD_UNCHANGED);
    if (!image.empty()) {
        output.width = image.cols;
        output.height = image.rows;
    }
    output.size = FileSize(output.path);
}

std::string OutputPathForIndex(const SdkPageProcessRequest& request, int index, bool single_output) {
    if (single_output || index == 0) {
        return request.output_path;
    }
    std::ostringstream name;
    name << "page_processed_" << std::setw(3) << std::setfill('0') << (index + 1) << ".jpg";
    const std::string dir = request.output_dir.empty() ? ParentDir(request.output_path) : request.output_dir;
    return JoinPath(dir, name.str());
}

void AddPageOutput(SdkPageProcessResult& result, const std::string& path, int index, int dpi) {
    SdkPageOutput page;
    std::ostringstream output_id;
    output_id << "page-" << std::setw(3) << std::setfill('0') << (index + 1);
    page.output_id = output_id.str();
    page.role = "page";
    page.index = index;
    page.path = path;
    page.content_type = "image/jpeg";
    FillImageInfo(page);
    page.dpi = dpi;
    result.outputs.push_back(page);
    if (result.output_path.empty()) {
        result.output_path = page.path;
    }
}

} // namespace

SinglePageProcessor::SinglePageProcessor() = default;

SinglePageProcessor::~SinglePageProcessor() = default;

SdkPageProcessResult SinglePageProcessor::Process(const SdkPageProcessRequest& request) {
    SdkPageProcessResult result;
    SDK_PRIVATE_LOG_INFO("graphic.single_page begin mode={} device={} input={} output={} realtime_rects={} rects={}",
                         request.page_processing,
                         SafeDeviceId(request.device_id),
                         SafePathSummary(request.input_path),
                         SafePathSummary(request.output_path),
                         request.single_page_realtime_detect_rects,
                         request.detected_rects.size());
    if (request.input_path.empty() || request.output_path.empty()) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "single_page input_path or output_path is empty";
        return result;
    }
    if (!FileExists(request.input_path)) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "single_page input image does not exist";
        return result;
    }

    std::lock_guard<std::mutex> lock(mu_);
    SdkStatusCode init_code = SdkStatusCode::Ok;
    std::string init_message;
    if (!GraphicAlgorithmRuntime::Instance().EnsureInitialized(init_code, init_message)) {
        result.code = ToCode(init_code);
        result.message = init_message.empty() ? "graphic service initialization failed" : init_message;
        return result;
    }

    cv::Mat source = ReadImageLocked(request.input_path, cv::IMREAD_COLOR);
    if (source.empty()) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "failed to read single_page input image";
        return result;
    }

    czcv::CutParams params;
    ConfigureCutParams(params, request, source.cols, source.rows);
    const bool selected_area = request.page_processing == "selected_area";
    const bool can_use_selected_area = selected_area && HasValidRect(request.selected_area_rect);
    const bool can_use_realtime_rects = request.single_page_realtime_detect_rects && !request.detected_rects.empty();
    const bool can_use_rect = can_use_selected_area || can_use_realtime_rects;
    try {
        std::lock_guard<std::mutex> algorithm_lock(GraphicAlgorithmRuntime::Instance().AlgorithmMutex());
        std::vector<czcv::CZCV_Rect4P> rects;
        if (can_use_selected_area) {
            float scale_x = 1.0f;
            float scale_y = 1.0f;
            if (request.selected_area_source_width > 0 && request.selected_area_source_height > 0) {
                scale_x = static_cast<float>(source.cols) / static_cast<float>(request.selected_area_source_width);
                scale_y = static_cast<float>(source.rows) / static_cast<float>(request.selected_area_source_height);
            }
            rects.push_back(ConvertRect(request.selected_area_rect, scale_x, scale_y));
        } else if (can_use_realtime_rects) {
            float scale_x = 1.0f;
            float scale_y = 1.0f;
            if (request.detected_rects_source_width > 0 && request.detected_rects_source_height > 0) {
                scale_x = static_cast<float>(source.cols) / static_cast<float>(request.detected_rects_source_width);
                scale_y = static_cast<float>(source.rows) / static_cast<float>(request.detected_rects_source_height);
            }
            for (std::vector<SdkRect4P>::const_iterator it = request.detected_rects.begin(); it != request.detected_rects.end(); ++it) {
                if (HasValidRect(*it)) {
                    rects.push_back(ConvertRect(*it, scale_x, scale_y));
                }
                if (!request.single_page.multi_target_paging && !rects.empty()) {
                    break;
                }
            }
        } else if (request.single_page.multi_target_paging) {
            czcv::page_keypoints_detect(source, params);
            RehomeCzcvDetectedRects(params);
            rects = params.detectedRects;
        }

        if (!rects.empty()) {
            const bool single_output = rects.size() == 1;
            for (std::vector<czcv::CZCV_Rect4P>::const_iterator it = rects.begin(); it != rects.end(); ++it) {
                czcv::CutParams page_params;
                ConfigureCutParams(page_params, request, source.cols, source.rows);
                page_params.detectedRects.push_back(*it);
                cv::Mat czcv_output;
                czcv::cut_page_by_keypoints(source, czcv_output, page_params);
                cv::Mat output = RehomeCzcvOutputMat(czcv_output);
                ProcessExtendParams(output, request.single_page);
                const int index = static_cast<int>(result.outputs.size());
                const std::string output_path = OutputPathForIndex(request, index, single_output);
                if (output.empty() || !WriteImageWithDpiLocked(output_path, output, request.dpi)) {
                    throw std::runtime_error("failed to write single_page output");
                }
                AddPageOutput(result, output_path, index, request.dpi);
            }
        } else if (selected_area) {
            result.code = ToCode(SdkStatusCode::InvalidParams);
            result.message = "selected_area points are invalid";
            SDK_PRIVATE_LOG_WARN("graphic.single_page selected_area invalid_points input={}",
                                 SafePathSummary(request.input_path));
            return result;
        } else {
            cv::Mat czcv_output;
            czcv::single_cut_pipeline(source, czcv_output, params);
            cv::Mat output = RehomeCzcvOutputMat(czcv_output);
            RehomeCzcvDetectedRects(params);
            ProcessExtendParams(output, request.single_page);
            if (output.empty() || !WriteImageWithDpiLocked(request.output_path, output, request.dpi)) {
                throw std::runtime_error("failed to write single_page output");
            }
            AddPageOutput(result, request.output_path, 0, request.dpi);
        }
    } catch (const std::exception& e) {
        SDK_PRIVATE_LOG_WARN("graphic.single_page algorithm_failed input={} error={}",
                             SafePathSummary(request.input_path),
                             e.what());
        result.outputs.clear();
    } catch (...) {
        SDK_PRIVATE_LOG_WARN("graphic.single_page algorithm_failed input={} error=unknown",
                             SafePathSummary(request.input_path));
        result.outputs.clear();
    }

    if (result.outputs.empty()) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = "single_page page processing failed";
        SDK_PRIVATE_LOG_ERROR("graphic.single_page failed code={} input={} output={} realtime_rects={} used_rect={}",
                              result.code,
                              SafePathSummary(request.input_path),
                              SafePathSummary(request.output_path),
                              request.single_page_realtime_detect_rects,
                              can_use_rect);
        return result;
    }

    result.processed = true;
    result.message = "ok";
    SDK_PRIVATE_LOG_INFO("graphic.single_page success output={} outputs={} used_rect={}",
                         SafePathSummary(result.output_path),
                         result.outputs.size(),
                         can_use_rect);
    return result;
}

} // namespace graphic
} // namespace private_impl
} // namespace sdk
} // namespace editor
