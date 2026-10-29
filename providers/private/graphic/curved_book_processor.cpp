#include "graphic/curved_book_processor.h"

#include <cstdlib>
#include <mutex>
#include <sys/stat.h>

#include "czcv/czcv_all_header.hpp"
#include "czcv_dewarp_compat.h"
#include "graphic/czcv_result_ownership.h"
#include "graphic/graphic_algorithm_runtime.h"
#include "graphic/image_codec_lock.h"
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

std::string EnvValue(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? std::string() : std::string(value);
}

cv::Mat ReadImageLocked(const std::string& path, int flags) {
    std::lock_guard<std::mutex> lock(ImageCodecMutex());
    return cv::imread(path, flags);
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

bool ConfigureDewarpParams(czcv::DewarpParams& params,
                          const SdkPageProcessRequest& request,
                          int width,
                          int height) {
    params.srcW = width;
    params.srcH = height;
    params.widthCutMargin = request.curved_book.crop_border.enabled ? request.curved_book.crop_border.width : 0;
    params.heightCutMargin = request.curved_book.crop_border.enabled ? request.curved_book.crop_border.height : 0;
    const int center_margin = request.curved_book.crop_center.enabled ? request.curved_book.crop_center.margin : 0;
    if (!editor::graphic::set_center_cut_margin(params, center_margin, 0)) {
        return false;
    }
    if (!request.curved_book.remove_finger) {
        params.fingerType = czcv::CZCV_FINGER_NO_REMOVE;
    } else if (request.curved_book.finger_type == "without_sleeve") {
        params.fingerType = czcv::CZCV_FINGER_RAW_SKIN;
    } else {
        params.fingerType = czcv::CZCV_FINGER_AUTO;
    }
    params.colorFilterMode = czcv::CZCV_FILTER_NOTHING;
    params.needWhichPage = czcv::CZCV_DOUBLE_PAGE;
    params.guidedCenterLine = cv::Vec4f(0.0f, 0.0f, 0.0f, 0.0f);
    return true;
}

void ProcessCurvedExtendParams(cv::Mat& image, const SdkCurvedBookOptions& options) {
    if (!options.auto_complete) {
        return;
    }
    cv::Mat czcv_output;
    czcv::page_filling_black_background(image, czcv_output);
    cv::Mat output = RehomeCzcvOutputMat(czcv_output);
    if (!output.empty()) {
        image = output;
    }
}

bool WriteDewarpOutputs(cv::Mat& image,
                        const czcv::DewarpParams& params,
                        const SdkCurvedBookOptions& options,
                        const std::string& left_path,
                        const std::string& right_path,
                        const std::string& single_path,
                        int dpi) {
    if (image.empty()) {
        return false;
    }
    if (!options.smart_paging) {
        cv::Mat single = image.clone();
        ProcessCurvedExtendParams(single, options);
        return WriteImageWithDpiLocked(single_path, single, dpi);
    }
    int center_x = params.centerX;
    if (center_x <= 0 || center_x >= image.cols) {
        center_x = image.cols / 2;
    }
    cv::Mat left = image(cv::Rect(0, 0, center_x, image.rows)).clone();
    cv::Mat right = image(cv::Rect(center_x, 0, image.cols - center_x, image.rows)).clone();
    ProcessCurvedExtendParams(left, options);
    ProcessCurvedExtendParams(right, options);
    return WriteImageWithDpiLocked(left_path, left, dpi) &&
           WriteImageWithDpiLocked(right_path, right, dpi);
}

} // namespace

CurvedBookProcessor::CurvedBookProcessor() = default;

CurvedBookProcessor::~CurvedBookProcessor() = default;

SdkPageProcessResult CurvedBookProcessor::Process(const SdkPageProcessRequest& request) {
    SdkPageProcessResult result;
    SDK_PRIVATE_LOG_INFO("graphic.curved_book begin device={} input={} laser={} output_dir={} requested={}x{}",
                         SafeDeviceId(request.device_id),
                         SafePathSummary(request.input_path),
                         SafePathSummary(request.laser_path),
                         SafePathSummary(request.output_dir),
                         request.width,
                         request.height);
    if (request.input_path.empty()) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "curved_book input_path is empty";
        SDK_PRIVATE_LOG_WARN("graphic.curved_book failed code={} message={}", result.code, result.message);
        return result;
    }
    if (!FileExists(request.input_path)) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "curved_book input image does not exist";
        SDK_PRIVATE_LOG_WARN("graphic.curved_book failed code={} input={} message={}",
                             result.code,
                             SafePathSummary(request.input_path),
                             result.message);
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

    int width = request.width;
    int height = request.height;
    if (width <= 0 || height <= 0) {
        cv::Mat image = ReadImageLocked(request.input_path, cv::IMREAD_UNCHANGED);
        if (!image.empty()) {
            width = image.cols;
            height = image.rows;
        }
    }
    const std::string output_dir = ResolveOutputDir(request);
    const std::string left_path = JoinPath(output_dir, "page_processed_left.jpg");
    const std::string right_path = JoinPath(output_dir, "page_processed_right.jpg");
    cv::Mat source = ReadImageLocked(request.input_path, cv::IMREAD_COLOR);
    if (source.empty()) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "failed to read curved_book input image";
        SDK_PRIVATE_LOG_WARN("graphic.curved_book failed code={} input={} message={}",
                             result.code,
                             SafePathSummary(request.input_path),
                             result.message);
        return result;
    }
    if (width <= 0 || height <= 0) {
        width = source.cols;
        height = source.rows;
    }

    int rc = -1;
    cv::Mat processed;
    czcv::DewarpParams params;
    if (!ConfigureDewarpParams(params, request, width, height)) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "crop_center requires a CZCV version with centerCutMargin support";
        return result;
    }
    const bool has_laser_image = FileExists(request.laser_path);
    bool used_laser = false;
    if (has_laser_image && LoadLaserConfigIfNeeded(request, width, height)) {
        cv::Mat laser = ReadImageLocked(request.laser_path, cv::IMREAD_COLOR);
        if (!laser.empty()) {
            try {
                std::lock_guard<std::mutex> algorithm_lock(GraphicAlgorithmRuntime::Instance().AlgorithmMutex());
                cv::Mat czcv_processed;
                czcv::dewarp_by_laser(source, laser, czcv_processed, params);
                processed = RehomeCzcvOutputMat(czcv_processed);
                rc = WriteDewarpOutputs(processed, params, request.curved_book, left_path, right_path, request.output_path, request.dpi) ? 0 : -1;
                used_laser = rc == 0;
            } catch (...) {
                rc = -1;
            }
        }
    }
    if (rc != 0) {
        SDK_PRIVATE_LOG_INFO("graphic.curved_book fallback_edge input={} laser_present={}",
                             SafePathSummary(request.input_path),
                             has_laser_image);
        try {
            processed.release();
            ConfigureDewarpParams(params, request, width, height);
            std::lock_guard<std::mutex> algorithm_lock(GraphicAlgorithmRuntime::Instance().AlgorithmMutex());
            cv::Mat czcv_processed;
            czcv::dewarp_by_edge_a3(source, czcv_processed, params);
            processed = RehomeCzcvOutputMat(czcv_processed);
            rc = WriteDewarpOutputs(processed, params, request.curved_book, left_path, right_path, request.output_path, request.dpi) ? 0 : -1;
        } catch (...) {
            rc = -1;
        }
    }
    if (rc != 0) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = "curved_book page processing failed";
        SDK_PRIVATE_LOG_ERROR("graphic.curved_book failed code={} input={} laser={} message={}",
                              result.code,
                              SafePathSummary(request.input_path),
                              SafePathSummary(request.laser_path),
                              result.message);
        return result;
    }

    if (!request.curved_book.smart_paging) {
        SdkPageOutput single;
        single.output_id = "page-001";
        single.role = "page";
        single.index = 0;
        single.path = request.output_path;
        single.content_type = "image/jpeg";
        FillImageInfo(single);
        single.dpi = request.dpi;
        if (FileExists(single.path)) {
            result.outputs.push_back(single);
            result.output_path = single.path;
        }
    }

    SdkPageOutput left;
    left.output_id = "left";
    left.role = "left";
    left.index = 0;
    left.path = left_path;
    left.content_type = "image/jpeg";
    FillImageInfo(left);
    left.dpi = request.dpi;
    if (request.curved_book.smart_paging && FileExists(left.path)) {
        result.outputs.push_back(left);
        result.output_path = left.path;
    }

    SdkPageOutput right;
    right.output_id = "right";
    right.role = "right";
    right.index = 1;
    right.path = right_path;
    right.content_type = "image/jpeg";
    FillImageInfo(right);
    right.dpi = request.dpi;
    if (request.curved_book.smart_paging && FileExists(right.path)) {
        result.outputs.push_back(right);
    }

    if (result.outputs.empty() && FileExists(request.output_path)) {
        SdkPageOutput single;
        single.output_id = "page-001";
        single.role = "page";
        single.index = 0;
        single.path = request.output_path;
        single.content_type = "image/jpeg";
        FillImageInfo(single);
        single.dpi = request.dpi;
        result.outputs.push_back(single);
        result.output_path = single.path;
    }

    if (result.outputs.empty()) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = "curved_book produced no output images";
        SDK_PRIVATE_LOG_ERROR("graphic.curved_book failed code={} message={}", result.code, result.message);
        return result;
    }

    result.processed = true;
    result.message = "ok";
    SDK_PRIVATE_LOG_INFO("graphic.curved_book success outputs={} used_laser={} output={} left={} right={}",
                         result.outputs.size(),
                         used_laser,
                         SafePathSummary(result.output_path),
                         SafePathSummary(left_path),
                         SafePathSummary(right_path));
    return result;
}

bool CurvedBookProcessor::LoadLaserConfigIfNeeded(const SdkPageProcessRequest& request, int width, int height) {
    if (request.laser_path.empty() || request.device_id.empty() || width <= 0 || height <= 0) {
        return false;
    }
    if (laser_config_device_id_ == request.device_id && laser_config_width_ == width && laser_config_height_ == height) {
        return true;
    }
    const std::string config_path = ResolveLaserConfigPath(request.device_id);
    if (!FileExists(config_path)) {
        return false;
    }
    try {
        std::lock_guard<std::mutex> algorithm_lock(GraphicAlgorithmRuntime::Instance().AlgorithmMutex());
        czcv::load_laser_config(config_path.c_str(), width, height);
    } catch (...) {
        return false;
    }
    laser_config_device_id_ = request.device_id;
    laser_config_width_ = width;
    laser_config_height_ = height;
    return true;
}

std::string CurvedBookProcessor::ResolveOutputDir(const SdkPageProcessRequest& request) const {
    if (!request.output_dir.empty()) {
        return request.output_dir;
    }
    if (!request.output_path.empty()) {
        return ParentDir(request.output_path);
    }
    return ".";
}

std::string CurvedBookProcessor::ResolveLaserConfigPath(const std::string& device_id) const {
    const std::string env = EnvValue("SDK_GRAPHIC_LASER_CONFIG_PATH");
    if (!env.empty()) {
        return env;
    }
    if (device_id.empty()) {
        return std::string();
    }
    return GetSdkDeviceLaserConfigPath(device_id);
}

} // namespace graphic
} // namespace private_impl
} // namespace sdk
} // namespace editor
