#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "opencv2/core.hpp"

namespace editor {
namespace sdk {
namespace private_impl {

int NormalizeImageDpi(int dpi);

int ReadImageDpiFromMetadata(const std::string& input_path);

int ResolveImageDpi(const std::string& input_path, int preferred_dpi);

// 生成带 DPI 元数据的 JPEG 字节，供 Provider 在写入原图前完成元数据封装。
std::vector<unsigned char> BuildJpegBytesWithDpi(const std::vector<unsigned char>& jpeg_data,
                                                 int dpi);

bool WriteJpegBytesWithDpi(const std::string& path,
                           const unsigned char* jpeg_data,
                           std::size_t jpeg_size,
                           int width,
                           int height,
                           int dpi);

bool WriteJpegBytesWithDpi(const std::string& path,
                           const std::vector<unsigned char>& jpeg_data,
                           int width,
                           int height,
                           int dpi);

bool EncodeJpegBytesLocked(const cv::Mat& image,
                           std::vector<unsigned char>* jpeg_data,
                           const std::vector<int>& params = std::vector<int>());

bool WriteImageLocked(const std::string& path,
                      const cv::Mat& image,
                      const std::vector<int>& params = std::vector<int>());

bool WriteImageWithDpiLocked(const std::string& path,
                             const cv::Mat& image,
                             int dpi,
                             const std::vector<int>& params = std::vector<int>());

bool WriteDpiToExistingImage(const std::string& path,
                             int width,
                             int height,
                             int dpi);

} // namespace private_impl
} // namespace sdk
} // namespace editor
