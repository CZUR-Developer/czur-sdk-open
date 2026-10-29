// Copyright (c) 2026 CZUR Tech. All rights reserved.

#include "image_write_utils.h"
#include "graphic/format_convert_processor.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unistd.h>

#include "opencv2/imgcodecs.hpp"

namespace {

std::string TempPath(const std::string& name, const std::string& extension) {
    return "/tmp/" + name + "-" + std::to_string(::getpid()) + extension;
}

bool RemoveFile(const std::string& path) {
    return path.empty() || std::remove(path.c_str()) == 0;
}

void CleanupFiles(const std::vector<std::string>& paths) {
    for (std::vector<std::string>::const_iterator it = paths.begin(); it != paths.end(); ++it) {
        RemoveFile(*it);
    }
}

int CheckDpiAndSize(const std::string& path, int expected_width, int expected_height, int expected_dpi) {
    const cv::Mat image = cv::imread(path, cv::IMREAD_UNCHANGED);
    if (image.empty()) {
        std::cerr << "failed to read output image: " << path << std::endl;
        return 1;
    }
    if (image.cols != expected_width || image.rows != expected_height) {
        std::cerr << "unexpected image dimensions: got " << image.cols << "x" << image.rows
                  << ", expected " << expected_width << "x" << expected_height << std::endl;
        return 1;
    }
    const int actual_dpi = editor::sdk::private_impl::ReadImageDpiFromMetadata(path);
    if (actual_dpi != expected_dpi) {
        std::cerr << "unexpected dpi: got " << actual_dpi << ", expected " << expected_dpi
                  << " path=" << path << std::endl;
        return 1;
    }
    return 0;
}

int CheckFormatConvertPreservesDpi(const std::string& input_path,
                                   const std::string& output_path,
                                   const std::string& output_format,
                                   int expected_width,
                                   int expected_height,
                                   int expected_dpi,
                                   int quality = 0,
                                   float scale = 1.0f) {
    editor::sdk::private_impl::graphic::FormatConvertProcessor processor;
    editor::sdk::SdkFormatConvertRequest request;
    request.input_path = input_path;
    request.output_path = output_path;
    request.output_format = output_format;
    request.quality = quality;
    request.scale = scale;
    const editor::sdk::SdkFormatConvertResult result = processor.Convert(request);
    if (result.code != 0 || !result.converted) {
        std::cerr << "format convert failed: input=" << input_path
                  << " output=" << output_path
                  << " format=" << output_format
                  << " code=" << result.code
                  << " message=" << result.message << std::endl;
        return 1;
    }
    return CheckDpiAndSize(output_path, expected_width, expected_height, expected_dpi);
}

} // namespace

int main() {
    const std::string jpeg_path = TempPath("sdk-private-image-write", ".jpg");
    const std::string jpeg_bytes_path = TempPath("sdk-private-jpeg-bytes", ".jpg");
    const std::string png_path = TempPath("sdk-private-image-dpi", ".png");
    const std::string tiff_path = TempPath("sdk-private-image-dpi", ".tiff");
    const std::string bmp_path = TempPath("sdk-private-image-dpi", ".bmp");
    const std::string png_to_jpeg_path = TempPath("sdk-private-format-convert-png", ".jpg");
    const std::string tiff_to_jpeg_path = TempPath("sdk-private-format-convert-tiff", ".jpg");
    const std::string bmp_to_jpeg_path = TempPath("sdk-private-format-convert-bmp", ".jpg");
    const std::string jpeg_quality_path = TempPath("sdk-private-format-convert-quality", ".jpg");
    const std::string jpeg_scaled_path = TempPath("sdk-private-format-convert-scaled", ".jpg");
    const std::vector<std::string> cleanup_paths = {
        jpeg_path,
        jpeg_bytes_path,
        png_path,
        tiff_path,
        bmp_path,
        png_to_jpeg_path,
        tiff_to_jpeg_path,
        bmp_to_jpeg_path,
        jpeg_quality_path,
        jpeg_scaled_path
    };

    const cv::Mat image(18, 24, CV_8UC3, cv::Scalar(11, 22, 33));
    const int dpi = 410;

    if (!cv::imwrite(jpeg_path, image)) {
        std::cerr << "failed to write source jpeg" << std::endl;
        return 1;
    }
    if (!editor::sdk::private_impl::WriteDpiToExistingImage(jpeg_path, image.cols, image.rows, dpi)) {
        std::cerr << "WriteDpiToExistingImage failed" << std::endl;
        CleanupFiles(cleanup_paths);
        return 1;
    }
    if (CheckDpiAndSize(jpeg_path, image.cols, image.rows, dpi) != 0) {
        CleanupFiles(cleanup_paths);
        return 1;
    }

    std::vector<unsigned char> jpeg_bytes;
    if (!cv::imencode(".jpg", image, jpeg_bytes)) {
        std::cerr << "failed to encode jpeg bytes" << std::endl;
        CleanupFiles(cleanup_paths);
        return 1;
    }
    std::vector<unsigned char> locked_jpeg_bytes;
    std::vector<int> jpeg_params;
    jpeg_params.push_back(cv::IMWRITE_JPEG_QUALITY);
    jpeg_params.push_back(90);
    if (!editor::sdk::private_impl::EncodeJpegBytesLocked(image, &locked_jpeg_bytes, jpeg_params) ||
        cv::imdecode(locked_jpeg_bytes, cv::IMREAD_COLOR).empty()) {
        std::cerr << "EncodeJpegBytesLocked failed" << std::endl;
        CleanupFiles(cleanup_paths);
        return 1;
    }
    if (!editor::sdk::private_impl::WriteJpegBytesWithDpi(
            jpeg_bytes_path, jpeg_bytes, image.cols, image.rows, dpi)) {
        std::cerr << "WriteJpegBytesWithDpi failed" << std::endl;
        CleanupFiles(cleanup_paths);
        return 1;
    }
    if (CheckDpiAndSize(jpeg_bytes_path, image.cols, image.rows, dpi) != 0) {
        CleanupFiles(cleanup_paths);
        return 1;
    }

    if (!editor::sdk::private_impl::WriteImageWithDpiLocked(png_path, image, dpi)) {
        std::cerr << "failed to write png with dpi" << std::endl;
        CleanupFiles(cleanup_paths);
        return 1;
    }
    if (CheckDpiAndSize(png_path, image.cols, image.rows, dpi) != 0) {
        CleanupFiles(cleanup_paths);
        return 1;
    }

    if (!editor::sdk::private_impl::WriteImageWithDpiLocked(tiff_path, image, dpi)) {
        std::cerr << "failed to write tiff with dpi" << std::endl;
        CleanupFiles(cleanup_paths);
        return 1;
    }
    if (CheckDpiAndSize(tiff_path, image.cols, image.rows, dpi) != 0) {
        CleanupFiles(cleanup_paths);
        return 1;
    }

    if (!editor::sdk::private_impl::WriteImageWithDpiLocked(bmp_path, image, dpi)) {
        std::cerr << "failed to write bmp with dpi" << std::endl;
        CleanupFiles(cleanup_paths);
        return 1;
    }
    if (CheckDpiAndSize(bmp_path, image.cols, image.rows, dpi) != 0) {
        CleanupFiles(cleanup_paths);
        return 1;
    }

    if (CheckFormatConvertPreservesDpi(png_path, png_to_jpeg_path, "jpg", image.cols, image.rows, dpi) != 0) {
        CleanupFiles(cleanup_paths);
        return 1;
    }
    if (CheckFormatConvertPreservesDpi(tiff_path, tiff_to_jpeg_path, "jpg", image.cols, image.rows, dpi) != 0) {
        CleanupFiles(cleanup_paths);
        return 1;
    }
    if (CheckFormatConvertPreservesDpi(bmp_path, bmp_to_jpeg_path, "jpg", image.cols, image.rows, dpi) != 0) {
        CleanupFiles(cleanup_paths);
        return 1;
    }
    if (CheckFormatConvertPreservesDpi(jpeg_path, jpeg_quality_path, "jpg", image.cols, image.rows, dpi, 40) != 0) {
        CleanupFiles(cleanup_paths);
        return 1;
    }
    if (CheckFormatConvertPreservesDpi(jpeg_path, jpeg_scaled_path, "jpg", 12, 9, dpi, 40, 0.5f) != 0) {
        CleanupFiles(cleanup_paths);
        return 1;
    }

    CleanupFiles(cleanup_paths);
    return 0;
}
