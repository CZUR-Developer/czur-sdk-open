// Copyright (c) 2026 CZUR Tech. All rights reserved.

#include "image_write_utils.h"

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

int Expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << std::endl;
        return 1;
    }
    return 0;
}

} // namespace

int main() {
    const std::string output_path = TempPath("png-compression-smoke", ".png");
    const cv::Mat image(64, 96, CV_8UC3, cv::Scalar(44, 88, 132));
    const int dpi = 410;

    if (!editor::sdk::private_impl::WriteImageWithDpiLocked(output_path, image, dpi)) {
        std::cerr << "WriteImageWithDpiLocked failed for png output" << std::endl;
        return 1;
    }

    const cv::Mat written = cv::imread(output_path, cv::IMREAD_UNCHANGED);
    if (Expect(!written.empty(), "failed to read written png") != 0) {
        std::remove(output_path.c_str());
        return 1;
    }
    if (Expect(written.cols == image.cols && written.rows == image.rows, "written png size mismatch") != 0) {
        std::remove(output_path.c_str());
        return 1;
    }
    const int written_dpi = editor::sdk::private_impl::ReadImageDpiFromMetadata(output_path);
    if (Expect(written_dpi == dpi, "written png dpi mismatch") != 0) {
        std::remove(output_path.c_str());
        return 1;
    }

    std::remove(output_path.c_str());
    std::cout << "png_compression_smoke_test passed size=" << written.cols << "x" << written.rows
              << " requested_dpi=" << dpi
              << " written_dpi=" << written_dpi << std::endl;
    return 0;
}
