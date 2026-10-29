#include "graphic/thumbnail_processor.h"

#include <exception>
#include <sys/stat.h>

#if defined(SDK_OPEN_HARMONY)
#include <algorithm>
#include <mutex>

#include "graphic/image_codec_lock.h"
#include "opencv2/opencv.hpp"
#else
#include "imgengine.h"
#endif

namespace editor {
namespace sdk {
namespace private_impl {
namespace graphic {

namespace {

bool FileExists(const std::string& path) {
    struct stat st;
    return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

} // namespace

SdkThumbnailResult ThumbnailProcessor::Generate(const SdkThumbnailRequest& request) const {
    SdkThumbnailResult result;
    if (request.input_path.empty() || request.output_path.empty()) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "thumbnail input_path or output_path is empty";
        return result;
    }

#if defined(SDK_OPEN_HARMONY)
    if (!FileExists(request.input_path)) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "thumbnail input image does not exist";
        return result;
    }
    const int max_width = request.max_width > 0 ? request.max_width : 320;
    const int max_height = request.max_height > 0 ? request.max_height : 320;
    try {
        std::lock_guard<std::mutex> lock(ImageCodecMutex());
        const cv::Mat source = cv::imread(request.input_path, cv::IMREAD_UNCHANGED);
        if (source.empty()) {
            result.code = ToCode(SdkStatusCode::ProviderCallFailed);
            result.message = "thumbnail generation failed: load image failed";
            return result;
        }

        int width = max_width;
        int height = max_height;
        if (request.keep_aspect_ratio) {
            const double scale = std::min(1.0,
                                          std::min(static_cast<double>(max_width) / source.cols,
                                                   static_cast<double>(max_height) / source.rows));
            width = std::max(1, static_cast<int>(source.cols * scale + 0.5));
            height = std::max(1, static_cast<int>(source.rows * scale + 0.5));
        }
        cv::Mat thumbnail;
        cv::resize(source,
                   thumbnail,
                   cv::Size(width, height),
                   0,
                   0,
                   width < source.cols || height < source.rows ? cv::INTER_AREA : cv::INTER_LANCZOS4);
        if (!cv::imwrite(request.output_path, thumbnail)) {
            result.code = ToCode(SdkStatusCode::ProviderCallFailed);
            result.message = "thumbnail generation failed: save image failed";
            return result;
        }
        result.generated = true;
        result.output_path = request.output_path;
        result.width = thumbnail.cols;
        result.height = thumbnail.rows;
        struct stat value;
        if (::stat(request.output_path.c_str(), &value) == 0 && S_ISREG(value.st_mode)) {
            result.size = static_cast<uint64_t>(value.st_size);
        }
        result.message = "ok";
        return result;
    } catch (const std::exception& error) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = std::string("thumbnail generation failed: ") + error.what();
        return result;
    } catch (...) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = "thumbnail generation failed";
        return result;
    }
#else
    ImgProc::LimitArea limit_area;
    limit_area.width = request.max_width > 0 ? request.max_width : 320;
    limit_area.height = request.max_height > 0 ? request.max_height : 320;

    ImgProc::ImgProcResult thumbnail_result;
    try {
        ImgProc::ImageEngine engine;
        thumbnail_result = engine.createThumbnail(request.input_path, request.output_path, limit_area);
    } catch (const std::exception& e) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = std::string("thumbnail generation failed: ") + e.what();
        return result;
    } catch (...) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = "thumbnail generation failed";
        return result;
    }

    const std::string output_path = thumbnail_result.dst_info.path.empty() ? request.output_path : thumbnail_result.dst_info.path;
    if (thumbnail_result.error_code != ImgProc::SUCCESS || !FileExists(output_path)) {
        result.code = ToCode(SdkStatusCode::ProviderCallFailed);
        result.message = thumbnail_result.error_msg.empty() ? "thumbnail generation failed" : thumbnail_result.error_msg;
        return result;
    }

    result.generated = true;
    result.output_path = output_path;
    result.width = thumbnail_result.dst_info.width;
    result.height = thumbnail_result.dst_info.height;
    result.size = thumbnail_result.dst_info.size;
    result.message = "ok";
    return result;
#endif
}

} // namespace graphic
} // namespace private_impl
} // namespace sdk
} // namespace editor
