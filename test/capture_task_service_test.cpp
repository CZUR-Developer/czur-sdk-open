// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "capture_task_service.h"
#include "sdk_runtime_paths.h"

namespace editor {
namespace sdk {
namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Predicate>
bool WaitUntil(Predicate predicate, int timeout_ms) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

class TestDeviceProvider : public ISdkDeviceProvider {
public:
    std::string ProviderName() const override { return "capture-task-test-device"; }

    std::vector<SdkDeviceDescriptor> ListDevices() const override {
        std::vector<SdkDeviceDescriptor> devices;
        SdkDeviceDescriptor device;
        device.device_id = "test-device";
        device.vid = 1;
        device.pid = 1;
        device.status = "online";
        devices.push_back(device);
        return devices;
    }

    SdkDeviceOpenResult GetDevice(const SdkDeviceOpenRequest& request) override {
        SdkDeviceOpenResult result;
        if (request.device_id != "test-device") {
            result.code = ToCode(SdkStatusCode::DeviceNotFound);
            result.message = "device not found";
            return result;
        }
        result.opened = true;
        result.device = ListDevices()[0];
        return result;
    }

    SdkDeviceOpenResult OpenDevice(const SdkDeviceOpenRequest& request) override {
        return GetDevice(request);
    }

    SdkDeviceCloseResult CloseDevice(const SdkDeviceCloseRequest&) override {
        SdkDeviceCloseResult result;
        result.closed = true;
        return result;
    }

    void CaptureStill(const SdkCaptureRequest& request, SdkCaptureCallback callback) override {
        bool fail_capture = false;
        bool block_capture = false;
        int capture_index = 0;
        {
            std::lock_guard<std::mutex> lock(mu_);
            ++capture_still_count_;
            capture_index = capture_still_count_;
            fail_capture = fail_next_capture_;
            fail_next_capture_ = false;
            block_capture = block_next_capture_;
            block_next_capture_ = false;
            capture_started_ = true;
        }
        capture_cv_.notify_all();

        if (block_capture) {
            std::unique_lock<std::mutex> lock(mu_);
            capture_cv_.wait(lock, [this]() { return release_capture_; });
            release_capture_ = false;
        }

        SdkCaptureResult result;
        if (fail_capture) {
            result.code = ToCode(SdkStatusCode::CaptureFailed);
            result.message = "test capture failure";
        } else {
            Require(EnsureDirectoryRecursive(request.output_dir), "capture raw directory should exist");
            const std::string path = JoinPath(request.output_dir,
                                              "original-" + std::to_string(static_cast<long long>(capture_index)) + ".jpg");
            std::ofstream output(path.c_str(), std::ios::binary | std::ios::trunc);
            output << "test jpeg " << capture_index;
            Require(output.good(), "test original should be writable");
            result.captured = true;
            result.original_path = path;
            result.output_path = path;
            result.content_type = "image/jpeg";
            result.width = 16;
            result.height = 16;
            result.dpi = 300;
        }
        if (callback) {
            callback(result);
        }
    }

    SdkVideoStartResult StartVideo(const SdkVideoStartRequest&, SdkVideoFrameCallback) override {
        SdkVideoStartResult result;
        result.accepted = true;
        return result;
    }

    SdkVideoStopResult StopVideo(const SdkVideoStopRequest&) override {
        SdkVideoStopResult result;
        result.stopped = true;
        return result;
    }

    SdkVideoFormatResult SetVideoFormat(const SdkVideoFormatRequest&) override {
        SdkVideoFormatResult result;
        result.applied = true;
        return result;
    }

    SdkVideoProfileResult SetVideoProfile(const SdkVideoProfileRequest&) override {
        SdkVideoProfileResult result;
        result.applied = true;
        return result;
    }

    void FailNextCapture() {
        std::lock_guard<std::mutex> lock(mu_);
        fail_next_capture_ = true;
    }

    void BlockNextCapture() {
        std::lock_guard<std::mutex> lock(mu_);
        block_next_capture_ = true;
        capture_started_ = false;
        release_capture_ = false;
    }

    bool WaitForCaptureStart(int timeout_ms) {
        std::unique_lock<std::mutex> lock(mu_);
        return capture_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this]() { return capture_started_; });
    }

    void ReleaseCapture() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            release_capture_ = true;
        }
        capture_cv_.notify_all();
    }

    int CaptureStillCount() const {
        std::lock_guard<std::mutex> lock(mu_);
        return capture_still_count_;
    }

private:
    mutable std::mutex mu_;
    std::condition_variable capture_cv_;
    int capture_still_count_ = 0;
    bool fail_next_capture_ = false;
    bool block_next_capture_ = false;
    bool capture_started_ = false;
    bool release_capture_ = false;
};

class TestGraphicProvider : public ISdkGraphicProvider {
public:
    std::string ProviderName() const override { return "capture-task-test-graphic"; }

    SdkImageProcessResult Process(const SdkImageProcessRequest&) override {
        SdkImageProcessResult result;
        result.code = ToCode(SdkStatusCode::UnsupportedMethod);
        result.message = "not used by capture task test";
        return result;
    }

    SdkPageProcessResult ProcessPage(const SdkPageProcessRequest& request) override {
        bool block = false;
        {
            std::lock_guard<std::mutex> lock(mu_);
            processed_inputs_.push_back(request.input_path);
            page_process_entered_ = true;
            block = block_first_page_process_ && processed_inputs_.size() == 1;
        }
        page_process_cv_.notify_all();
        if (block) {
            std::unique_lock<std::mutex> lock(mu_);
            page_process_cv_.wait(lock, [this]() { return release_page_process_; });
        }
        SdkPageProcessResult result;
        result.unsupported = true;
        result.message = "test fallback";
        return result;
    }

    SdkColorModeResult ApplyColorMode(const SdkColorModeRequest&) override {
        SdkColorModeResult result;
        result.unsupported = true;
        result.message = "test fallback";
        return result;
    }

    SdkFormatConvertResult ConvertImageFormat(const SdkFormatConvertRequest& request) override {
        SdkFormatConvertResult result;
        bool fail = false;
        {
            std::lock_guard<std::mutex> lock(mu_);
            fail = fail_next_format_convert_;
            fail_next_format_convert_ = false;
        }
        if (fail) {
            result.code = ToCode(SdkStatusCode::ProviderCallFailed);
            result.message = "test processing failure";
            return result;
        }
        std::ifstream input(request.input_path.c_str(), std::ios::binary);
        std::ofstream output(request.output_path.c_str(), std::ios::binary | std::ios::trunc);
        output << input.rdbuf();
        result.converted = output.good();
        result.output_path = request.output_path;
        return result;
    }

    SdkThumbnailResult GenerateThumbnail(const SdkThumbnailRequest&) override {
        SdkThumbnailResult result;
        result.code = ToCode(SdkStatusCode::UnsupportedMethod);
        result.message = "test fallback";
        return result;
    }

    void BlockFirstPageProcess() {
        std::lock_guard<std::mutex> lock(mu_);
        block_first_page_process_ = true;
        page_process_entered_ = false;
        release_page_process_ = false;
    }

    bool WaitForFirstPageProcess(int timeout_ms) {
        std::unique_lock<std::mutex> lock(mu_);
        return page_process_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this]() { return page_process_entered_; });
    }

    void ReleasePageProcess() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            release_page_process_ = true;
        }
        page_process_cv_.notify_all();
    }

    std::vector<std::string> ProcessedInputs() const {
        std::lock_guard<std::mutex> lock(mu_);
        return processed_inputs_;
    }

    void FailNextFormatConvert() {
        std::lock_guard<std::mutex> lock(mu_);
        fail_next_format_convert_ = true;
    }

private:
    mutable std::mutex mu_;
    std::condition_variable page_process_cv_;
    std::vector<std::string> processed_inputs_;
    bool block_first_page_process_ = false;
    bool page_process_entered_ = false;
    bool release_page_process_ = false;
    bool fail_next_format_convert_ = false;
};

class SessionEventRecorder {
public:
    void Record(const std::string&, const Json& event) {
        if (event.value("event", std::string()) != "capture.session.updated") {
            return;
        }
        const Json payload = event.value("payload", Json::object());
        std::lock_guard<std::mutex> lock(mu_);
        reasons_.push_back(payload.value("reason", std::string()));
    }

    bool HasReason(const std::string& reason) const {
        std::lock_guard<std::mutex> lock(mu_);
        for (std::vector<std::string>::const_iterator it = reasons_.begin(); it != reasons_.end(); ++it) {
            if (*it == reason) {
                return true;
            }
        }
        return false;
    }

private:
    mutable std::mutex mu_;
    std::vector<std::string> reasons_;
};

CaptureTaskStartRequest MakeRequest(const std::string& connection_id) {
    CaptureTaskStartRequest request;
    request.connection_id = connection_id;
    request.session_token = "test-session";
    request.device_id = "test-device";
    request.timeout_ms = 2000;
    request.profile.device_id = request.device_id;
    request.profile.thumbnail_original = false;
    request.profile.thumbnail_page_processed = false;
    request.profile.thumbnail_color_processed = false;
    request.profile.thumbnail_final = false;
    return request;
}

CaptureTaskStartResult ReserveAndStart(CaptureTaskService* service, const CaptureTaskStartRequest& request) {
    CaptureTaskStartResult result = service->ReserveTask(request);
    Require(result.accepted, "capture task should be reserved");
    result = service->StartReservedTask(result.task.task_id);
    Require(result.accepted, "capture task should start");
    return result;
}

bool WaitForTerminal(CaptureTaskService* service,
                     const std::string& connection_id,
                     const std::string& task_id) {
    return WaitUntil([service, connection_id, task_id]() {
        const CaptureTaskSnapshot task = service->GetTask(connection_id, task_id);
        return task.status == "succeeded" || task.status == "failed";
    }, 5000);
}

void TestDeviceBusyTakesPrecedenceOverCooldown() {
    std::shared_ptr<TestDeviceProvider> device(new TestDeviceProvider);
    std::shared_ptr<TestGraphicProvider> graphic(new TestGraphicProvider);
    ProviderBundle providers;
    providers.device_provider = device;
    providers.graphic_provider = graphic;
    CaptureTaskService service(providers);

    device->BlockNextCapture();
    const CaptureTaskStartResult first = ReserveAndStart(&service, MakeRequest("busy-connection"));
    Require(device->WaitForCaptureStart(1000), "physical capture should start");
    std::this_thread::sleep_for(std::chrono::milliseconds(1550));

    const CaptureTaskStartResult rejected = service.ReserveTask(MakeRequest("busy-connection"));
    Require(rejected.code == ToCode(SdkStatusCode::DeviceBusy), "active physical capture should return DeviceBusy");
    device->ReleaseCapture();
    Require(WaitForTerminal(&service, "busy-connection", first.task.task_id), "blocked capture should finish");
}

void TestFifoRateLimitHardGrabAndSummary() {
    std::shared_ptr<TestDeviceProvider> device(new TestDeviceProvider);
    std::shared_ptr<TestGraphicProvider> graphic(new TestGraphicProvider);
    ProviderBundle providers;
    providers.device_provider = device;
    providers.graphic_provider = graphic;
    CaptureTaskService service(providers);
    SessionEventRecorder events;
    service.SetEventSink([&events](const std::string& connection_id, const Json& event) {
        events.Record(connection_id, event);
    });

    graphic->BlockFirstPageProcess();
    const CaptureTaskStartResult first = ReserveAndStart(&service, MakeRequest("connection-a"));
    Require(graphic->WaitForFirstPageProcess(2000), "first task should reach the processing stage");

    std::this_thread::sleep_for(std::chrono::milliseconds(1550));
    const CaptureTaskStartResult second = ReserveAndStart(&service, MakeRequest("connection-a"));
    Require(WaitUntil([&service]() {
        return service.GetSessionSummary("connection-a").captured_count == 2;
    }, 2000), "second capture should complete acquisition while first task processes");

    const CaptureTaskStartResult rate_limited = service.ReserveTask(MakeRequest("connection-a"));
    Require(rate_limited.code == ToCode(SdkStatusCode::RateLimited), "second request inside 1500ms should be rate limited");
    Require(rate_limited.retry_after_ms > 0, "rate limit should report retry_after_ms");
    CaptureTaskStartRequest rate_limited_hardgrab = MakeRequest("connection-a");
    rate_limited_hardgrab.raw_capture.captured = true;
    rate_limited_hardgrab.raw_capture.raw_payload.assign(8, static_cast<uint8_t>('R'));
    const CaptureTaskStartResult hardgrab_rate_limited = service.ReserveTask(rate_limited_hardgrab);
    Require(hardgrab_rate_limited.code == ToCode(SdkStatusCode::RateLimited),
            "hardgrab inside 1500ms should use the same rate limit");
    Require(hardgrab_rate_limited.retry_after_ms > 0, "hardgrab rate limit should report retry_after_ms");

    graphic->ReleasePageProcess();
    Require(WaitForTerminal(&service, "connection-a", first.task.task_id), "first task should finish");
    Require(WaitForTerminal(&service, "connection-a", second.task.task_id), "second task should finish");
    const std::vector<std::string> processed_inputs = graphic->ProcessedInputs();
    Require(processed_inputs.size() >= 2, "two tasks should enter processing");
    Require(processed_inputs[0].find(first.task.task_id) != std::string::npos, "first task should process first");
    Require(processed_inputs[1].find(second.task.task_id) != std::string::npos, "second task should process second");

    std::this_thread::sleep_for(std::chrono::milliseconds(1550));
    CaptureTaskStartRequest hardgrab = MakeRequest("connection-a");
    hardgrab.raw_capture.captured = true;
    hardgrab.raw_capture.content_type = "image/jpeg";
    hardgrab.raw_capture.raw_payload.assign(8, static_cast<uint8_t>('J'));
    const CaptureTaskStartResult hardgrab_task = ReserveAndStart(&service, hardgrab);
    Require(WaitForTerminal(&service, "connection-a", hardgrab_task.task.task_id), "hardgrab task should finish");
    Require(device->CaptureStillCount() == 2, "hardgrab must not call CaptureStill a second time");
    const CaptureTaskSnapshot hardgrab_snapshot = service.GetTask("connection-a", hardgrab_task.task.task_id);
    Require(hardgrab_snapshot.capture_source == "hardgrab", "hardgrab task source should be visible");

    const CaptureSessionSummary summary = service.GetSessionSummary("connection-a");
    Require(summary.captured_count == 3 && summary.processed_count == 3 && summary.failed_count == 0 && summary.pending_count == 0,
            "successful capture summary should count acquired and processed tasks");
    Require(events.HasReason("raw_captured") && events.HasReason("processing_completed"),
            "session updates should cover raw acquisition and processing completion");
    const CaptureSessionSummary other_summary = service.GetSessionSummary("connection-b");
    Require(other_summary.captured_count == 0 && other_summary.processed_count == 0,
            "session summaries should be isolated by connection");
    Require(service.GetTask("connection-b", first.task.task_id).code == ToCode(SdkStatusCode::CapabilityNotAllowed),
            "capture.get task access should remain connection isolated");
}

void TestFailureSessionUpdates() {
    std::shared_ptr<TestDeviceProvider> device(new TestDeviceProvider);
    std::shared_ptr<TestGraphicProvider> graphic(new TestGraphicProvider);
    ProviderBundle providers;
    providers.device_provider = device;
    providers.graphic_provider = graphic;
    CaptureTaskService service(providers);
    SessionEventRecorder events;
    service.SetEventSink([&events](const std::string& connection_id, const Json& event) {
        events.Record(connection_id, event);
    });

    device->FailNextCapture();
    const CaptureTaskStartResult capture_failure = ReserveAndStart(&service, MakeRequest("failure-connection"));
    Require(WaitForTerminal(&service, "failure-connection", capture_failure.task.task_id), "capture failure should become terminal");

    std::this_thread::sleep_for(std::chrono::milliseconds(1550));
    graphic->FailNextFormatConvert();
    CaptureTaskStartRequest processing_failure = MakeRequest("failure-connection");
    processing_failure.profile.output_format = "png";
    const CaptureTaskStartResult process_task = ReserveAndStart(&service, processing_failure);
    Require(WaitForTerminal(&service, "failure-connection", process_task.task.task_id), "processing failure should become terminal");
    Require(service.GetTask("failure-connection", process_task.task.task_id).processing_status == "failed",
            "processing failure should be distinguishable from acquisition failure");

    const CaptureSessionSummary summary = service.GetSessionSummary("failure-connection");
    Require(summary.captured_count == 1 && summary.processed_count == 0 && summary.failed_count == 2 && summary.pending_count == 0,
            "failure summary should distinguish capture and processing failures without pending work");
    Require(WaitUntil([&events]() {
                return events.HasReason("capture_failed") && events.HasReason("processing_failed");
            }, 1000),
            "failure transitions should publish session updates");
}

} // namespace
} // namespace sdk
} // namespace editor

int main() {
    try {
        editor::sdk::TestDeviceBusyTakesPrecedenceOverCooldown();
        editor::sdk::TestFifoRateLimitHardGrabAndSummary();
        editor::sdk::TestFailureSessionUpdates();
        std::cout << "sdk_open_capture_task_service_test passed" << std::endl;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "sdk_open_capture_task_service_test failed: " << error.what() << std::endl;
        return 1;
    }
}
