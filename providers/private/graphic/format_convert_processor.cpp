#include "graphic/format_convert_processor.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <exception>
#include <vector>
#include <sys/stat.h>

#if !defined(SDK_OPEN_HARMONY)
#include "imgengine.h"
#endif
#include "image_write_utils.h"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"
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

std::string NormalizeFormat(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool IsJpegFormat(const std::string& format) {
    return format == "jpg" || format == "jpeg";
}

bool NeedsScale(float scale) {
    return scale < 0.999f || scale > 1.001f;
}

int ScaledDimension(int value, float scale) {
    if (value <= 0) {
        return 0;
    }
    const int scaled = static_cast<int>(static_cast<float>(value) * scale + 0.5f);
    return std::max(1, scaled);
}

cv::Mat PrepareJpegImage(const cv::Mat& image) {
    if (image.empty() || image.channels() != 4) {
        return image;
    }

    std::vector<cv::Mat> channels;
    cv::split(image, channels);
    if (channels.size() != 4) {
        return image;
    }

    cv::Mat bgr;
    cv::cvtColor(image, bgr, cv::COLOR_BGRA2BGR);
    cv::Mat white(image.rows, image.cols, CV_8UC3, cv::Scalar(255, 255, 255));
    bgr.copyTo(white, channels[3]);
    return white;
}

#if !defined(SDK_OPEN_HARMONY)
const char* ImgProcErrorName(ImgProc::ImgProcErrorCode code) {
    switch (code) {
        case ImgProc::SUCCESS:
            return "success";
        case ImgProc::LOAD_IMAGE_FAILED:
            return "load image failed";
        case ImgProc::SAVE_IMAGE_FAILED:
            return "save image failed";
        case ImgProc::INVALID_PARAMETER:
            return "invalid parameter";
        case ImgProc::MEMORY_ERROR:
            return "memory error";
        case ImgProc::FILE_PERMISSION_ERROR:
            return "file permission error";
        case ImgProc::FILE_NOT_FOUND:
            return "file not found";
        case ImgProc::UNSUPPORTED_FORMAT:
            return "unsupported format";
        case ImgProc::INVALID_IMAGE_SIZE:
            return "invalid image size";
        case ImgProc::PROCESS_ERROR:
            return "process error";
        case ImgProc::UNKNOWN_ERROR:
        default:
            return "unknown error";
    }
}
#endif

long long ElapsedMs(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start)
        .count();
}

} // namespace

SdkFormatConvertResult FormatConvertProcessor::Convert(const SdkFormatConvertRequest& request) const {
    SdkFormatConvertResult result;
    if (request.input_path.empty() || request.output_path.empty()) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "format convert input_path or output_path is empty";
        return result;
    }
    if (!FileExists(request.input_path)) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "format convert input image does not exist";
        return result;
    }

    const std::string format = NormalizeFormat(request.output_format);
    if (request.scale <= 0.0f) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "format convert scale must be positive";
        return result;
    }
    if (IsJpegFormat(format) && request.quality != 0 && (request.quality < 1 || request.quality > 100)) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "format convert quality must be between 1 and 100";
        return result;
    }
    const int metadata_dpi = ReadImageDpiFromMetadata(request.input_path);
    const int source_dpi = metadata_dpi > 0 ? metadata_dpi : NormalizeImageDpi(0);
    const auto convert_start = std::chrono::steady_clock::now();
#if defined(SDK_OPEN_HARMONY)
    const bool use_custom_encode = true;
#else
    const bool use_custom_encode = NeedsScale(request.scale) || (IsJpegFormat(format) && request.quality > 0);
    ImgProc::ImageEngine engine;
    ImgProc::ImgProcErrorCode convert_code = ImgProc::UNKNOWN_ERROR;
#endif
    SDK_PRIVATE_LOG_INFO("graphic.format_convert output_dpi format={} input={} output={} dpi={} source={} quality={} scale={}",
                         format,
                         SafePathSummary(request.input_path),
                         SafePathSummary(request.output_path),
                         source_dpi,
                         metadata_dpi > 0 ? "metadata" : "fallback",
                         request.quality,
                         request.scale);

#if defined(SDK_OPEN_HARMONY)
    bool converted = false;
#endif
    try {
        if (use_custom_encode) {
            cv::Mat source_image = cv::imread(request.input_path, cv::IMREAD_UNCHANGED);
            if (source_image.empty()) {
                result.code = ToCode(SdkStatusCode::ProviderCallFailed);
                result.message = "format convert failed: load image failed";
                return result;
            }

            cv::Mat output_image = source_image;
            if (NeedsScale(request.scale)) {
                const int target_width = ScaledDimension(source_image.cols, request.scale);
                const int target_height = ScaledDimension(source_image.rows, request.scale);
                cv::resize(source_image,
                           output_image,
                           cv::Size(target_width, target_height),
                           0,
                           0,
                           request.scale < 1.0f ? cv::INTER_AREA : cv::INTER_LANCZOS4);
            }

            bool written = false;
            if (IsJpegFormat(format)) {
                const int quality = request.quality > 0 ? request.quality : 95;
                std::vector<int> params;
                params.push_back(cv::IMWRITE_JPEG_QUALITY);
                params.push_back(quality);
                written = WriteImageWithDpiLocked(request.output_path, PrepareJpegImage(output_image), source_dpi, params);
            } else if (format == "png" || format == "tiff" || format == "tif") {
                written = WriteImageWithDpiLocked(request.output_path, output_image, source_dpi);
            } else {
                result.code = ToCode(SdkStatusCode::UnsupportedMethod);
                result.message = "unsupported output format: " + request.output_format;
                return result;
            }
#if defined(SDK_OPEN_HARMONY)
            converted = written;
#else
            convert_code = written ? ImgProc::SUCCESS : ImgProc::SAVE_IMAGE_FAILED;
#endif
#if !defined(SDK_OPEN_HARMONY)
        } else if (format == "jpg" || format == "jpeg") {
            convert_code = engine.convertToJpg(request.input_path,
                                               request.output_path,
                                               ImgProc::ImageEngine::HIGH_QUALITY,
                                               source_dpi);
        } else if (format == "png") {
            convert_code = engine.convertToPng(request.input_path,
                                               request.output_path,
                                               ImgProc::ImageEngine::MEDIUM_QUALITY,
                                               source_dpi,
                                               24);
        } else if (format == "tiff" || format == "tif") {
            convert_code = engine.convertToTiff({request.input_path},
                                                request.output_path,
                                                ImgProc::ImageEngine::TIFF_COLOR,
                                                ImgProc::ImageEngine::TIFF_COMP_LZW,
                                                source_dpi);
        } else {
            result.code = ToCode(SdkStatusCode::UnsupportedMethod);
            result.message = "unsupported output format: " + request.output_format;
            return result;
#endif
        }
    } catch (const std::exception& e) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = std::string("format convert failed: ") + e.what();
        SDK_PRIVATE_LOG_WARN("graphic.format_convert exception format={} input={} output={} elapsed_ms={} message={}",
                             request.output_format,
                             SafePathSummary(request.input_path),
                             SafePathSummary(request.output_path),
                             ElapsedMs(convert_start),
                             result.message);
        return result;
    } catch (...) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = "format convert failed";
        SDK_PRIVATE_LOG_WARN("graphic.format_convert exception format={} input={} output={} elapsed_ms={} message={}",
                             request.output_format,
                             SafePathSummary(request.input_path),
                             SafePathSummary(request.output_path),
                             ElapsedMs(convert_start),
                             result.message);
        return result;
    }

#if defined(SDK_OPEN_HARMONY)
    if (!converted) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = "format convert failed: save image failed";
        SDK_PRIVATE_LOG_WARN("graphic.format_convert failed format={} elapsed_ms={} message={}",
                             request.output_format,
                             ElapsedMs(convert_start),
                             result.message);
        return result;
    }
#else
    if (convert_code != ImgProc::SUCCESS) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = std::string("format convert failed: ") + ImgProcErrorName(convert_code);
        SDK_PRIVATE_LOG_WARN("graphic.format_convert failed format={} code={} elapsed_ms={} message={}",
                             request.output_format,
                             static_cast<int>(convert_code),
                             ElapsedMs(convert_start),
                             result.message);
        return result;
    }
#endif
    if (!FileExists(request.output_path)) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = "format convert output image was not created";
        SDK_PRIVATE_LOG_WARN("graphic.format_convert missing_output format={} output={} elapsed_ms={}",
                             request.output_format,
                             SafePathSummary(request.output_path),
                             ElapsedMs(convert_start));
        return result;
    }

    result.converted = true;
    result.output_path = request.output_path;
    result.message = "ok";
    SDK_PRIVATE_LOG_INFO("graphic.format_convert success format={} output={} size={} elapsed_ms={}",
                         request.output_format,
                         SafePathSummary(result.output_path),
                         FileSize(result.output_path),
                         ElapsedMs(convert_start));
    return result;
}

} // namespace graphic
} // namespace private_impl
} // namespace sdk
} // namespace editor
