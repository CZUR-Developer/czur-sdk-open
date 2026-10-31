#include "private_device_frame_codec.h"
#include "private_device_provider.h"

#include <cstdint>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
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

bool CheckCaptureTimeoutDiagnostics() {
    using editor::sdk::private_impl::BuildCaptureTimeoutMessage;
    const std::string stream_error =
        "USB UVC payload lost during still transaction; pair cannot be associated safely";
    if (BuildCaptureTimeoutMessage(true, false, stream_error) !=
            "capture timeout: " + stream_error ||
        BuildCaptureTimeoutMessage(false, false, stream_error) != "capture timeout" ||
        BuildCaptureTimeoutMessage(true, true, stream_error) != "capture timeout" ||
        BuildCaptureTimeoutMessage(true, false, "") != "capture timeout") {
        std::cerr << "capture timeout diagnostic guard mismatch" << std::endl;
        return false;
    }
    return true;
}

bool CheckRealtimeRectCacheSelection() {
    using editor::sdk::private_impl::CanPublishRealtimeRectCache;
    using editor::sdk::private_impl::IsExpectedRealtimeRectSession;
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
    if (!IsExpectedRealtimeRectSession(current, true, current) ||
        IsExpectedRealtimeRectSession(current, false, current) ||
        IsExpectedRealtimeRectSession(replacement, true, current)) {
        std::cerr << "stale realtime rect session was allowed to publish" << std::endl;
        return false;
    }
    if (!CanPublishRealtimeRectCache(current, true, current, 1536, 1152, 1536, 1152) ||
        CanPublishRealtimeRectCache(current, true, current, 1920, 1080, 1536, 1152) ||
        CanPublishRealtimeRectCache(replacement, true, current, 1536, 1152, 1536, 1152)) {
        std::cerr << "stale or mismatched realtime rect cache was allowed to publish" << std::endl;
        return false;
    }
    return true;
}

bool CheckFrameCaptureRectSelection() {
    using editor::sdk::private_impl::NeedsCurrentFrameRectDetection;
    using editor::sdk::private_impl::SelectFrameCaptureRectCache;

    const int stale_preview_rect = 7;
    const int current_frame_no_rect = 0;
    if (!NeedsCurrentFrameRectDetection(true, false) ||
        !NeedsCurrentFrameRectDetection(false, true) ||
        NeedsCurrentFrameRectDetection(false, false) ||
        SelectFrameCaptureRectCache(&current_frame_no_rect, stale_preview_rect) != 0 ||
        SelectFrameCaptureRectCache(static_cast<const int*>(NULL), stale_preview_rect) != 7) {
        std::cerr << "capture did not prefer its same-frame detection result" << std::endl;
        return false;
    }
    return true;
}

bool CheckLatestItemReplacement() {
    std::mutex mu;
    std::condition_variable cv;
    bool first_started = false;
    bool release_first = false;
    std::vector<int> processed;
    editor::sdk::private_impl::LatestItemWorker<int> worker(
        [&](int value) {
            std::unique_lock<std::mutex> lock(mu);
            processed.push_back(value);
            if (value == 1) {
                first_started = true;
                cv.notify_all();
                cv.wait(lock, [&]() { return release_first; });
            }
            cv.notify_all();
        });

    worker.Submit(1);
    {
        std::unique_lock<std::mutex> lock(mu);
        if (!cv.wait_for(lock, std::chrono::seconds(2), [&]() { return first_started; })) {
            std::cerr << "latest worker did not start first item" << std::endl;
            release_first = true;
            lock.unlock();
            cv.notify_all();
            return false;
        }
    }
    worker.Submit(2);
    worker.Submit(3);
    {
        std::lock_guard<std::mutex> lock(mu);
        release_first = true;
    }
    cv.notify_all();
    {
        std::unique_lock<std::mutex> lock(mu);
        if (!cv.wait_for(lock, std::chrono::seconds(2), [&]() { return processed.size() == 2; })) {
            std::cerr << "latest worker did not drain replacement item" << std::endl;
            return false;
        }
        if (processed[0] != 1 || processed[1] != 3) {
            std::cerr << "latest worker accumulated stale items" << std::endl;
            return false;
        }
    }
    worker.Stop();
    return !worker.Submit(4);
}

bool CheckWorkerStopWaits() {
    std::mutex mu;
    std::condition_variable cv;
    bool processing = false;
    bool release = false;
    std::atomic<bool> stop_started(false);
    std::atomic<bool> stop_returned(false);
    editor::sdk::private_impl::LatestItemWorker<int> worker(
        [&](int) {
            std::unique_lock<std::mutex> lock(mu);
            processing = true;
            cv.notify_all();
            cv.wait(lock, [&]() { return release; });
        });
    worker.Submit(1);
    {
        std::unique_lock<std::mutex> lock(mu);
        if (!cv.wait_for(lock, std::chrono::seconds(2), [&]() { return processing; })) {
            std::cerr << "stop-wait worker did not start" << std::endl;
            release = true;
            lock.unlock();
            cv.notify_all();
            return false;
        }
    }
    std::thread stopper([&]() {
        stop_started.store(true);
        worker.Stop();
        stop_returned.store(true);
    });
    while (!stop_started.load()) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    if (stop_returned.load()) {
        std::cerr << "worker stop returned before in-flight processing completed" << std::endl;
        {
            std::lock_guard<std::mutex> lock(mu);
            release = true;
        }
        cv.notify_all();
        stopper.join();
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mu);
        release = true;
    }
    cv.notify_all();
    stopper.join();
    return stop_returned.load();
}

bool CheckWorkerSurvivesProcessorFailure() {
    std::mutex mu;
    std::condition_variable cv;
    bool recovered = false;
    editor::sdk::private_impl::LatestItemWorker<int> worker(
        [&](int value) {
            if (value == 1) {
                throw std::runtime_error("expected processor failure");
            }
            {
                std::lock_guard<std::mutex> lock(mu);
                recovered = value == 2;
            }
            cv.notify_all();
        });
    worker.Submit(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    worker.Submit(2);
    {
        std::unique_lock<std::mutex> lock(mu);
        if (!cv.wait_for(lock, std::chrono::seconds(2), [&]() { return recovered; })) {
            std::cerr << "latest worker terminated after processor failure" << std::endl;
            return false;
        }
    }
    worker.Stop();
    return true;
}

bool CheckWorkerPauseAndResume() {
    std::mutex mu;
    std::condition_variable cv;
    std::vector<int> processed;
    editor::sdk::private_impl::LatestItemWorker<int> worker(
        [&](int value) {
            {
                std::lock_guard<std::mutex> lock(mu);
                processed.push_back(value);
            }
            cv.notify_all();
        });
    if (!worker.PauseAndWaitForIdle(100) || worker.Submit(1) || !worker.Resume() ||
        !worker.Submit(2)) {
        std::cerr << "worker pause/resume state mismatch" << std::endl;
        return false;
    }
    {
        std::unique_lock<std::mutex> lock(mu);
        if (!cv.wait_for(lock, std::chrono::seconds(2), [&]() { return processed.size() == 1; }) ||
            processed[0] != 2) {
            std::cerr << "paused worker processed a rejected item" << std::endl;
            return false;
        }
    }
    if (!worker.PauseAndWaitForIdle(100)) {
        std::cerr << "worker did not pause before stop" << std::endl;
        return false;
    }
    worker.Stop();
    return !worker.Resume() && !worker.Submit(3);
}

bool CheckWorkerPauseWaitsForInflight() {
    std::mutex mu;
    std::condition_variable cv;
    bool processing = false;
    bool release = false;
    int processed = 0;
    editor::sdk::private_impl::LatestItemWorker<int> worker(
        [&](int) {
            std::unique_lock<std::mutex> lock(mu);
            processing = true;
            ++processed;
            cv.notify_all();
            cv.wait(lock, [&]() { return release; });
        });
    worker.Submit(1);
    {
        std::unique_lock<std::mutex> lock(mu);
        if (!cv.wait_for(lock, std::chrono::seconds(2), [&]() { return processing; })) {
            std::cerr << "pause-wait worker did not start" << std::endl;
            release = true;
            lock.unlock();
            cv.notify_all();
            return false;
        }
    }
    worker.Submit(2);
    std::thread releaser([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        {
            std::lock_guard<std::mutex> lock(mu);
            release = true;
        }
        cv.notify_all();
    });
    const bool paused = worker.PauseAndWaitForIdle(1000);
    releaser.join();
    if (!paused || worker.Submit(3) || !worker.Resume() || !worker.Submit(4)) {
        std::cerr << "worker did not pause after in-flight processing completed" << std::endl;
        return false;
    }
    {
        std::unique_lock<std::mutex> lock(mu);
        if (!cv.wait_for(lock, std::chrono::seconds(2), [&]() { return processed == 2; })) {
            std::cerr << "worker did not discard pending work or resume correctly" << std::endl;
            return false;
        }
    }
    worker.Stop();
    return true;
}

bool CheckWorkerPauseTimeoutRecovers() {
    std::mutex mu;
    std::condition_variable cv;
    bool processing = false;
    bool release = false;
    int processed = 0;
    editor::sdk::private_impl::LatestItemWorker<int> worker(
        [&](int) {
            std::unique_lock<std::mutex> lock(mu);
            processing = true;
            ++processed;
            cv.notify_all();
            cv.wait(lock, [&]() { return release; });
        });
    worker.Submit(1);
    {
        std::unique_lock<std::mutex> lock(mu);
        if (!cv.wait_for(lock, std::chrono::seconds(2), [&]() { return processing; })) {
            std::cerr << "pause-timeout worker did not start" << std::endl;
            release = true;
            lock.unlock();
            cv.notify_all();
            return false;
        }
    }
    const bool recovered = !worker.PauseAndWaitForIdle(20) && !worker.Submit(2) &&
        worker.Resume() && worker.Submit(3);
    {
        std::lock_guard<std::mutex> lock(mu);
        release = true;
    }
    cv.notify_all();
    if (!recovered) {
        std::cerr << "worker pause timeout state mismatch" << std::endl;
        worker.Stop();
        return false;
    }
    {
        std::unique_lock<std::mutex> lock(mu);
        if (!cv.wait_for(lock, std::chrono::seconds(2), [&]() { return processed == 2; })) {
            std::cerr << "worker did not recover after pause timeout" << std::endl;
            return false;
        }
    }
    worker.Stop();
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
                   CheckCaptureTimeoutDiagnostics() &&
                   CheckRealtimeRectCacheSelection() && CheckFrameCaptureRectSelection() &&
                   CheckLatestItemReplacement() &&
                   CheckWorkerStopWaits() && CheckWorkerSurvivesProcessorFailure() &&
                   CheckWorkerPauseAndResume() && CheckWorkerPauseWaitsForInflight() &&
                   CheckWorkerPauseTimeoutRecovers() &&
                   CheckFullFrameDetection()
               ? 0
               : 1;
}
