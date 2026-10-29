#include "private_graphic_provider.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <sys/stat.h>
#include <vector>

#include "graphic/image_codec_lock.h"
#include "image_write_utils.h"
#include "opencv2/opencv.hpp"
#include "support/private_sdk_logger.h"

namespace editor {
namespace sdk {
namespace private_impl {

namespace {

uint64_t FileSize(const std::string& path) {
    struct stat st;
    if (path.empty() || ::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        return 0;
    }
    return static_cast<uint64_t>(st.st_size);
}

bool FileExists(const std::string& path) {
    struct stat st;
    return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool IsKeepOriginalMode(const std::string& mode) {
    return mode == "keep_original" || mode == "KeepOriginal";
}

const char* DpiSourceName(bool has_requested_dpi, bool has_metadata_dpi) {
    if (has_requested_dpi) {
        return "request";
    }
    if (has_metadata_dpi) {
        return "metadata";
    }
    return "fallback";
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

std::string ExtensionForFormat(const std::string& format) {
    if (format == "png") {
        return "png";
    }
    if (format == "tif" || format == "tiff") {
        return "tiff";
    }
    return "jpg";
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

std::string ContentTypeForFormat(const std::string& format) {
    if (format == "png") {
        return "image/png";
    }
    if (format == "tif" || format == "tiff") {
        return "image/tiff";
    }
    return "image/jpeg";
}

std::string OutputPathForIndex(const SdkImageProcessRequest& request, int index) {
    if (index == 0) {
        return request.output_path;
    }
    const std::string dir = request.output_dir.empty() ? ParentDir(request.output_path) : request.output_dir;
    const std::string::size_type slash_pos = request.output_path.find_last_of("/\\");
    const std::string filename = slash_pos == std::string::npos ? request.output_path : request.output_path.substr(slash_pos + 1);
    const std::string::size_type dot_pos = filename.find_last_of('.');
    const std::string stem = dot_pos == std::string::npos ? filename : filename.substr(0, dot_pos);
    std::ostringstream name;
    name << stem << "_" << std::setw(3) << std::setfill('0') << (index + 1) << "." << ExtensionForFormat(request.output_format);
    return JoinPath(dir, name.str());
}

std::string TempPath(const std::string& output_dir, const std::string& label, int index, const std::string& extension = "jpg") {
    std::ostringstream name;
    name << ".sdk_image_process_" << static_cast<long long>(std::time(nullptr)) << "_"
         << static_cast<long long>(std::clock()) << "_" << label << "_"
         << std::setw(3) << std::setfill('0') << (index + 1) << "." << ExtensionForFormat(extension);
    return JoinPath(output_dir.empty() ? "." : output_dir, name.str());
}

void ReadImageInfo(const std::string& path, int* width, int* height, uint64_t* size) {
    if (!FileExists(path)) {
        return;
    }
    cv::Mat image;
    {
        std::lock_guard<std::mutex> lock(graphic::ImageCodecMutex());
        image = cv::imread(path, cv::IMREAD_UNCHANGED);
    }
    if (!image.empty()) {
        if (width != NULL) {
            *width = image.cols;
        }
        if (height != NULL) {
            *height = image.rows;
        }
    }
    if (size != NULL) {
        *size = FileSize(path);
    }
}

void FillImageInfo(SdkImageProcessOutput& output) {
    ReadImageInfo(output.path, &output.width, &output.height, &output.size);
}

SdkImageProcessOutput ConvertOutput(const SdkPageOutput& page, const std::string& path, const std::string& output_format) {
    SdkImageProcessOutput output;
    output.output_id = page.output_id;
    output.role = page.role;
    output.index = page.index;
    output.path = path;
    output.content_type = ContentTypeForFormat(output_format);
    FillImageInfo(output);
    return output;
}

} // namespace

std::string PrivateGraphicProvider::ProviderName() const {
    return "czur-graphic-provider";
}

SdkImageProcessResult PrivateGraphicProvider::Process(const SdkImageProcessRequest& request) {
    SDK_PRIVATE_LOG_INFO("graphic.process begin input={} output={} mode={} color={} format={}",
                         SafePathSummary(request.input_path),
                         SafePathSummary(request.output_path),
                         request.page_processing,
                         request.color_mode,
                         request.output_format);
    SdkImageProcessResult result;
    if (request.input_path.empty() || request.output_path.empty()) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "image.process input_path or output_path is empty";
        return result;
    }
    if (!FileExists(request.input_path)) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "image.process input image does not exist";
        return result;
    }

    const int metadata_dpi = ReadImageDpiFromMetadata(request.input_path);
    SDK_PRIVATE_LOG_INFO("graphic.process dpi_request input={} requested_dpi={} metadata_dpi={} source={}",
                         SafePathSummary(request.input_path),
                         request.dpi,
                         metadata_dpi,
                         DpiSourceName(request.dpi > 0, metadata_dpi > 0));

    const std::string output_dir = request.output_dir.empty() ? ParentDir(request.output_path) : request.output_dir;
    const std::string page_temp_path = TempPath(output_dir, "page", 0);
    SdkPageProcessRequest page_request;
    page_request.input_path = request.input_path;
    page_request.output_path = page_temp_path;
    page_request.output_dir = output_dir;
    page_request.page_processing = request.page_processing.empty() ? "keep_original" : request.page_processing;
    page_request.dpi = request.dpi;
    page_request.single_page = request.single_page;
    page_request.curved_book = request.curved_book;
    page_request.selected_area_rect = request.selected_area_rect;
    page_request.selected_area_source_width = request.selected_area_source_width;
    page_request.selected_area_source_height = request.selected_area_source_height;
    page_request.scan_device_type = request.scan_device_type;

    SdkPageProcessResult page_result = ProcessPage(page_request);
    if (!IsOkStatusCode(page_result.code) || page_result.outputs.empty()) {
        result.code = page_result.code;
        result.message = page_result.message.empty() ? "image.process page processing failed" : page_result.message;
        SDK_PRIVATE_LOG_WARN("graphic.process page_failed code={} message={}", result.code, result.message);
        return result;
    }

    std::vector<std::string> temporary_paths;
    temporary_paths.push_back(page_temp_path);
    for (std::vector<SdkPageOutput>::const_iterator it = page_result.outputs.begin(); it != page_result.outputs.end(); ++it) {
        const int index = static_cast<int>(result.outputs.size());
        temporary_paths.push_back(it->path);
        const std::string color_path = TempPath(output_dir, "color", index, ExtensionFromPath(it->path));
        SdkColorModeRequest color_request;
        color_request.input_path = it->path;
        color_request.output_path = color_path;
        color_request.color_mode = request.color_mode.empty() ? "no_optimize" : request.color_mode;
        color_request.dpi = it->dpi > 0 ? it->dpi : request.dpi;
        SdkColorModeResult color_result = ApplyColorMode(color_request);
        if (!IsOkStatusCode(color_result.code)) {
            result.code = color_result.code;
            result.message = color_result.message.empty() ? "image.process color mode failed" : color_result.message;
            SDK_PRIVATE_LOG_WARN("graphic.process color_failed code={} message={}", result.code, result.message);
            return result;
        }
        temporary_paths.push_back(color_path);

        const std::string final_path = OutputPathForIndex(request, index);
        SdkFormatConvertRequest format_request;
        format_request.input_path = color_result.output_path.empty() ? color_path : color_result.output_path;
        format_request.output_path = final_path;
        format_request.output_format = request.output_format.empty() ? "jpg" : request.output_format;
        SdkFormatConvertResult format_result = ConvertImageFormat(format_request);
        if (!IsOkStatusCode(format_result.code)) {
            result.code = format_result.code;
            result.message = format_result.message.empty() ? "image.process format convert failed" : format_result.message;
            SDK_PRIVATE_LOG_WARN("graphic.process format_failed code={} message={}", result.code, result.message);
            return result;
        }

        result.outputs.push_back(ConvertOutput(*it,
                                               format_result.output_path.empty() ? final_path : format_result.output_path,
                                               format_request.output_format));
        if (result.output_path.empty()) {
            result.output_path = result.outputs.back().path;
        }
    }

    for (std::vector<std::string>::const_iterator it = temporary_paths.begin(); it != temporary_paths.end(); ++it) {
        if (*it != request.input_path && *it != request.output_path) {
            std::remove(it->c_str());
        }
    }
    result.processed = true;
    result.message = "ok";
    SDK_PRIVATE_LOG_INFO("graphic.process end code={} processed={} outputs={} output={}",
                         result.code,
                         result.processed,
                         result.outputs.size(),
                         SafePathSummary(result.output_path));
    return result;
}

SdkPageProcessResult PrivateGraphicProvider::ProcessPage(const SdkPageProcessRequest& request) {
    SDK_PRIVATE_LOG_INFO("graphic.page_process begin mode={} device={} input={} laser={} output_dir={}",
                         request.page_processing,
                         SafeDeviceId(request.device_id),
                         SafePathSummary(request.input_path),
                         SafePathSummary(request.laser_path),
                         SafePathSummary(request.output_dir));
    SdkPageProcessRequest resolved_request = request;
    const int metadata_dpi = ReadImageDpiFromMetadata(request.input_path);
    resolved_request.dpi = ResolveImageDpi(request.input_path, request.dpi);
    SDK_PRIVATE_LOG_INFO("graphic.page_process dpi_resolved input={} requested_dpi={} metadata_dpi={} resolved_dpi={} source={}",
                         SafePathSummary(request.input_path),
                         request.dpi,
                         metadata_dpi,
                         resolved_request.dpi,
                         DpiSourceName(request.dpi > 0, metadata_dpi > 0));
    if (IsKeepOriginalMode(resolved_request.page_processing)) {
        SdkPageProcessResult result;
        SdkPageOutput output;
        output.output_id = "page-001";
        output.role = "page";
        output.index = 0;
        output.path = resolved_request.input_path;
        output.content_type = "image/jpeg";
        output.dpi = resolved_request.dpi;
        ReadImageInfo(output.path, &output.width, &output.height, &output.size);
        result.outputs.push_back(output);
        result.output_path = output.path;
        result.processed = true;
        result.message = "ok";
        SDK_PRIVATE_LOG_INFO("graphic.page_process keep_original output={} size={}",
                             SafePathSummary(result.output_path),
                             result.outputs.front().size);
        return result;
    }
    if (resolved_request.page_processing == "curved_book") {
        SdkPageProcessResult result = curved_book_processor_.Process(resolved_request);
        SDK_PRIVATE_LOG_INFO("graphic.page_process end mode={} code={} processed={} unsupported={} outputs={} output={}",
                             resolved_request.page_processing,
                             result.code,
                             result.processed,
                             result.unsupported,
                             result.outputs.size(),
                             SafePathSummary(result.output_path));
        return result;
    }
    if (resolved_request.page_processing == "single_page" || resolved_request.page_processing == "selected_area") {
        SdkPageProcessResult result = single_page_processor_.Process(resolved_request);
        SDK_PRIVATE_LOG_INFO("graphic.page_process end mode={} code={} processed={} unsupported={} outputs={} output={}",
                             resolved_request.page_processing,
                             result.code,
                             result.processed,
                             result.unsupported,
                             result.outputs.size(),
                             SafePathSummary(result.output_path));
        return result;
    }
    SdkPageProcessResult result;
    result.code = ToCode(SdkStatusCode::UnsupportedMethod);
    result.message = "page processing provider not implemented for mode: " + resolved_request.page_processing;
    result.unsupported = true;
    SDK_PRIVATE_LOG_WARN("graphic.page_process unsupported mode={} code={}", resolved_request.page_processing, result.code);
    return result;
}

SdkColorModeResult PrivateGraphicProvider::ApplyColorMode(const SdkColorModeRequest& request) {
    SDK_PRIVATE_LOG_INFO("graphic.color_mode begin mode={} input={} output={}",
                         request.color_mode,
                         SafePathSummary(request.input_path),
                         SafePathSummary(request.output_path));
    SdkColorModeRequest resolved_request = request;
    const int metadata_dpi = ReadImageDpiFromMetadata(request.input_path);
    resolved_request.dpi = ResolveImageDpi(request.input_path, request.dpi);
    SDK_PRIVATE_LOG_INFO("graphic.color_mode dpi_resolved input={} requested_dpi={} metadata_dpi={} resolved_dpi={} source={}",
                         SafePathSummary(request.input_path),
                         request.dpi,
                         metadata_dpi,
                         resolved_request.dpi,
                         DpiSourceName(request.dpi > 0, metadata_dpi > 0));
    SdkColorModeResult result = color_mode_processor_.Apply(resolved_request);
    if (IsOkStatusCode(result.code)) {
        const std::string output_path = result.output_path.empty() ? resolved_request.output_path : result.output_path;
        ReadImageInfo(output_path, &result.width, &result.height, &result.size);
    }
    SDK_PRIVATE_LOG_INFO("graphic.color_mode end mode={} code={} processed={} unsupported={} output={}",
                         request.color_mode,
                         result.code,
                         result.processed,
                         result.unsupported,
                         SafePathSummary(result.output_path));
    return result;
}

SdkFormatConvertResult PrivateGraphicProvider::ConvertImageFormat(const SdkFormatConvertRequest& request) {
    SDK_PRIVATE_LOG_INFO("graphic.format_convert begin format={} input={} output={}",
                         request.output_format,
                         SafePathSummary(request.input_path),
                         SafePathSummary(request.output_path));
    SdkFormatConvertResult result = format_convert_processor_.Convert(request);
    SDK_PRIVATE_LOG_INFO("graphic.format_convert end format={} code={} converted={} passthrough={} output={}",
                         request.output_format,
                         result.code,
                         result.converted,
                         result.passthrough,
                         SafePathSummary(result.output_path));
    return result;
}

SdkThumbnailResult PrivateGraphicProvider::GenerateThumbnail(const SdkThumbnailRequest& request) {
    SDK_PRIVATE_LOG_INFO("graphic.thumbnail begin kind={} input={} output={} max={}x{} keep_aspect={}",
                         request.thumbnail_kind,
                         SafePathSummary(request.input_path),
                         SafePathSummary(request.output_path),
                         request.max_width,
                         request.max_height,
                         request.keep_aspect_ratio);
    SdkThumbnailResult result = thumbnail_processor_.Generate(request);
    SDK_PRIVATE_LOG_INFO("graphic.thumbnail end kind={} code={} generated={} output={} size={} dimensions={}x{}",
                         request.thumbnail_kind,
                         result.code,
                         result.generated,
                         SafePathSummary(result.output_path),
                         result.size,
                         result.width,
                         result.height);
    return result;
}

} // namespace private_impl
} // namespace sdk
} // namespace editor
