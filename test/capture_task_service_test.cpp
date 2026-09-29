// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
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


class TestImageEnhanceProvider : public ISdkImageEnhanceProvider {
public:
    std::string ProviderName() const override { return "capture-task-test-enhance"; }

    SdkImageEnhanceCapabilityResult ListCapabilities() override {
        SdkImageEnhanceCapabilityResult result;
        result.provider = ProviderName();
        return result;
    }

    SdkImageEnhanceStepResult RunStep(const SdkImageEnhanceStepRequest& request) override {
        std::lock_guard<std::mutex> lock(mu_);
        ++run_step_count_;
        last_request_ = request;
        SdkImageEnhanceStepResult result;
        if (fail_next_step_) {
            fail_next_step_ = false;
            result.code = ToCode(SdkStatusCode::ProviderCallFailed);
            result.message = "test enhance failure";
            return result;
        }
        Require(EnsureDirectoryRecursive(request.output_dir), "enhance output dir should exist");
        const int output_count = next_output_count_ > 0 ? next_output_count_ : static_cast<int>(request.pages.size());
        next_output_count_ = 0;
        for (int i = 0; i < output_count; ++i) {
            const SdkImageEnhancePage& source = request.pages[static_cast<std::size_t>(i) % request.pages.size()];
            const std::string output_path = JoinPath(request.output_dir,
                                                     "enhanced-" + std::to_string(static_cast<long long>(i + 1)) + ".jpg");
            std::ifstream input(source.path.c_str(), std::ios::binary);
            std::ofstream output(output_path.c_str(), std::ios::binary | std::ios::trunc);
            output << input.rdbuf() << " enhanced";
            Require(output.good(), "enhance output should be writable");
            SdkImageEnhancePage page;
            page.source_index = source.source_index;
            page.output_index = i + 1;
            page.path = output_path;
            result.pages.push_back(page);
        }
        result.processed = true;
        return result;
    }

    int RunStepCount() const {
        std::lock_guard<std::mutex> lock(mu_);
        return run_step_count_;
    }

    SdkImageEnhanceStepRequest LastRequest() const {
        std::lock_guard<std::mutex> lock(mu_);
        return last_request_;
    }

    void FailNextStep() {
        std::lock_guard<std::mutex> lock(mu_);
        fail_next_step_ = true;
    }

    void SetNextOutputCount(int count) {
        std::lock_guard<std::mutex> lock(mu_);
        next_output_count_ = count;
    }

private:
    mutable std::mutex mu_;
    int run_step_count_ = 0;
    int next_output_count_ = 0;
    bool fail_next_step_ = false;
    SdkImageEnhanceStepRequest last_request_;
};

class TestAuthProvider : public ISdkAuthProvider {
public:
    std::function<void(const QuotaConsumeRequest&)> on_consume;

    std::string ProviderName() const override { return "capture-task-test-auth"; }
    AuthValidateResult ValidateToken(const AuthValidateRequest&) override { return AuthValidateResult(); }
    AuthRefreshResult CreateSession(const AuthValidateRequest&) override { return AuthRefreshResult(); }
    AuthRefreshResult RefreshSession(const AuthRefreshRequest&) override { return AuthRefreshResult(); }
    SessionValidateResult ValidateSession(const SessionValidateRequest&) override { return SessionValidateResult(); }
    AuthContextResult GetAuthContext(const AuthLookupRequest&) override { return AuthContextResult(); }
    OfflineActivateResult ActivateOffline(const OfflineActivateRequest&) override { return OfflineActivateResult(); }

    QuotaConsumeResult ConsumeQuota(const QuotaConsumeRequest& request) override {
        {
            std::lock_guard<std::mutex> lock(mu_);
            ++consume_count_;
            last_request_ = request;
            requests_.push_back(request);
        }
        if (on_consume) {
            on_consume(request);
        }
        QuotaConsumeResult result;
        bool fail = false;
        {
            std::lock_guard<std::mutex> lock(mu_);
            fail = fail_next_consume_;
            fail_next_consume_ = false;
        }
        if (fail) {
            result.code = ToCode(SdkStatusCode::UsageLimitExceeded);
            result.message = "quota exhausted";
        } else {
            result.consumed = true;
        }
        return result;
    }

    int ConsumeCount() const {
        std::lock_guard<std::mutex> lock(mu_);
        return consume_count_;
    }

    QuotaConsumeRequest LastRequest() const {
        std::lock_guard<std::mutex> lock(mu_);
        return last_request_;
    }

    void FailNextConsume() {
        std::lock_guard<std::mutex> lock(mu_);
        fail_next_consume_ = true;
    }

private:
    mutable std::mutex mu_;
    int consume_count_ = 0;
    bool fail_next_consume_ = false;
    QuotaConsumeRequest last_request_;
    std::vector<QuotaConsumeRequest> requests_;
};

class CaptureReleaseGuard {
public:
    explicit CaptureReleaseGuard(const std::shared_ptr<TestDeviceProvider>& provider) : provider_(provider) {}
    ~CaptureReleaseGuard() {
        if (provider_) {
            provider_->ReleaseCapture();
        }
    }
private:
    std::shared_ptr<TestDeviceProvider> provider_;
};

class PageProcessReleaseGuard {
public:
    explicit PageProcessReleaseGuard(const std::shared_ptr<TestGraphicProvider>& provider) : provider_(provider) {}
    void Release() {
        if (provider_) {
            provider_->ReleasePageProcess();
            provider_.reset();
        }
    }
    ~PageProcessReleaseGuard() {
        Release();
    }
private:
    std::shared_ptr<TestGraphicProvider> provider_;
};

class BlockingCompletionEvent {
public:
    explicit BlockingCompletionEvent(const std::string& name = "capture.completed",
                                     const std::string& reason = "") : name_(name), reason_(reason) {}
    void Record(const std::string&, const Json& event) {
        if (event.value("event", std::string()) != name_) return;
        if (!reason_.empty() && event.value("payload", Json::object()).value("reason", std::string()) != reason_) return;
        {
            std::lock_guard<std::mutex> lock(mu_);
            entered_ = true;
        }
        cv_.notify_all();
        std::unique_lock<std::mutex> lock(mu_);
        cv_.wait(lock, [this]() { return released_; });
    }

    bool WaitForEntry(int timeout_ms) {
        std::unique_lock<std::mutex> lock(mu_);
        return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this]() { return entered_; });
    }

    void Release() {
        {
            std::lock_guard<std::mutex> lock(mu_);
            released_ = true;
        }
        cv_.notify_all();
    }

private:
    std::string name_;
    std::string reason_;
    std::mutex mu_;
    std::condition_variable cv_;
    bool entered_ = false;
    bool released_ = false;
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

struct SnapshotEventRecord {
    std::string connection_id;
    std::string event;
    CaptureTaskSnapshot task;
    bool has_stage = false;
    SdkCaptureStageResult stage;
};

struct SnapshotCopyControl {
    SnapshotCopyControl() : throw_on_copy(false), callback_count(0) {}

    bool throw_on_copy;
    int callback_count;
};

struct EventCount {
    EventCount() : count(0) {}

    mutable std::mutex mu;
    int count;
};

class ThrowOnSnapshotSinkCopy {
public:
    explicit ThrowOnSnapshotSinkCopy(const std::shared_ptr<SnapshotCopyControl>& control)
        : control_(control) {}

    ThrowOnSnapshotSinkCopy(const ThrowOnSnapshotSinkCopy& other)
        : control_(other.control_) {
        if (control_ && control_->throw_on_copy) {
            throw std::runtime_error("snapshot sink copy failed");
        }
    }

    void operator()(const std::string&,
                    const std::string&,
                    const CaptureTaskSnapshot&,
                    const SdkCaptureStageResult*) const {
        if (control_) {
            ++control_->callback_count;
        }
    }

private:
    std::shared_ptr<SnapshotCopyControl> control_;
};

class ReentrantEventSink {
public:
    ReentrantEventSink(CaptureTaskService* service,
                       bool snapshot,
                       const std::shared_ptr<bool>& armed,
                       const std::shared_ptr<bool>& fired)
        : service_(service), snapshot_(snapshot), armed_(armed), fired_(fired) {}

    void operator()(const std::string&, const Json&) const {}

    ~ReentrantEventSink() {
        if (!service_ || !armed_ || !*armed_ || !fired_ || *fired_) {
            return;
        }
        *fired_ = true;
        if (snapshot_) {
            service_->SetSnapshotEventSink(CaptureTaskService::SnapshotEventSink());
        } else {
            service_->SetEventSink(CaptureTaskService::EventSink());
        }
    }

private:
    CaptureTaskService* service_;
    bool snapshot_;
    std::shared_ptr<bool> armed_;
    std::shared_ptr<bool> fired_;
};

class ReentrantSnapshotSink {
public:
    ReentrantSnapshotSink(CaptureTaskService* service,
                          const std::shared_ptr<bool>& armed,
                          const std::shared_ptr<bool>& fired)
        : service_(service), armed_(armed), fired_(fired) {}

    void operator()(const std::string&,
                    const std::string&,
                    const CaptureTaskSnapshot&,
                    const SdkCaptureStageResult*) const {}

    ~ReentrantSnapshotSink() {
        if (!service_ || !armed_ || !*armed_ || !fired_ || *fired_) {
            return;
        }
        *fired_ = true;
        service_->SetSnapshotEventSink(CaptureTaskService::SnapshotEventSink());
    }

private:
    CaptureTaskService* service_;
    std::shared_ptr<bool> armed_;
    std::shared_ptr<bool> fired_;
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


SdkImageEnhanceStep MakeOnlineEnhanceStep(const std::string& type = "document_rectify_enhance") {
    SdkImageEnhanceStep step;
    step.id = "online-step";
    step.type = type;
    step.enabled = true;
    step.on_error = "fail";
    step.params_json = "{}";
    return step;
}

CaptureTaskStartRequest MakeOnlineEnhanceRequest(const std::string& connection_id) {
    CaptureTaskStartRequest request = MakeRequest(connection_id);
    request.pipeline.steps.push_back(MakeOnlineEnhanceStep());
    request.online_api_key = "online-api-key";
    request.session_token = "online-session-token";
    request.online_base_url = "https://enhance.example.test";
    request.authz_base_url = "https://authz.example.test";
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
    CaptureReleaseGuard release_capture(device);
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
    SessionEventRecorder events;
    CaptureTaskService service(providers);
    service.SetEventSink([&events](const std::string& connection_id, const Json& event) {
        events.Record(connection_id, event);
    });

    graphic->BlockFirstPageProcess();
    PageProcessReleaseGuard release_processing(graphic);
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
    const std::string hardgrab_path = JoinPath(GetSdkOpenCaptureDir(), "test-hardgrab-original.jpg");
    Require(EnsureDirectoryRecursive(GetSdkOpenCaptureDir()), "hardgrab test directory should exist");
    {
        std::ofstream output(hardgrab_path.c_str(), std::ios::binary | std::ios::trunc);
        output << "test hardgrab original";
        Require(output.good(), "hardgrab test original should be writable");
    }
    hardgrab.raw_capture.original_path = hardgrab_path;
    hardgrab.raw_capture.output_path = hardgrab_path;
    hardgrab.raw_capture.size = 22;
    const CaptureTaskStartResult hardgrab_task = ReserveAndStart(&service, hardgrab);
    Require(WaitForTerminal(&service, "connection-a", hardgrab_task.task.task_id), "hardgrab task should finish");
    Require(device->CaptureStillCount() == 2, "hardgrab must not call CaptureStill a second time");
    const CaptureTaskSnapshot hardgrab_snapshot = service.GetTask("connection-a", hardgrab_task.task.task_id);
    Require(hardgrab_snapshot.capture_source == "hardgrab", "hardgrab task source should be visible");

    std::this_thread::sleep_for(std::chrono::milliseconds(1550));
    CaptureTaskStartRequest missing_hardgrab = MakeRequest("connection-a");
    missing_hardgrab.raw_capture.captured = true;
    missing_hardgrab.raw_capture.content_type = "image/jpeg";
    missing_hardgrab.raw_capture.original_path = JoinPath(GetSdkOpenCaptureDir(), "missing-hardgrab.jpg");
    missing_hardgrab.raw_capture.output_path = missing_hardgrab.raw_capture.original_path;
    const CaptureTaskStartResult missing_task = ReserveAndStart(&service, missing_hardgrab);
    Require(WaitForTerminal(&service, "connection-a", missing_task.task.task_id),
            "missing hardgrab source should become terminal");
    const CaptureTaskSnapshot missing_snapshot = service.GetTask("connection-a", missing_task.task.task_id);
    Require(missing_snapshot.status == "failed" && missing_snapshot.acquisition_status == "failed",
            "missing hardgrab source should fail during acquisition staging");

    const CaptureSessionSummary summary = service.GetSessionSummary("connection-a");
    Require(summary.captured_count == 3 && summary.processed_count == 3 && summary.failed_count == 1 && summary.pending_count == 0,
            "capture summary should count acquired, processed and staging failures");
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
    SessionEventRecorder events;
    CaptureTaskService service(providers);
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

void TestCancelAndWaitLifecycle() {
    std::shared_ptr<TestDeviceProvider> device(new TestDeviceProvider);
    std::shared_ptr<TestGraphicProvider> graphic(new TestGraphicProvider);
    ProviderBundle providers;
    providers.device_provider = device;
    providers.graphic_provider = graphic;
    CaptureTaskService service(providers);

    device->BlockNextCapture();
    CaptureReleaseGuard release_capture(device);
    const CaptureTaskStartResult started = ReserveAndStart(&service, MakeRequest("cancel-owner"));
    Require(device->WaitForCaptureStart(1000), "cancel test capture should start");
    const CaptureTaskSnapshot foreign = service.CancelTask("other-owner", started.task.task_id);
    Require(foreign.code == ToCode(SdkStatusCode::CapabilityNotAllowed) && foreign.task_id.empty(),
            "foreign cancellation should fail with an empty payload");

    const CaptureTaskSnapshot cancelling = service.CancelTask("cancel-owner", started.task.task_id);
    Require(cancelling.status == "cancelling" && cancelling.code == ToCode(SdkStatusCode::Ok),
            "active cancellation should expose cancelling state");
    Require(!service.CancelAndWait("cancel-owner", 20), "blocked provider should make bounded drain time out");
    Require(service.ActiveTaskCount() == 1, "timed out drain must preserve activity ownership");

    device->ReleaseCapture();
    Require(service.CancelAndWait("cancel-owner", 2000), "drain should succeed after provider returns");
    const CaptureTaskSnapshot cancelled = service.GetTask("cancel-owner", started.task.task_id);
    Require(cancelled.status == "cancelled" && cancelled.code == ToCode(SdkStatusCode::Ok),
            "cancelled task should converge to cancelled terminal state");
    Require(service.CancelAndWait("cancel-owner", 20), "repeated drain should be idempotent");
}

void TestReservedAndAdmissionCancellation() {
    std::shared_ptr<TestDeviceProvider> device(new TestDeviceProvider);
    std::shared_ptr<TestGraphicProvider> graphic(new TestGraphicProvider);
    ProviderBundle providers;
    providers.device_provider = device;
    providers.graphic_provider = graphic;
    CaptureTaskService service(providers);

    const CaptureTaskStartResult reserved = service.ReserveTask(MakeRequest("reserved-owner"));
    Require(reserved.accepted, "reserved task should be accepted");
    const CaptureTaskSnapshot cancelled = service.CancelTask("reserved-owner", reserved.task.task_id);
    Require(cancelled.status == "cancelled" && cancelled.code == ToCode(SdkStatusCode::Ok),
            "reserved cancel should terminalize the task");
    Require(service.ActiveTaskCount() == 0, "reserved cancel should release active ownership");
    Require(service.CancelAndWait("reserved-owner", 100), "reserved cancel drain should be immediate");

    const CaptureTaskStartResult reusable = service.ReserveTask(MakeRequest("reserved-owner"));
    Require(reusable.accepted, "reserved cancellation should release device admission");
    service.AbortReservedTask(reusable.task.task_id);

    device->BlockNextCapture();
    CaptureReleaseGuard release_guard(device);
    const CaptureTaskStartResult started = ReserveAndStart(&service, MakeRequest("admission-owner"));
    Require(started.accepted, "started task should be accepted");
    const CaptureTaskStartResult second_start = service.StartReservedTask(started.task.task_id);
    Require(second_start.code == ToCode(SdkStatusCode::InvalidParams),
            "starting the same reserved task twice should be rejected");
    service.AbortReservedTask(started.task.task_id);
    Require(service.GetTask("admission-owner", started.task.task_id).task_id == started.task.task_id,
            "abort must not erase an already started task");
}

void TestQueuedOwnerCancellationDoesNotCancelOtherOwner() {
    std::shared_ptr<TestDeviceProvider> device(new TestDeviceProvider);
    std::shared_ptr<TestGraphicProvider> graphic(new TestGraphicProvider);
    ProviderBundle providers;
    providers.device_provider = device;
    providers.graphic_provider = graphic;
    CaptureTaskService service(providers);

    graphic->BlockFirstPageProcess();
    PageProcessReleaseGuard release_graphic(graphic);
    const CaptureTaskStartResult owner_b = ReserveAndStart(&service, MakeRequest("owner-b"));
    Require(graphic->WaitForFirstPageProcess(2000), "owner B should block in processing");

    std::this_thread::sleep_for(std::chrono::milliseconds(1550));
    const CaptureTaskStartResult owner_a = ReserveAndStart(&service, MakeRequest("owner-a"));
    Require(WaitUntil([&service, &owner_a]() {
                return service.GetTask("owner-a", owner_a.task.task_id).processing_status == "queued";
            }, 2000),
            "owner A should queue behind owner B processing");
    const CaptureTaskSnapshot cancelling = service.CancelTask("owner-a", owner_a.task.task_id);
    Require(cancelling.status == "cancelled", "queued owner A has no executing provider and cancels immediately");
    Require(service.CancelAndWait("owner-a", 1000), "owner A drain should not wait for owner B");
    Require(service.GetTask("owner-a", owner_a.task.task_id).status == "cancelled",
            "queued owner A should become cancelled");
    Require(service.ActiveTaskCount() >= 1, "owner B should remain active");

    Require(service.GetSessionSummary("owner-a").pending_count == 0, "cancelled queue must release pending summary");
    Require(service.GetTask("owner-b", owner_b.task.task_id).status == "running", "owner B must not be cancelled");
    release_graphic.Release();
    Require(WaitForTerminal(&service, "owner-b", owner_b.task.task_id), "owner B must finish normally");
    Require(service.GetTask("owner-b", owner_b.task.task_id).status == "succeeded", "owner B must succeed");
}

void TestProcessingCancellationAndTerminalEventActivity() {
    BlockingCompletionEvent blocked_event; // outlives the service and its event sink
    std::shared_ptr<TestDeviceProvider> device(new TestDeviceProvider);
    std::shared_ptr<TestGraphicProvider> graphic(new TestGraphicProvider);
    ProviderBundle providers;
    providers.device_provider = device;
    providers.graphic_provider = graphic;
    CaptureTaskService service(providers);

    graphic->BlockFirstPageProcess();
    PageProcessReleaseGuard release_graphic(graphic);
    const CaptureTaskStartResult started = ReserveAndStart(&service, MakeRequest("processing-owner"));
    Require(graphic->WaitForFirstPageProcess(2000), "task should enter blocked processing");
    Require(service.CancelTask("processing-owner", started.task.task_id).status == "cancelling",
            "processing task should enter cancelling state");
    Require(!service.CancelAndWait("processing-owner", 20), "blocked processing should time out drain");
    Require(service.ActiveTaskCount() == 1, "timed out processing drain should preserve ownership");
    release_graphic.Release();
    Require(service.CancelAndWait("processing-owner", 3000), "processing task should drain after provider return");
    Require(service.GetTask("processing-owner", started.task.task_id).status == "cancelled",
            "processing task should converge to cancelled");

    Require(service.GetTask("processing-owner", started.task.task_id).error.empty(),
            "cancelled processing is not an error");
    Require(service.ClearFinishedTasks() == 1, "clear the previous cancelled task before event ownership checks");
    struct EventReleaseGuard {
        BlockingCompletionEvent* event;
        ~EventReleaseGuard() { event->Release(); }
    } release_event = {&blocked_event};
    service.SetEventSink([&blocked_event](const std::string& connection_id, const Json& event) {
        blocked_event.Record(connection_id, event);
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(1550));
    const CaptureTaskStartResult completed = ReserveAndStart(&service, MakeRequest("event-owner"));
    Require(blocked_event.WaitForEntry(3000), "terminal completion event should enter blocked sink");
    Require(service.ActiveTaskCount() == 1, "blocked terminal event must retain active ownership");
    Require(service.ClearFinishedTasks() == 0, "blocked terminal event must not be cleared");
    Require(!service.CancelAndWait("event-owner", 20), "blocked terminal event should time out drain");
    blocked_event.Release();
    Require(service.CancelAndWait("event-owner", 3000), "event ownership should drain after callback release");
    Require(service.CancelTask("event-owner", completed.task.task_id).status == "succeeded",
            "cancelling a succeeded task should return original terminal snapshot");
}


void TestCaptureDefaultOnlineEnhanceDoesNotConfirmQuota() {
    std::shared_ptr<TestDeviceProvider> device(new TestDeviceProvider);
    std::shared_ptr<TestGraphicProvider> graphic(new TestGraphicProvider);
    std::shared_ptr<TestImageEnhanceProvider> enhance(new TestImageEnhanceProvider);
    std::shared_ptr<TestAuthProvider> auth(new TestAuthProvider);
    ProviderBundle providers;
    providers.device_provider = device;
    providers.graphic_provider = graphic;
    providers.image_enhance_provider = enhance;
    providers.auth_provider = auth;
    CaptureTaskService service(providers);

    const CaptureTaskStartResult started = ReserveAndStart(&service, MakeOnlineEnhanceRequest("capture-default-quota"));
    Require(WaitForTerminal(&service, "capture-default-quota", started.task.task_id), "default capture enhance should finish");
    const CaptureTaskSnapshot task = service.GetTask("capture-default-quota", started.task.task_id);
    Require(task.status == "succeeded" && !task.assets.empty(), "default capture enhance should publish enhanced assets");
    Require(enhance->RunStepCount() == 1, "default capture enhance should execute configured step");
    Require(auth->ConsumeCount() == 0, "default capture enhance must not confirm online quota");
}

void TestCaptureOnlineEnhanceQuotaOptInSuccess() {
    std::shared_ptr<TestDeviceProvider> device(new TestDeviceProvider);
    std::shared_ptr<TestGraphicProvider> graphic(new TestGraphicProvider);
    std::shared_ptr<TestImageEnhanceProvider> enhance(new TestImageEnhanceProvider);
    std::shared_ptr<TestAuthProvider> auth(new TestAuthProvider);
    ProviderBundle providers;
    providers.device_provider = device;
    providers.graphic_provider = graphic;
    providers.image_enhance_provider = enhance;
    providers.auth_provider = auth;
    CaptureTaskService service(providers);

    CaptureTaskStartRequest request = MakeOnlineEnhanceRequest("capture-quota-success");
    request.confirm_online_enhance_quota = true;
    enhance->SetNextOutputCount(2);
    const CaptureTaskStartResult started = ReserveAndStart(&service, request);
    Require(WaitForTerminal(&service, "capture-quota-success", started.task.task_id), "quota opt-in capture should finish");
    const CaptureTaskSnapshot task = service.GetTask("capture-quota-success", started.task.task_id);
    Require(task.status == "succeeded" && task.assets.size() == 2, "quota success should publish enhanced assets");
    const QuotaConsumeRequest quota = auth->LastRequest();
    Require(auth->ConsumeCount() == 1, "quota success should confirm once per capability");
    Require(quota.capability == "doc_crop_enhance", "quota should normalize online capability aliases");
    Require(quota.units == 2, "quota should count successful output pages");
    Require(quota.token == request.online_api_key && quota.session_token == request.session_token &&
                quota.authz_base_url == request.authz_base_url,
            "quota should forward online credentials");
    Require(quota.request_id == "image.enhance:" + started.task.task_id + ":doc_crop_enhance",
            "quota request id should preserve image enhance prefix semantics");
    Require(enhance->LastRequest().online_base_url == request.online_base_url,
            "capture step should forward online enhance base URL to provider");
}

void TestCaptureOnlineEnhanceQuotaFailureDoesNotPublishAssetsAndDeviceRecovers() {
    std::shared_ptr<TestDeviceProvider> device(new TestDeviceProvider);
    std::shared_ptr<TestGraphicProvider> graphic(new TestGraphicProvider);
    std::shared_ptr<TestImageEnhanceProvider> enhance(new TestImageEnhanceProvider);
    std::shared_ptr<TestAuthProvider> auth(new TestAuthProvider);
    ProviderBundle providers;
    providers.device_provider = device;
    providers.graphic_provider = graphic;
    providers.image_enhance_provider = enhance;
    providers.auth_provider = auth;
    CaptureTaskService service(providers);

    CaptureTaskStartRequest request = MakeOnlineEnhanceRequest("capture-quota-failure");
    request.confirm_online_enhance_quota = true;
    auth->FailNextConsume();
    const CaptureTaskStartResult failed = ReserveAndStart(&service, request);
    Require(WaitForTerminal(&service, "capture-quota-failure", failed.task.task_id), "quota failure should finish terminal");
    const CaptureTaskSnapshot failed_task = service.GetTask("capture-quota-failure", failed.task.task_id);
    Require(failed_task.status == "failed" && failed_task.assets.empty(),
            "quota failure must not publish enhanced assets");
    Require(failed_task.code == ToCode(SdkStatusCode::UsageLimitExceeded), "quota failure should preserve auth code");

    std::this_thread::sleep_for(std::chrono::milliseconds(1550));
    const CaptureTaskStartResult recovered = ReserveAndStart(&service, MakeRequest("capture-quota-failure"));
    Require(WaitForTerminal(&service, "capture-quota-failure", recovered.task.task_id), "device should accept a later task");
    Require(service.GetTask("capture-quota-failure", recovered.task.task_id).status == "succeeded",
            "later capture should succeed after quota failure");
}

void TestCaptureOnlineEnhanceQuotaMissingProviderAndCancelAfterConfirm() {
    std::shared_ptr<TestDeviceProvider> device(new TestDeviceProvider);
    std::shared_ptr<TestGraphicProvider> graphic(new TestGraphicProvider);
    std::shared_ptr<TestImageEnhanceProvider> enhance(new TestImageEnhanceProvider);
    ProviderBundle missing_auth;
    missing_auth.device_provider = device;
    missing_auth.graphic_provider = graphic;
    missing_auth.image_enhance_provider = enhance;
    CaptureTaskService missing_service(missing_auth);

    CaptureTaskStartRequest missing_request = MakeOnlineEnhanceRequest("capture-quota-missing-auth");
    missing_request.confirm_online_enhance_quota = true;
    const CaptureTaskStartResult missing = ReserveAndStart(&missing_service, missing_request);
    Require(WaitForTerminal(&missing_service, "capture-quota-missing-auth", missing.task.task_id),
            "missing auth provider should finish terminal");
    const CaptureTaskSnapshot missing_task = missing_service.GetTask("capture-quota-missing-auth", missing.task.task_id);
    Require(missing_task.status == "failed" && missing_task.assets.empty() &&
                missing_task.code == ToCode(SdkStatusCode::ProviderNotReady),
            "missing auth provider should fail quota confirmation before publication");

    std::this_thread::sleep_for(std::chrono::milliseconds(1550));
    std::shared_ptr<TestAuthProvider> auth(new TestAuthProvider);
    ProviderBundle providers;
    providers.device_provider = device;
    providers.graphic_provider = graphic;
    providers.image_enhance_provider = enhance;
    providers.auth_provider = auth;
    CaptureTaskService service(providers);
    auth->on_consume = [&service](const QuotaConsumeRequest& request) {
        const std::string prefix = "image.enhance:";
        const std::string::size_type start = request.request_id.find(prefix);
        const std::string::size_type end = request.request_id.rfind(":" + request.capability);
        if (start != std::string::npos && end != std::string::npos && end > start + prefix.size()) {
            const std::string task_id = request.request_id.substr(start + prefix.size(), end - start - prefix.size());
            service.CancelTask("capture-quota-cancel", task_id);
        }
    };
    CaptureTaskStartRequest cancel_request = MakeOnlineEnhanceRequest("capture-quota-cancel");
    cancel_request.confirm_online_enhance_quota = true;
    const CaptureTaskStartResult started = ReserveAndStart(&service, cancel_request);
    Require(WaitUntil([&service, &started]() {
                return service.GetTask("capture-quota-cancel", started.task.task_id).status == "cancelled";
            }, 5000),
            "cancellation requested during quota confirmation should win before publication");
    const CaptureTaskSnapshot cancelled = service.GetTask("capture-quota-cancel", started.task.task_id);
    Require(auth->ConsumeCount() == 1, "already-started quota confirmation is not refunded by cancellation");
    for (const auto& asset : cancelled.assets) {
        Require(asset.asset_id.find("asset-enhanced-final-") != 0,
                "cancel after quota confirmation must not expose enhanced assets");
    }
}

void TestCaptureQuotaExceptionDoesNotExposeCredentials() {
    auto auth = std::make_shared<TestAuthProvider>();
    ProviderBundle providers;
    providers.device_provider.reset(new TestDeviceProvider);
    providers.graphic_provider.reset(new TestGraphicProvider);
    providers.image_enhance_provider.reset(new TestImageEnhanceProvider);
    providers.auth_provider = auth;
    CaptureTaskService service(providers);
    auth->on_consume = [](const QuotaConsumeRequest&) { throw std::runtime_error("private auth credential"); };
    auto request = MakeOnlineEnhanceRequest("quota-exception");
    request.confirm_online_enhance_quota = true;
    const auto task = ReserveAndStart(&service, request);
    Require(WaitForTerminal(&service, request.connection_id, task.task.task_id), "quota exception must settle");
    const auto result = service.GetTask(request.connection_id, task.task.task_id);
    Require(result.status == "failed" && result.code == ToCode(SdkStatusCode::InternalError) &&
                result.assets.empty() && result.message == "online image enhance quota confirmation failed",
            "quota exception must use a stable public error and withhold enhanced assets");
    Require(service.CancelAndWait(request.connection_id, 2000), "quota exception must release processing ownership");
}

class SilentCaptureProvider : public TestDeviceProvider {
public:
    void CaptureStill(const SdkCaptureRequest&, SdkCaptureCallback) override {}
};

void TestCaptureErrorCodeOptInPreservesOpenDefault() {
    for (int preserve = 0; preserve < 2; ++preserve) {
        ProviderBundle providers;
        providers.device_provider.reset(new SilentCaptureProvider);
        providers.graphic_provider.reset(new TestGraphicProvider);
        CaptureTaskService service(providers);
        CaptureTaskStartRequest request = MakeRequest("timeout-code");
        request.timeout_ms = 25;
        request.preserve_acquisition_error_code = preserve != 0;
        const auto task = ReserveAndStart(&service, request);
        Require(WaitForTerminal(&service, request.connection_id, task.task.task_id), "short timeout must finish");
        const auto result = service.GetTask(request.connection_id, task.task.task_id);
        Require(result.status == "failed" && result.message == "capture timeout" &&
                    result.code == ToCode(preserve ? SdkStatusCode::CaptureTimeout : SdkStatusCode::CaptureFailed),
                "Local opt-in preserves timeout code; Open keeps the historic failure code");
    }
}

class CancellingEnhanceProvider : public TestImageEnhanceProvider {
public:
    std::function<void(const std::string&)> cancel;
    SdkImageEnhanceStepResult RunStep(const SdkImageEnhanceStepRequest& request) override {
        const auto result = TestImageEnhanceProvider::RunStep(request);
        cancel(request.task_id); // successful processing, but cancellation precedes quota admission
        return result;
    }
};

void TestCaptureCancelledBeforeQuotaConfirmation() {
    auto enhance = std::make_shared<CancellingEnhanceProvider>();
    auto auth = std::make_shared<TestAuthProvider>();
    ProviderBundle providers;
    providers.device_provider.reset(new TestDeviceProvider);
    providers.graphic_provider.reset(new TestGraphicProvider);
    providers.image_enhance_provider = enhance;
    providers.auth_provider = auth;
    CaptureTaskService service(providers);
    enhance->cancel = [&service](const std::string& id) { service.CancelTask("cancel-before-quota", id); };
    auto request = MakeOnlineEnhanceRequest("cancel-before-quota");
    request.confirm_online_enhance_quota = true;
    const auto task = ReserveAndStart(&service, request);
    Require(WaitUntil([&]() {
        return service.GetTask(request.connection_id, task.task.task_id).status == "cancelled";
    }, 5000), "cancelled enhancement must settle");
    Require(service.GetTask(request.connection_id, task.task.task_id).status == "cancelled" &&
                auth->ConsumeCount() == 0, "cancellation observed before quota admission must skip confirmation");
}

void TestCaptureQuotaConfirmationDrain() {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    bool released = false;
    auto auth = std::make_shared<TestAuthProvider>();
    ProviderBundle providers;
    providers.device_provider.reset(new TestDeviceProvider);
    providers.graphic_provider.reset(new TestGraphicProvider);
    providers.image_enhance_provider.reset(new TestImageEnhanceProvider);
    providers.auth_provider = auth;
    CaptureTaskService service(providers);
    struct ReleaseGuard {
        std::mutex& mutex;
        std::condition_variable& changed;
        bool& released;
        void Release() {
            std::lock_guard<std::mutex> lock(mutex);
            released = true;
            changed.notify_all();
        }
        ~ReleaseGuard() { Release(); }
    } release = {mutex, changed, released};
    auth->on_consume = [&](const QuotaConsumeRequest&) {
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        changed.notify_all();
        changed.wait(lock, [&]() { return released; });
    };
    auto request = MakeOnlineEnhanceRequest("quota-drain");
    request.confirm_online_enhance_quota = true;
    const auto task = ReserveAndStart(&service, request);
    {
        std::unique_lock<std::mutex> lock(mutex);
        Require(changed.wait_for(lock, std::chrono::seconds(3), [&]() { return entered; }),
                "quota confirmation must reach provider");
    }
    Require(service.ActiveTaskCount() == 1 && service.ClearFinishedTasks() == 0,
            "quota confirmation must retain processing activity and cleanup ownership");
    Require(!service.CancelAndWait(request.connection_id, 20), "blocked quota provider must prevent early drain");
    Require(service.GetTask(request.connection_id, task.task.task_id).status == "cancelling",
            "quota cancellation remains pending until the provider returns");
    release.Release();
    Require(service.CancelAndWait(request.connection_id, 2000), "quota drain retry should wait for actual return");
    const auto result = service.GetTask(request.connection_id, task.task.task_id);
    Require(result.status == "cancelled" && auth->ConsumeCount() == 1, "started confirmation may consume despite cancellation");
    for (const auto& asset : result.assets) {
        Require(asset.asset_id.find("asset-enhanced-final-") != 0, "cancelled confirmation must not expose enhanced outputs");
    }
}

void TestTerminalFailureCancelIsIdempotent() {
    std::shared_ptr<TestDeviceProvider> device(new TestDeviceProvider);
    std::shared_ptr<TestGraphicProvider> graphic(new TestGraphicProvider);
    ProviderBundle providers;
    providers.device_provider = device;
    providers.graphic_provider = graphic;
    CaptureTaskService service(providers);

    const CaptureTaskSnapshot missing = service.CancelTask("terminal-owner", "missing-task");
    Require(missing.code == ToCode(SdkStatusCode::InvalidParams) && missing.task_id.empty(),
            "missing cancellation should fail with an empty payload");
    device->FailNextCapture();
    const CaptureTaskStartResult failed = ReserveAndStart(&service, MakeRequest("terminal-owner"));
    Require(WaitForTerminal(&service, "terminal-owner", failed.task.task_id), "failure should become terminal");
    const CaptureTaskSnapshot first = service.CancelTask("terminal-owner", failed.task.task_id);
    const CaptureTaskSnapshot second = service.CancelTask("terminal-owner", failed.task.task_id);
    Require(first.status == "failed" && second.status == "failed" &&
                first.code == ToCode(SdkStatusCode::CaptureFailed) &&
                second.code == ToCode(SdkStatusCode::CaptureFailed),
            "cancelling a failed task should be idempotent and preserve failure");
}

void TestAcquisitionEventOutlivesProcessing() {
    BlockingCompletionEvent blocked("capture.session.updated", "raw_captured");
    ProviderBundle providers;
    providers.device_provider.reset(new TestDeviceProvider);
    providers.graphic_provider.reset(new TestGraphicProvider);
    CaptureTaskService service(providers);
    struct ReleaseGuard {
        BlockingCompletionEvent* event;
        ~ReleaseGuard() { event->Release(); }
    } release = {&blocked};
    service.SetEventSink([&blocked](const std::string& connection, const Json& event) {
        blocked.Record(connection, event);
    });
    const auto started = ReserveAndStart(&service, MakeRequest("acquisition-tail"));
    Require(blocked.WaitForEntry(2000), "acquisition event should block independently of processing");
    Require(WaitForTerminal(&service, "acquisition-tail", started.task.task_id), "processing should finish while raw event is blocked");
    Require(service.ActiveTaskCount() == 1 && service.ClearFinishedTasks() == 0,
            "acquisition tail must protect terminal storage after processing exits");
    Require(!service.CancelAndWait("acquisition-tail", 20), "drain must include acquisition event tail");
    blocked.Release();
    Require(service.CancelAndWait("acquisition-tail", 2000), "drain must finish after both activities exit");
}

void TestSnapshotEventSinkPreservesSnapshotsAndIsolatesFailures() {
    ProviderBundle providers;
    providers.device_provider.reset(new TestDeviceProvider);
    providers.graphic_provider.reset(new TestGraphicProvider);
    CaptureTaskService service(providers);

    std::mutex callback_mu;
    std::vector<SnapshotEventRecord> snapshot_events;
    int legacy_event_count = 0;
    service.SetEventSink([&](const std::string&, const Json& event) {
        if (event.value("event", std::string()).find("capture.") == 0) {
            std::lock_guard<std::mutex> lock(callback_mu);
            ++legacy_event_count;
        }
    });
    service.SetSnapshotEventSink(
        [&](const std::string& connection_id,
            const std::string& event,
            const CaptureTaskSnapshot& task,
            const SdkCaptureStageResult* stage) {
            SnapshotEventRecord record;
            record.connection_id = connection_id;
            record.event = event;
            record.task = task;
            if (stage != NULL) {
                record.has_stage = true;
                record.stage = *stage;
            }
            {
                std::lock_guard<std::mutex> lock(callback_mu);
                snapshot_events.push_back(record);
            }
            // A faulty typed consumer must not suppress legacy JSON delivery
            // or strand the worker's activity reference.
            throw std::runtime_error("typed snapshot consumer failed");
        });

    const std::string connection_id = "typed-snapshot-owner";
    const CaptureTaskStartResult started = ReserveAndStart(&service, MakeRequest(connection_id));
    Require(WaitForTerminal(&service, connection_id, started.task.task_id),
            "typed snapshot task should finish");
    Require(service.CancelAndWait(connection_id, 2000),
            "typed snapshot callback activity should drain");

    SnapshotEventRecord started_event;
    SnapshotEventRecord terminal_event;
    SnapshotEventRecord stage_event;
    bool found_started = false;
    bool found_terminal = false;
    bool found_stage = false;
    {
        std::lock_guard<std::mutex> lock(callback_mu);
        for (std::vector<SnapshotEventRecord>::const_iterator it = snapshot_events.begin();
             it != snapshot_events.end(); ++it) {
            Require(it->connection_id == connection_id,
                    "typed snapshot callback must preserve the owning connection");
            Require(it->task.connection_id == connection_id,
                    "typed task snapshot must preserve the owning connection");
            Require(it->task.task_id == started.task.task_id,
                    "typed task snapshot must preserve the original task id");
            if (it->event == "capture.started") {
                started_event = *it;
                found_started = true;
            } else if (it->event == "capture.stage.updated" && it->has_stage) {
                stage_event = *it;
                found_stage = true;
            } else if (it->event == "capture.completed") {
                terminal_event = *it;
                found_terminal = true;
            }
        }
        Require(legacy_event_count > 0,
                "throwing typed snapshot sink must not suppress legacy JSON events");
    }
    Require(found_started && started_event.task.status == "running",
            "started typed event must contain its original running snapshot");
    Require(found_stage && !stage_event.stage.name.empty(),
            "stage typed event must contain the original stage payload");
    Require(found_terminal && terminal_event.task.status == "succeeded",
            "terminal typed event must contain its original succeeded snapshot");
    Require(terminal_event.task.connection_id == connection_id,
            "terminal typed snapshot must remain scoped to its owner");
}

void TestSnapshotEventSinkReverseIsolationAndSetterReentrancy() {
    {
        ProviderBundle providers;
        providers.device_provider.reset(new TestDeviceProvider);
        providers.graphic_provider.reset(new TestGraphicProvider);
        CaptureTaskService service(providers);
        std::shared_ptr<EventCount> typed_event_count(new EventCount());
        service.SetEventSink([](const std::string&, const Json&) {
            throw std::runtime_error("legacy event consumer failed");
        });
        service.SetSnapshotEventSink(
            [typed_event_count](const std::string&,
                                const std::string&,
                                const CaptureTaskSnapshot&,
                                const SdkCaptureStageResult*) {
                std::lock_guard<std::mutex> lock(typed_event_count->mu);
                ++typed_event_count->count;
            });

        const CaptureTaskStartResult started = ReserveAndStart(&service, MakeRequest("reverse-isolation"));
        Require(WaitForTerminal(&service, "reverse-isolation", started.task.task_id),
                "legacy throwing sink task should finish");
        Require(service.CancelAndWait("reverse-isolation", 2000),
                "legacy throwing sink activity should drain");
        {
            std::lock_guard<std::mutex> lock(typed_event_count->mu);
            Require(typed_event_count->count > 0,
                    "throwing legacy sink must not suppress typed snapshot delivery");
        }
    }

    {
        ProviderBundle providers;
        providers.device_provider.reset(new TestDeviceProvider);
        providers.graphic_provider.reset(new TestGraphicProvider);
        CaptureTaskService service(providers);
        std::shared_ptr<SnapshotCopyControl> copy_control(new SnapshotCopyControl());
        CaptureTaskService::SnapshotEventSink snapshot_sink{
            ThrowOnSnapshotSinkCopy(copy_control)};
        service.SetSnapshotEventSink(std::move(snapshot_sink));
        copy_control->throw_on_copy = true;

        std::shared_ptr<EventCount> legacy_event_count(new EventCount());
        service.SetEventSink(
            [legacy_event_count](const std::string&, const Json& event) {
                if (event.value("event", std::string()).find("capture.") == 0) {
                    std::lock_guard<std::mutex> lock(legacy_event_count->mu);
                    ++legacy_event_count->count;
                }
            });
        const CaptureTaskStartResult started = ReserveAndStart(&service, MakeRequest("copy-isolation"));
        Require(WaitForTerminal(&service, "copy-isolation", started.task.task_id),
                "copy throwing snapshot sink task should finish");
        Require(service.CancelAndWait("copy-isolation", 2000),
                "copy throwing snapshot sink activity should drain");
        Require(copy_control->callback_count == 0,
                "copy-throwing snapshot sink must not be invoked");
        {
            std::lock_guard<std::mutex> lock(legacy_event_count->mu);
            Require(legacy_event_count->count > 0,
                    "snapshot sink copy failure must not suppress legacy JSON delivery");
        }
    }

    {
        ProviderBundle providers;
        providers.device_provider.reset(new TestDeviceProvider);
        providers.graphic_provider.reset(new TestGraphicProvider);
        CaptureTaskService service(providers);

        std::shared_ptr<bool> event_armed(new bool(false));
        std::shared_ptr<bool> event_fired(new bool(false));
        CaptureTaskService::EventSink event_sink(
            ReentrantEventSink(&service, false, event_armed, event_fired));
        service.SetEventSink(std::move(event_sink));
        *event_armed = true;
        service.SetEventSink(CaptureTaskService::EventSink());
        Require(*event_fired,
                "legacy sink replacement must destroy the old callable outside the service lock");

        std::shared_ptr<bool> snapshot_armed(new bool(false));
        std::shared_ptr<bool> snapshot_fired(new bool(false));
        CaptureTaskService::SnapshotEventSink snapshot_sink(
            ReentrantSnapshotSink(&service, snapshot_armed, snapshot_fired));
        service.SetSnapshotEventSink(std::move(snapshot_sink));
        *snapshot_armed = true;
        service.SetSnapshotEventSink(CaptureTaskService::SnapshotEventSink());
        Require(*snapshot_fired,
                "snapshot sink replacement must destroy the old callable outside the service lock");
    }
}

class CancellingOutputProvider : public TestGraphicProvider {
public:
    bool cancelled = false;
    int colors = 0;
    int formats = 0;
    SdkPageProcessResult ProcessPage(const SdkPageProcessRequest& request) override {
        SdkPageProcessResult result;
        result.processed = true;
        for (int i = 0; i < 2; ++i) {
            SdkPageOutput page;
            page.output_id = "page-" + std::to_string(i);
            page.index = i;
            page.path = request.input_path;
            result.outputs.push_back(page);
        }
        return result;
    }
    SdkColorModeResult ApplyColorMode(const SdkColorModeRequest& request) override {
        ++colors;
        cancelled = true; // observe cancellation inside the first output's substage
        return TestGraphicProvider::ApplyColorMode(request);
    }
    SdkFormatConvertResult ConvertImageFormat(const SdkFormatConvertRequest& request) override {
        ++formats;
        return TestGraphicProvider::ConvertImageFormat(request);
    }
};

void TestCancellationInsideOutputWorkflow() {
    auto graphic = std::make_shared<CancellingOutputProvider>();
    ProviderBundle providers;
    providers.device_provider.reset(new TestDeviceProvider);
    providers.graphic_provider = graphic;
    CapturePipelineService pipeline(providers);
    CapturePipelineRequest request;
    request.task_id = "cancel-output-workflow";
    request.device_id = "test-device";
    request.profile = MakeRequest("unused").profile;
    request.profile.output_format = "png";
    request.should_cancel = [graphic]() { return graphic->cancelled; };
    const CapturePipelineResult result = pipeline.Run(request, CaptureStageCallback());
    Require(result.status == "cancelled" && IsOkStatusCode(result.code), "output-stage cancellation must remain a cancelled result");
    Require(graphic->colors == 1 && graphic->formats == 0,
            "cancellation must skip format conversion and all following output pages");
}

class ThrowingGraphicProvider : public TestGraphicProvider {
public:
    mutable bool throw_name = true;
    std::string ProviderName() const override {
        if (throw_name) {
            throw_name = false;
            throw 17; // exercise the non-std::exception thread boundary
        }
        return TestGraphicProvider::ProviderName();
    }
};

void TestProcessingExceptionAllowsLaterTask() {
    ProviderBundle providers;
    providers.device_provider.reset(new TestDeviceProvider);
    providers.graphic_provider.reset(new ThrowingGraphicProvider);
    CaptureTaskService service(providers);
    const auto first = ReserveAndStart(&service, MakeRequest("throwing-owner"));
    Require(WaitForTerminal(&service, "throwing-owner", first.task.task_id), "provider exception must reach terminal");
    Require(service.GetTask("throwing-owner", first.task.task_id).status == "failed", "provider exception must not be success");
    Require(service.CancelAndWait("throwing-owner", 2000), "exception must release activity");
    Require(service.GetSessionSummary("throwing-owner").pending_count == 0, "exception must release pending summary");
    std::this_thread::sleep_for(std::chrono::milliseconds(1550));
    const auto second = ReserveAndStart(&service, MakeRequest("throwing-owner"));
    Require(WaitForTerminal(&service, "throwing-owner", second.task.task_id), "same device must accept a task after exception");
    Require(service.GetTask("throwing-owner", second.task.task_id).status == "succeeded", "subsequent processing must succeed");
}

} // namespace
} // namespace sdk
} // namespace editor

int main() {
    try {
        editor::sdk::TestDeviceBusyTakesPrecedenceOverCooldown();
        editor::sdk::TestFifoRateLimitHardGrabAndSummary();
        editor::sdk::TestFailureSessionUpdates();
        editor::sdk::TestCancelAndWaitLifecycle();
        editor::sdk::TestReservedAndAdmissionCancellation();
        editor::sdk::TestQueuedOwnerCancellationDoesNotCancelOtherOwner();
        editor::sdk::TestProcessingCancellationAndTerminalEventActivity();
        editor::sdk::TestCaptureQuotaExceptionDoesNotExposeCredentials();
        editor::sdk::TestCaptureErrorCodeOptInPreservesOpenDefault();
        editor::sdk::TestCaptureCancelledBeforeQuotaConfirmation();
        editor::sdk::TestCaptureQuotaConfirmationDrain();
        editor::sdk::TestCaptureDefaultOnlineEnhanceDoesNotConfirmQuota();
        editor::sdk::TestCaptureOnlineEnhanceQuotaOptInSuccess();
        editor::sdk::TestCaptureOnlineEnhanceQuotaFailureDoesNotPublishAssetsAndDeviceRecovers();
        editor::sdk::TestCaptureOnlineEnhanceQuotaMissingProviderAndCancelAfterConfirm();
        editor::sdk::TestTerminalFailureCancelIsIdempotent();
        editor::sdk::TestAcquisitionEventOutlivesProcessing();
        editor::sdk::TestSnapshotEventSinkPreservesSnapshotsAndIsolatesFailures();
        editor::sdk::TestSnapshotEventSinkReverseIsolationAndSetterReentrancy();
        editor::sdk::TestCancellationInsideOutputWorkflow();
        editor::sdk::TestProcessingExceptionAllowsLaterTask();
        std::cout << "sdk_open_capture_task_service_test passed" << std::endl;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "sdk_open_capture_task_service_test failed: " << error.what() << std::endl;
        return 1;
    }
}
