// Copyright (c) 2026 CZUR Tech. All rights reserved.

#pragma once

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "device_manager.h"
#include "i_sdk_device_provider.h"

namespace editor {
namespace sdk {
namespace private_impl {

inline std::string BuildCaptureTimeoutMessage(bool is_current_capture,
                                              bool device_is_streaming,
                                              const std::string& device_error) {
    const std::string base_message = "capture timeout";
    if (!is_current_capture || device_is_streaming || device_error.empty()) {
        return base_message;
    }
    return base_message + ": " + device_error;
}

inline bool IsFullFrameRect(const SdkRect4P& rect, int width, int height, float tolerance = 3.0f) {
    if (width <= 1 || height <= 1) {
        return false;
    }
    const float right = static_cast<float>(width - 1);
    const float bottom = static_cast<float>(height - 1);
    const auto near_value = [tolerance](float value, float expected) {
        return std::fabs(value - expected) <= tolerance;
    };
    return near_value(rect.left_top.x, 0.0f) &&
           near_value(rect.left_top.y, 0.0f) &&
           near_value(rect.right_top.x, right) &&
           near_value(rect.right_top.y, 0.0f) &&
           near_value(rect.right_down.x, right) &&
           near_value(rect.right_down.y, bottom) &&
           near_value(rect.left_down.x, 0.0f) &&
           near_value(rect.left_down.y, bottom);
}

enum class RealtimeRectCacheMutation {
    Ignore,
    Clear,
    Replace,
};

inline bool IsExpectedRealtimeRectSession(
    const std::shared_ptr<std::atomic<uint64_t> >& active_stream,
    bool realtime_detection_enabled,
    const std::shared_ptr<std::atomic<uint64_t> >& expected_stream) {
    return active_stream && expected_stream && active_stream == expected_stream &&
           realtime_detection_enabled;
}

inline bool CanPublishRealtimeRectCache(
    const std::shared_ptr<std::atomic<uint64_t> >& active_stream,
    bool realtime_detection_enabled,
    const std::shared_ptr<std::atomic<uint64_t> >& expected_stream,
    int cache_width,
    int cache_height,
    int frame_width,
    int frame_height) {
    return IsExpectedRealtimeRectSession(
               active_stream, realtime_detection_enabled, expected_stream) &&
           cache_width > 0 && cache_height > 0 && frame_width > 0 && frame_height > 0 &&
           cache_width == frame_width && cache_height == frame_height;
}

inline bool NeedsCurrentFrameRectDetection(bool has_raw_capture,
                                           bool hardgrab_capture_pending) {
    return has_raw_capture || hardgrab_capture_pending;
}

template <typename Cache>
Cache SelectFrameCaptureRectCache(const Cache* current_frame, const Cache& latest_preview) {
    return current_frame != NULL ? *current_frame : latest_preview;
}

inline RealtimeRectCacheMutation SelectRealtimeRectCacheMutation(
    const std::shared_ptr<std::atomic<uint64_t> >& active_stream,
    bool realtime_detection_enabled,
    const std::shared_ptr<std::atomic<uint64_t> >& expected_stream,
    bool has_detected_rects) {
    if (!IsExpectedRealtimeRectSession(
            active_stream, realtime_detection_enabled, expected_stream)) {
        return RealtimeRectCacheMutation::Ignore;
    }
    return has_detected_rects ? RealtimeRectCacheMutation::Replace
                              : RealtimeRectCacheMutation::Clear;
}

// A single-consumer bounded worker used by realtime preview processing. Submit
// replaces the pending item so a slow algorithm can never build an unbounded
// frame backlog. Stop waits for an in-flight callback and discards pending work.
template <typename T>
class LatestItemWorker {
public:
    typedef std::function<void(T)> Processor;

    explicit LatestItemWorker(const Processor& processor)
        : processor_(processor), running_(true), paused_(false), processing_(false),
          worker_(&LatestItemWorker::Run, this) {}

    ~LatestItemWorker() {
        Stop();
    }

    bool Submit(T item) {
        std::lock_guard<std::mutex> lock(mu_);
        if (!running_ || paused_) {
            return false;
        }
        pending_.reset(new T(std::move(item)));
        cv_.notify_one();
        return true;
    }

    bool PauseAndWaitForIdle(int timeout_ms) {
        std::unique_lock<std::mutex> lock(mu_);
        if (!running_ || paused_) {
            return false;
        }
        paused_ = true;
        pending_.reset();
        if (!processing_) {
            return true;
        }
        if (timeout_ms <= 0) {
            return false;
        }
        const bool idle = idle_cv_.wait_for(
            lock,
            std::chrono::milliseconds(timeout_ms),
            [this]() { return !running_ || !processing_; });
        return idle && running_ && !processing_;
    }

    bool Resume() {
        std::lock_guard<std::mutex> lock(mu_);
        if (!running_) {
            return false;
        }
        paused_ = false;
        cv_.notify_one();
        return true;
    }

    void Stop() {
        std::thread worker;
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (!running_ && !worker_.joinable()) {
                return;
            }
            running_ = false;
            paused_ = false;
            pending_.reset();
            cv_.notify_all();
            idle_cv_.notify_all();
            worker.swap(worker_);
        }
        if (worker.joinable()) {
            worker.join();
        }
    }

private:
    LatestItemWorker(const LatestItemWorker&);
    LatestItemWorker& operator=(const LatestItemWorker&);

    void Run() {
        for (;;) {
            std::unique_ptr<T> item;
            {
                std::unique_lock<std::mutex> lock(mu_);
                cv_.wait(lock, [this]() {
                    return !running_ || (!paused_ && pending_.get() != NULL);
                });
                if (!running_) {
                    return;
                }
                item.swap(pending_);
                processing_ = true;
            }
            if (item.get() != NULL && processor_) {
                try {
                    processor_(std::move(*item));
                } catch (...) {
                    // Keep the worker alive. Provider processors log and clear
                    // session-scoped output before allowing an error to escape.
                }
            }
            {
                std::lock_guard<std::mutex> lock(mu_);
                processing_ = false;
                idle_cv_.notify_all();
            }
        }
    }

    Processor processor_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::condition_variable idle_cv_;
    bool running_;
    bool paused_;
    bool processing_;
    std::unique_ptr<T> pending_;
    std::thread worker_;
};

class PrivateDeviceProvider : public ISdkDeviceProvider {
public:
    ~PrivateDeviceProvider() override;
    // macOS 需要在 SDK Open 启动阶段完成共享 helper 的版本预检，不能等到
    // 第一次 device.list/device.open 请求才初始化设备管理器。
    bool InitializeForRuntimeStartup() const;
    std::string ProviderName() const override;
    std::vector<SdkDeviceDescriptor> ListDevices() const override;
    SdkDeviceOpenResult GetDevice(const SdkDeviceOpenRequest& request) override;
    SdkDeviceOpenResult OpenDevice(const SdkDeviceOpenRequest& request) override;
    SdkDeviceCloseResult CloseDevice(const SdkDeviceCloseRequest& request) override;
    void CaptureStill(const SdkCaptureRequest& request, SdkCaptureCallback callback) override;
    SdkVideoStartResult StartVideo(const SdkVideoStartRequest& request, SdkVideoFrameCallback callback) override;
    SdkVideoStopResult StopVideo(const SdkVideoStopRequest& request) override;
    SdkVideoFormatResult SetVideoFormat(const SdkVideoFormatRequest& request) override;
    SdkVideoProfileResult SetVideoProfile(const SdkVideoProfileRequest& request) override;
    bool IsDeviceRecentlyRemoved(const std::string& device_id) const override;
    void SetDeviceActionEventSink(SdkDeviceActionEventCallback sink) override;
    void SetDeviceEventSink(SdkDeviceEventCallback sink) override;
    SdkTurnDetectResult SetTurnDetect(const SdkTurnDetectRequest& request) override;
    // storage cleanup 使用同一把锁串行化 pending 注册、实际写盘与目录删除。
    std::unique_lock<std::mutex> TryLockCaptureStorageLifecycle() const;
    std::size_t ActiveCaptureCount() const;

private:
    struct PendingCapture;
    struct DetectedRectCache {
        std::vector<SdkRect4P> rects;
        int source_width = 0;
        int source_height = 0;
        int scan_device_type = 0;
    };
    struct RealtimeRectFrame {
        std::vector<uint8_t> native_mjpeg;
        int width = 0;
        int height = 0;
        editor::devices::device_frame_format format = editor::devices::DEVICE_FRAME_FORMAT_UNKNOWN;
    };
    typedef LatestItemWorker<RealtimeRectFrame> RealtimeRectWorker;
    struct TurnDetectState {
        bool enabled = false;
        bool initialized = false;
        bool auto_capture = false;
        int scan_device_type = -1;
        int cooldown_ms = 1000;
    };
    // 非 still-image 设备的硬拍由下一帧预览图完成，只需要跨回调保存触发时间。
    struct PendingHardGrabCapture {
        int64_t timestamp_ms = 0;
    };

    bool EnsureDeviceManagerReady() const;
    std::vector<SdkDeviceDescriptor> BuildDeviceDescriptors(
        const std::vector<std::shared_ptr<editor::devices::Device> >& devices) const;
    SdkDeviceDescriptor BuildDeviceDescriptor(const std::shared_ptr<editor::devices::Device>& device) const;
    std::shared_ptr<editor::devices::Device> LookupDevice(const std::string& device_id) const;
    std::string ResolveDeviceManagerLogPath() const;
    std::string ResolveDeviceConfigPath() const;
    SdkDeviceOpenResult BuildDeviceResult(const std::string& device_id, bool include_resolutions);
    std::vector<editor::devices::Resolution> LoadSupportedResolutions(
        const std::shared_ptr<editor::devices::Device>& device,
        bool* opened_for_query) const;
    editor::devices::Resolution PickResolution(const std::shared_ptr<editor::devices::Device>& device,
                                                int width,
                                                int height,
                                                int fps);
    // expected_pending 为非空时，只能完成同一实例，避免旧超时任务终止新采集。
    bool CompletePendingCapture(const std::string& device_id,
                                const std::shared_ptr<PendingCapture>& expected_pending,
                                const SdkCaptureResult& result,
                                const char* completion_source);
    bool TryCompleteFrameCapture(const std::string& device_id,
                                 editor::devices::GenericFrame* frame,
                                 const DetectedRectCache* current_frame_rect_cache);
    bool CompleteStillImageCapture(const std::string& device_id, const editor::devices::StillImagePair& image_pair);
    // 使用下一帧预览图完成硬拍采集，覆盖非 still-image 设备。
    bool TryCompleteHardGrabFrameCapture(const std::string& device_id,
                                         editor::devices::GenericFrame* frame,
                                         const PendingHardGrabCapture* pending_hardgrab,
                                         const DetectedRectCache* current_frame_rect_cache);
    // 硬拍原始 JPEG 由 Provider 保存到 runtime capture/hardgrab 临时目录，
    // sdk_open task 只接管路径并转存到任务 raw 目录。
    SdkCaptureResult BuildHardGrabJpegCapture(const std::string& device_id,
                                              const std::vector<uint8_t>& jpeg_payload,
                                              const std::vector<uint8_t>* laser_payload,
                                              const DetectedRectCache* current_frame_rect_cache = NULL);
    void HandleStillImageReceived(const std::string& device_id, const editor::devices::StillImagePair& image_pair);
    std::vector<SdkRect4P> DetectSinglePageRects(const std::string& device_id,
                                                 editor::devices::GenericFrame* frame,
                                                 int scan_device_type,
                                                 bool multi_target_paging,
                                                 const std::shared_ptr<std::atomic<uint64_t> >& expected_stream);
    void UpdateDetectedRectCache(
        const std::string& device_id,
        const std::shared_ptr<std::atomic<uint64_t> >& expected_stream,
        const std::vector<SdkRect4P>& rects,
        int source_width,
        int source_height,
        int scan_device_type);
    DetectedRectCache GetDetectedRectCache(const std::string& device_id) const;
    DetectedRectCache GetDetectedRectCache(
        const std::string& device_id,
        const std::shared_ptr<std::atomic<uint64_t> >& expected_stream,
        int frame_width,
        int frame_height) const;
    bool RealtimeDetectRectsEnabled(const std::string& device_id) const;
    bool MultiTargetPagingEnabled(const std::string& device_id) const;
    bool TakePendingHardGrabFrameCapture(const std::string& device_id,
                                         PendingHardGrabCapture* pending_hardgrab);
    void DetectPageTurn(const std::string& device_id, editor::devices::GenericFrame* frame, int scan_device_type);
    void DisarmStreamState(const std::string& device_id, bool erase_turn_detect);
    void StopAllRealtimeRectWorkers();
    void HandleStreamError(const std::shared_ptr<editor::devices::Device>& device,
                           const std::string& message);
    bool ShouldSuppressStillImageActionEvent(const std::string& device_id, int64_t now_ms);
    void SuppressStillImageActionEvent(const std::string& device_id, int64_t until_ms);
    void PublishDeviceActionEvent(const SdkDeviceActionEvent& event) const;
    void HandleHotplugEvent(const std::shared_ptr<editor::devices::Device>& device, bool is_added);
    void PublishDeviceEvent(const SdkDeviceEvent& event) const;

    mutable std::mutex init_mu_;
    mutable bool initialized_ = false;
    mutable std::mutex state_mu_;
    mutable std::mutex capture_storage_lifecycle_mu_;
    std::map<std::string, editor::devices::Resolution> selected_resolutions_;
    std::map<std::string, std::shared_ptr<std::atomic<uint64_t> > > active_stream_sequences_;
    std::map<std::string, std::shared_ptr<PendingCapture> > pending_captures_;
    std::atomic<uint64_t> next_capture_token_{1};
    std::map<std::string, bool> realtime_detect_rects_;
    std::map<std::string, bool> multi_target_paging_;
    std::map<std::string, DetectedRectCache> detected_rect_caches_;
    std::map<std::string, std::shared_ptr<RealtimeRectWorker> > realtime_rect_workers_;
    std::map<std::string, TurnDetectState> turn_detect_states_;
    std::map<std::string, int64_t> still_image_action_suppressed_until_ms_;
    std::map<std::string, PendingHardGrabCapture> pending_hardgrab_captures_;
    std::set<std::string> recently_removed_device_ids_;
    SdkDeviceActionEventCallback device_action_event_sink_;
    SdkDeviceEventCallback device_event_sink_;
};

} // namespace private_impl
} // namespace sdk
} // namespace editor
