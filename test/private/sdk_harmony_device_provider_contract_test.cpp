#include "private_device_frame_codec.h"
#include "private_device_provider.h"

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

bool CheckRealtimeRectCacheSelection() {
    using editor::sdk::private_impl::RealtimeRectCacheMutation;
    using editor::sdk::private_impl::SelectRealtimeRectCacheMutation;

    const std::shared_ptr<std::atomic<uint64_t> > current(new std::atomic<uint64_t>(1));
    const std::shared_ptr<std::atomic<uint64_t> > replacement(new std::atomic<uint64_t>(1));
    if (SelectRealtimeRectCacheMutation(current, true, current, true) !=
            RealtimeRectCacheMutation::Replace ||
        SelectRealtimeRectCacheMutation(current, true, current, false) !=
            RealtimeRectCacheMutation::Clear ||
        SelectRealtimeRectCacheMutation(current, false, current, true) !=
            RealtimeRectCacheMutation::Ignore ||
        SelectRealtimeRectCacheMutation(replacement, true, current, true) !=
            RealtimeRectCacheMutation::Ignore ||
        SelectRealtimeRectCacheMutation(
            std::shared_ptr<std::atomic<uint64_t> >(), true, current, false) !=
            RealtimeRectCacheMutation::Ignore) {
        std::cerr << "realtime rect cache selection mismatch" << std::endl;
        return false;
    }
    return true;
}

bool CheckFullFrameDetection() {
    using editor::sdk::SdkRect4P;
    using editor::sdk::private_impl::IsFullFrameRect;
    SdkRect4P rect;
    rect.right_top.x = 1535;
    rect.right_down.x = 1535;
    rect.right_down.y = 1151;
    rect.left_down.y = 1151;
    if (!IsFullFrameRect(rect, 1536, 1152)) {
        std::cerr << "full-frame fallback was accepted as a paper boundary" << std::endl;
        return false;
    }
    // CZCV may return width/height rather than the last pixel coordinates.
    rect.right_top.x = rect.right_down.x = 1536;
    rect.right_down.y = rect.left_down.y = 1152;
    if (!IsFullFrameRect(rect, 1536, 1152)) {
        std::cerr << "full-frame tolerance mismatch" << std::endl;
        return false;
    }
    rect.left_top.x = rect.left_down.x = 300;
    rect.right_top.x = rect.right_down.x = 1100;
    rect.left_top.y = rect.right_top.y = 150;
    rect.left_down.y = rect.right_down.y = 1000;
    if (IsFullFrameRect(rect, 1536, 1152) || IsFullFrameRect(rect, 0, 0)) {
        std::cerr << "valid paper boundary was rejected" << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main() {
    return CheckStreamPolicy() && CheckRealJpegDecode() && CheckRuntimeConfigPath() &&
                   CheckRealtimeRectCacheSelection() && CheckFullFrameDetection()
               ? 0
               : 1;
}
