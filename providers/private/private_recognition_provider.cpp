#include "private_recognition_provider.h"

#include <algorithm>
#include <cctype>
#include <vector>

#include "ZXing/BarcodeFormat.h"
#include "ZXing/ReadBarcode.h"
#include "ZXing/Result.h"
#include "ZXing/TextUtfEncoding.h"
#include "opencv2/opencv.hpp"

namespace editor {
namespace sdk {
namespace private_impl {

namespace {

ZXing::BarcodeFormat ParseBarcodeFormat(const std::string& format) {
    std::string value = format;
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (value == "qrcode" || value == "qr_code") return ZXing::BarcodeFormat::QR_CODE;
    if (value == "pdf417" || value == "pdf_417") return ZXing::BarcodeFormat::PDF_417;
    if (value == "code128" || value == "code_128") return ZXing::BarcodeFormat::CODE_128;
    if (value == "code39" || value == "code_39") return ZXing::BarcodeFormat::CODE_39;
    if (value == "ean13" || value == "ean_13") return ZXing::BarcodeFormat::EAN_13;
    if (value == "ean8" || value == "ean_8") return ZXing::BarcodeFormat::EAN_8;
    if (value == "upca" || value == "upc_a") return ZXing::BarcodeFormat::UPC_A;
    if (value == "upce" || value == "upc_e") return ZXing::BarcodeFormat::UPC_E;
    if (value == "codabar") return ZXing::BarcodeFormat::CODABAR;
    if (value == "datamatrix" || value == "data_matrix") return ZXing::BarcodeFormat::DATA_MATRIX;
    if (value == "aztec") return ZXing::BarcodeFormat::AZTEC;
    if (value == "maxicode") return ZXing::BarcodeFormat::MAXICODE;
    if (value == "itf") return ZXing::BarcodeFormat::ITF;
    if (value == "rss14" || value == "rss_14") return ZXing::BarcodeFormat::RSS_14;
    if (value == "rss_expanded") return ZXing::BarcodeFormat::RSS_EXPANDED;
    if (value == "upc_ean_extension") return ZXing::BarcodeFormat::UPC_EAN_EXTENSION;
    return ZXing::BarcodeFormat::FORMAT_COUNT;
}

} // namespace

std::string PrivateRecognitionProvider::ProviderName() const {
    return "czur-recognition-provider";
}

SdkBarcodeDetectResult PrivateRecognitionProvider::DetectBarcode(const SdkBarcodeDetectRequest& request) {
    SdkBarcodeDetectResult result;
    cv::Mat image = cv::imread(request.input_path, cv::IMREAD_COLOR);
    if (image.empty()) {
        result.code = ToCode(SdkStatusCode::InvalidParams);
        result.message = "invalid image";
        return result;
    }
    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    if (!gray.isContinuous()) {
        gray = gray.clone();
    }
    std::vector<ZXing::BarcodeFormat> formats;
    for (std::vector<std::string>::const_iterator it = request.formats.begin(); it != request.formats.end(); ++it) {
        const ZXing::BarcodeFormat format = ParseBarcodeFormat(*it);
        if (format != ZXing::BarcodeFormat::FORMAT_COUNT) {
            formats.push_back(format);
        }
    }
    if (formats.empty()) {
        formats.push_back(ZXing::BarcodeFormat::QR_CODE);
        formats.push_back(ZXing::BarcodeFormat::PDF_417);
        formats.push_back(ZXing::BarcodeFormat::CODE_128);
        formats.push_back(ZXing::BarcodeFormat::EAN_13);
        formats.push_back(ZXing::BarcodeFormat::EAN_8);
        formats.push_back(ZXing::BarcodeFormat::UPC_A);
        formats.push_back(ZXing::BarcodeFormat::UPC_E);
        formats.push_back(ZXing::BarcodeFormat::CODE_39);
        formats.push_back(ZXing::BarcodeFormat::CODABAR);
    }
    const ZXing::Result zx_result = ZXing::ReadBarcode(
        gray.cols, gray.rows, gray.data, static_cast<int>(gray.step[0]), formats, true, true);
    result.input_path = request.input_path;
    result.width = image.cols;
    result.height = image.rows;
    if (!zx_result.isValid()) {
        result.detected = false;
        return result;
    }

    SdkBarcodeResult barcode;
    barcode.format = static_cast<int>(zx_result.format());
    barcode.format_name = ZXing::ToString(zx_result.format());
    barcode.text = ZXing::TextUtfEncoding::ToUtf8(zx_result.text());
    const std::vector<ZXing::ResultPoint>& points = zx_result.resultPoints();
    for (std::vector<ZXing::ResultPoint>::const_iterator it = points.begin(); it != points.end(); ++it) {
        SdkPoint2f point;
        point.x = it->x();
        point.y = it->y();
        barcode.points.push_back(point);
    }
    result.detected = true;
    result.barcodes.push_back(barcode);
    return result;
}

} // namespace private_impl
} // namespace sdk
} // namespace editor
