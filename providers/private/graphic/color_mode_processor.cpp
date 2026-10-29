#include "graphic/color_mode_processor.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <fstream>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "czcv/czcv_all_header.hpp"
#include "graphic/czcv_result_ownership.h"
#include "graphic/graphic_algorithm_runtime.h"
#include "graphic/image_codec_lock.h"
#include "image_write_utils.h"
#include "opencv2/opencv.hpp"
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

std::string NormalizeMode(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool CopyFileBinary(const std::string& src, const std::string& dst) {
    if (src == dst) {
        return true;
    }
    std::ifstream in(src.c_str(), std::ios::binary);
    std::ofstream out(dst.c_str(), std::ios::binary | std::ios::trunc);
    if (!in || !out) {
        return false;
    }
    out << in.rdbuf();
    return static_cast<bool>(out);
}

bool DpiAlreadyMatches(const std::string& path, int dpi) {
    if (dpi <= 0) {
        return true;
    }
    const int existing_dpi = ReadImageDpiFromMetadata(path);
    if (existing_dpi <= 0) {
        return false;
    }
    const int diff = existing_dpi > dpi ? existing_dpi - dpi : dpi - existing_dpi;
    return diff <= 1;
}

cv::Mat FlattenAlphaToWhite(const cv::Mat& image) {
    if (image.empty() || image.channels() != 4) {
        return image;
    }
    if (image.depth() != CV_8U) {
        cv::Mat converted;
        cv::cvtColor(image, converted, cv::COLOR_BGRA2BGR);
        return converted;
    }

    cv::Mat bgr(image.rows, image.cols, CV_8UC3, cv::Scalar(255, 255, 255));
    for (int y = 0; y < image.rows; ++y) {
        const cv::Vec4b* src = image.ptr<cv::Vec4b>(y);
        cv::Vec3b* dst = bgr.ptr<cv::Vec3b>(y);
        for (int x = 0; x < image.cols; ++x) {
            const double alpha = static_cast<double>(src[x][3]) / 255.0;
            dst[x][0] = static_cast<uchar>(src[x][0] * alpha + 255.0 * (1.0 - alpha));
            dst[x][1] = static_cast<uchar>(src[x][1] * alpha + 255.0 * (1.0 - alpha));
            dst[x][2] = static_cast<uchar>(src[x][2] * alpha + 255.0 * (1.0 - alpha));
        }
    }
    return bgr;
}

cv::Mat NormalizeForColorFilter(const cv::Mat& image) {
    cv::Mat normalized = FlattenAlphaToWhite(image);
    if (normalized.empty()) {
        return normalized;
    }
    if (normalized.depth() != CV_8U) {
        double min_value = 0.0;
        double max_value = 0.0;
        cv::minMaxLoc(normalized, &min_value, &max_value);
        const double scale = max_value > min_value ? 255.0 / (max_value - min_value) : 1.0;
        const double shift = max_value > min_value ? -min_value * scale : 0.0;
        normalized.convertTo(normalized, CV_8U, scale, shift);
    }
    if (normalized.channels() == 1) {
        cv::cvtColor(normalized, normalized, cv::COLOR_GRAY2BGR);
    } else if (normalized.channels() == 4) {
        cv::cvtColor(normalized, normalized, cv::COLOR_BGRA2BGR);
    } else if (normalized.channels() > 4) {
        std::vector<cv::Mat> channels;
        cv::split(normalized, channels);
        channels.resize(3);
        cv::merge(channels, normalized);
    } else if (normalized.channels() != 3) {
        return cv::Mat();
    }
    return normalized;
}

bool ResolveMode(const std::string& raw_mode, czcv::CZCV_ColorFilterMode& mode, bool& passthrough, bool& used_default) {
    const std::string value = NormalizeMode(raw_mode);
    passthrough = false;
    used_default = false;

    if (value.empty() || value == "auto_optimize" || value == "autoopti" || value == "filter_auto") {
        mode = czcv::CZCV_FILTER_AUTO;
        return true;
    }
    if (value == "color" || value == "color_enhance" || value == "colorenhance" || value == "filter_color_default") {
        mode = czcv::CZCV_FILTER_COLOR_DEFAULT;
        return true;
    }
    if (value == "black_white" || value == "blackwhite" || value == "filter_bw_default") {
        mode = czcv::CZCV_FILTER_BW_DEFAULT;
        return true;
    }
    if (value == "white_paper_seal" || value == "white_paper_stamp" || value == "whitepaper" || value == "filter_seal_red") {
        mode = czcv::CZCV_FILTER_SEAL_RED;
        return true;
    }
    if (value == "grayscale" || value == "gray_scale" || value == "filter_gray_default") {
        mode = czcv::CZCV_FILTER_GRAY_DEFAULT;
        return true;
    }
    if (value == "certificate" || value == "credential_texture" || value == "filter_credential_texture") {
        mode = czcv::CZCV_FILTER_CREDENTIAL_TEXTURE;
        return true;
    }
    if (value == "ancient" || value == "ancient_book" || value == "filter_ancient") {
        mode = czcv::CZCV_FILTER_ANCIENT;
        return true;
    }
    if (value == "none" || value == "original" || value == "no_optimize" || value == "noopti" || value == "filter_nothing") {
        mode = czcv::CZCV_FILTER_NOTHING;
        passthrough = true;
        return true;
    }

    mode = czcv::CZCV_FILTER_COLOR_DEFAULT;
    used_default = true;
    return true;
}

} // namespace

SdkColorModeResult ColorModeProcessor::Apply(const SdkColorModeRequest& request) const {
    SdkColorModeResult result;
    if (request.input_path.empty() || request.output_path.empty()) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "color mode input_path or output_path is empty";
        return result;
    }
    if (!FileExists(request.input_path)) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "color mode input image does not exist";
        return result;
    }

    czcv::CZCV_ColorFilterMode mode = czcv::CZCV_FILTER_AUTO;
    bool passthrough = false;
    bool used_default = false;
    ResolveMode(request.color_mode, mode, passthrough, used_default);
    if (used_default) {
        SDK_PRIVATE_LOG_WARN("graphic.color_mode unknown_mode mode={} fallback=FILTER_COLOR_DEFAULT",
                             request.color_mode);
    }

    if (passthrough) {
        if (!CopyFileBinary(request.input_path, request.output_path)) {
            result.code = ToCode(SdkStatusCode::ProviderCallFailed);
            result.message = "failed to copy passthrough color mode image";
            return result;
        }
        if (request.dpi > 0 &&
            !DpiAlreadyMatches(request.output_path, request.dpi) &&
            !WriteDpiToExistingImage(request.output_path, 0, 0, request.dpi)) {
            SDK_PRIVATE_LOG_WARN("graphic.color_mode passthrough dpi_write_failed input={} output={} dpi={}",
                                 SafePathSummary(request.input_path),
                                 SafePathSummary(request.output_path),
                                 request.dpi);
        }
        result.processed = true;
        result.output_path = request.output_path;
        result.message = "ok";
        SDK_PRIVATE_LOG_INFO("graphic.color_mode passthrough input={} output={} size={}",
                             SafePathSummary(request.input_path),
                             SafePathSummary(request.output_path),
                             FileSize(request.output_path));
        return result;
    }

    SdkStatusCode init_code = SdkStatusCode::Ok;
    std::string init_message;
    if (!GraphicAlgorithmRuntime::Instance().EnsureInitialized(init_code, init_message)) {
        result.code = ToCode(init_code);
        result.message = init_message.empty() ? "graphic service initialization failed" : init_message;
        return result;
    }

    cv::Mat source;
    try {
        std::lock_guard<std::mutex> lock(ImageCodecMutex());
        source = cv::imread(request.input_path, cv::IMREAD_UNCHANGED);
    } catch (const std::exception& e) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = std::string("failed to read color mode input: ") + e.what();
        return result;
    }
    source = NormalizeForColorFilter(source);
    if (source.empty()) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = "failed to read color mode input";
        return result;
    }

    cv::Mat czcv_output;
    try {
        czcv::ColorFilterParams params;
        params.mode = mode;
        params.srcW = source.cols;
        params.srcH = source.rows;
        params.inChannels = source.channels();
        params.outChannels = source.channels();
        {
            std::lock_guard<std::mutex> algorithm_lock(GraphicAlgorithmRuntime::Instance().AlgorithmMutex());
            czcv::color_filter(source, czcv_output, params);
        }
    } catch (const std::exception& e) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = std::string("color mode algorithm failed: ") + e.what();
        return result;
    } catch (...) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = "color mode algorithm failed";
        return result;
    }
    cv::Mat output = RehomeCzcvOutputMat(czcv_output);

    if (output.empty()) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = "color mode algorithm produced empty image";
        return result;
    }

    bool written = false;
    try {
        written = WriteImageWithDpiLocked(request.output_path, output, request.dpi);
    } catch (const std::exception& e) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = std::string("failed to write color mode output: ") + e.what();
        return result;
    }
    if (!written) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = "failed to write color mode output";
        return result;
    }

    result.processed = true;
    result.output_path = request.output_path;
    result.message = "ok";
    SDK_PRIVATE_LOG_INFO("graphic.color_mode success mode={} output={} size={} dimensions={}x{}",
                         request.color_mode,
                         SafePathSummary(result.output_path),
                         FileSize(result.output_path),
                         output.cols,
                         output.rows);
    return result;
}

} // namespace graphic
} // namespace private_impl
} // namespace sdk
} // namespace editor
