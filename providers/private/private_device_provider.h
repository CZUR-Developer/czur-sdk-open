// Copyright (c) 2026 CZUR Tech. All rights reserved.

#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "device_manager.h"
#include "i_sdk_device_provider.h"

namespace editor {
namespace sdk {
namespace private_impl {

class PrivateDeviceProvider : public ISdkDeviceProvider {
public:
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
    bool TryCompleteFrameCapture(const std::string& device_id, editor::devices::GenericFrame* frame);
    bool CompleteStillImageCapture(const std::string& device_id, const editor::devices::StillImagePair& image_pair);
    // 使用下一帧预览图完成硬拍采集，覆盖非 still-image 设备。
    bool TryCompleteHardGrabFrameCapture(const std::string& device_id, editor::devices::GenericFrame* frame);
    // 硬拍原始 JPEG 由 Provider 保存到 runtime capture/hardgrab 临时目录，
    // sdk_open task 只接管路径并转存到任务 raw 目录。
    SdkCaptureResult BuildHardGrabJpegCapture(const std::string& device_id,
                                              const std::vector<uint8_t>& jpeg_payload,
                                              const std::vector<uint8_t>* laser_payload);
    void HandleStillImageReceived(const std::string& device_id, const editor::devices::StillImagePair& image_pair);
    std::vector<SdkRect4P> DetectSinglePageRects(const std::string& device_id,
                                                 editor::devices::GenericFrame* frame,
                                                 int scan_device_type,
                                                 bool multi_target_paging);
    DetectedRectCache GetDetectedRectCache(const std::string& device_id) const;
    bool RealtimeDetectRectsEnabled(const std::string& device_id) const;
    bool MultiTargetPagingEnabled(const std::string& device_id) const;
    void ResetTurnDetectSession(const std::string& device_id);
    void DetectPageTurn(const std::string& device_id, editor::devices::GenericFrame* frame, int scan_device_type);
    void DisarmStreamState(const std::string& device_id, bool erase_turn_detect);
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
