#include "private_device_frame_codec.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool CheckStreamPolicy() {
    using editor::sdk::private_impl::device_frame_codec::BuildStreamOptions;
    using editor::sdk::private_impl::device_frame_codec::NeedsBgrFrame;

    const editor::devices::DeviceStreamOptions ohos_mjpeg = BuildStreamOptions("mjpeg", true);
    const editor::devices::DeviceStreamOptions ohos_bgr = BuildStreamOptions("bgr24", true);
    const editor::devices::DeviceStreamOptions desktop_bgr = BuildStreamOptions("bgr24", false);
    const editor::devices::DeviceStreamOptions desktop_mjpeg = BuildStreamOptions("mjpeg", false);
    if (ohos_mjpeg.decodeFrame || !ohos_mjpeg.deliverNativeMjpeg ||
        ohos_bgr.decodeFrame || !ohos_bgr.deliverNativeMjpeg ||
        !desktop_bgr.decodeFrame || desktop_bgr.deliverNativeMjpeg ||
        desktop_mjpeg.decodeFrame || !desktop_mjpeg.deliverNativeMjpeg) {
        std::cerr << "stream decode policy mismatch" << std::endl;
        return false;
    }
    if (NeedsBgrFrame("mjpeg", false, false) ||
        !NeedsBgrFrame("bgr24", false, false) ||
        !NeedsBgrFrame("mjpeg", true, false) ||
        !NeedsBgrFrame("mjpeg", false, true)) {
        std::cerr << "on-demand BGR policy mismatch" << std::endl;
        return false;
    }
    return true;
}

bool CheckRealJpegDecode() {
    cv::Mat source(2, 3, CV_8UC3);
    source.at<cv::Vec3b>(0, 0) = cv::Vec3b(10, 20, 230);
    source.at<cv::Vec3b>(0, 1) = cv::Vec3b(20, 210, 30);
    source.at<cv::Vec3b>(0, 2) = cv::Vec3b(220, 30, 40);
    source.at<cv::Vec3b>(1, 0) = cv::Vec3b(40, 50, 200);
    source.at<cv::Vec3b>(1, 1) = cv::Vec3b(50, 190, 60);
    source.at<cv::Vec3b>(1, 2) = cv::Vec3b(180, 70, 80);

    std::vector<uint8_t> jpeg;
    std::vector<int> params;
    params.push_back(cv::IMWRITE_JPEG_QUALITY);
    params.push_back(100);
    if (!cv::imencode(".jpg", source, jpeg, params) || jpeg.empty()) {
        std::cerr << "failed to create JPEG fixture" << std::endl;
        return false;
    }

    editor::devices::GenericFrame frame;
    frame.format = editor::devices::DEVICE_FRAME_FORMAT_MJPEG;
    frame.native_mjpeg = jpeg;
    if (!editor::sdk::private_impl::device_frame_codec::DecodeNativeMjpegFrame(&frame)) {
        std::cerr << "failed to decode native MJPEG" << std::endl;
        return false;
    }
    if (frame.width != source.cols || frame.height != source.rows ||
        frame.data.size() != source.total() * source.elemSize() ||
        frame.native_mjpeg != jpeg ||
        frame.format != editor::devices::DEVICE_FRAME_FORMAT_MJPEG) {
        std::cerr << "decoded frame contract mismatch" << std::endl;
        return false;
    }

    editor::devices::GenericFrame invalid;
    invalid.native_mjpeg.assign(8, 0x42);
    if (editor::sdk::private_impl::device_frame_codec::DecodeNativeMjpegFrame(&invalid) ||
        !invalid.data.empty()) {
        std::cerr << "invalid JPEG was accepted" << std::endl;
        return false;
    }
    return true;
}

bool CheckRuntimeConfigPath() {
    using editor::sdk::private_impl::device_frame_codec::OhosDeviceConfigPath;
    const std::string expected = "/data/storage/el2/base/files/sdkopen/configs/device_config_compact.conf";
    if (OhosDeviceConfigPath("/data/storage/el2/base/files/sdkopen") != expected ||
        OhosDeviceConfigPath("/data/storage/el2/base/files/sdkopen/") != expected) {
        std::cerr << "OHOS runtime config path mismatch" << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main() {
    return CheckStreamPolicy() && CheckRealJpegDecode() && CheckRuntimeConfigPath() ? 0 : 1;
}
