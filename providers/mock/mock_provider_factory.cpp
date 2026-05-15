// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "mock_provider_factory.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>

#include "sdk_logger.h"
#include "sdk_runtime_paths.h"
#include "sdk_status_code.h"

namespace editor {
namespace sdk {
namespace mock {

namespace {

std::string MaskToken(const std::string& token) {
    if (token.empty()) {
        return "<empty>";
    }
    if (token.size() <= 16) {
        return token.substr(0, token.size() / 2) + "...(len=" + std::to_string(token.size()) + ")";
    }
    return token.substr(0, 8) + "..." + token.substr(token.size() - 6) +
           " (len=" + std::to_string(token.size()) + ")";
}

class MockAuthProvider : public ISdkAuthProvider {
public:
    std::string ProviderName() const override { return "mock-auth-provider"; }

    AuthValidateResult ValidateToken(const AuthValidateRequest& request) override {
        AuthValidateResult result;
        SDK_OPEN_LOG_INFO("[mock_auth_provider] ValidateToken begin, token={}, now_ts={}",
                          MaskToken(request.token),
                          request.now_ts);
        if (request.token.empty()) {
            result.code = ToCode(SdkStatusCode::AuthRequired);
            result.message = "token required";
            SDK_OPEN_LOG_WARN("[mock_auth_provider] ValidateToken reject, code={}, message={}",
                              result.code,
                              result.message);
            return result;
        }
        if (request.token != "demo-token-42F8" && request.token != "mock-token") {
            result.code = ToCode(SdkStatusCode::TokenInvalid);
            result.message = "token invalid";
            SDK_OPEN_LOG_WARN("[mock_auth_provider] ValidateToken reject, code={}, message={}, note=mock provider only accepts demo-token-42F8/mock-token",
                              result.code,
                              result.message);
            return result;
        }
        result.auth_context.is_valid = true;
        result.auth_context.account_type = SdkAccountType::SvipPlus;
        result.auth_context.account_type_code = AccountTypeCode(SdkAccountType::SvipPlus);
        result.auth_context.auth_scene = "plugin";
        result.auth_context.license_mode = "offline_api_key";
        result.auth_context.entitlement_state = "offline_unlocked";
        result.auth_context.machine_code = "mock-machine-code";
        result.auth_context.device_scope.clear();
        SdkDeviceGrant first_device;
        first_device.vid = 4660;
        first_device.pid = 22136;
        result.auth_context.device_scope.push_back(first_device);
        SdkDeviceGrant second_device;
        second_device.vid = 4660;
        second_device.pid = 22137;
        result.auth_context.device_scope.push_back(second_device);
        result.auth_context.expires_at = 0;
        result.auth_context.capabilities = {
            "system.ping",
            "system.info",
            "system.capabilities",
            "auth.create_session",
            "auth.get_context",
            "auth.refresh_session",
            "auth.activate_offline",
            "auth.destroy_session",
            "device.list",
            "device.get",
            "device.open",
            "device.close",
            "capture.take",
            "capture.get",
            "video.start",
            "video.stop",
            "video.set_format",
            "video.set_profile",
            "image.process",
            "image.process_page",
            "image.apply_color_mode",
            "ocr.recognize",
            "file.convert",
        };
        SDK_OPEN_LOG_INFO("[mock_auth_provider] ValidateToken success, account_type={}, auth_scene={}, device_scope_count={}",
                          ToAccountTypeString(result.auth_context.account_type),
                          result.auth_context.auth_scene,
                          result.auth_context.device_scope.size());
        return result;
    }

    AuthRefreshResult CreateSession(const AuthValidateRequest& request) override {
        AuthRefreshResult result;
        const AuthValidateResult validate = ValidateToken(request);
        if (!IsOkStatusCode(validate.code)) {
            result.code = validate.code;
            result.message = validate.message;
            return result;
        }
        result.session_token = "mock-session-token";
        result.expires_in = 7200;
        result.auth_context = validate.auth_context;
        return result;
    }

    AuthRefreshResult RefreshSession(const AuthRefreshRequest& request) override {
        AuthRefreshResult result;
        SessionValidateRequest validate_request;
        validate_request.session_token = request.session_token;
        validate_request.now_ts = request.now_ts;
        const SessionValidateResult validate = ValidateSession(validate_request);
        if (!IsOkStatusCode(validate.code)) {
            result.code = validate.code;
            result.message = validate.message;
            return result;
        }
        result.session_token = "mock-session-token";
        result.expires_in = 7200;
        result.auth_context = validate.auth_context;
        return result;
    }

    SessionValidateResult ValidateSession(const SessionValidateRequest& request) override {
        SessionValidateResult result;
        if (request.session_token != "mock-session-token") {
            result.code = ToCode(SdkStatusCode::SessionTokenInvalid);
            result.message = "session token invalid";
            return result;
        }
        AuthValidateRequest validate_request;
        validate_request.token = "mock-token";
        validate_request.now_ts = request.now_ts;
        const AuthValidateResult validate = ValidateToken(validate_request);
        result.auth_context = validate.auth_context;
        return result;
    }

    AuthContextResult GetAuthContext(const AuthLookupRequest& request) override {
        AuthContextResult result;
        SessionValidateRequest session_request;
        session_request.session_token = request.session_token;
        session_request.now_ts = request.now_ts;
        const SessionValidateResult session = ValidateSession(session_request);
        result.code = session.code;
        result.message = session.message;
        result.via_session = IsOkStatusCode(session.code);
        result.auth_context = session.auth_context;
        return result;
    }

    OfflineActivateResult ActivateOffline(const OfflineActivateRequest& request) override {
        OfflineActivateResult result;
        if (request.auth_code.empty()) {
            result.code = ToCode(SdkStatusCode::InvalidParams);
            result.message = "auth_code required";
            return result;
        }
        AuthValidateRequest validate_request;
        validate_request.token = request.token.empty() ? "mock-token" : request.token;
        validate_request.now_ts = request.now_ts;
        const AuthRefreshResult session = CreateSession(validate_request);
        result.code = session.code;
        result.message = session.message;
        result.activated = IsOkStatusCode(session.code);
        result.session_token = session.session_token;
        result.expires_in = session.expires_in;
        result.auth_context = session.auth_context;
        return result;
    }

    QuotaConsumeResult ConsumeQuota(const QuotaConsumeRequest& request) override {
        QuotaConsumeResult result;
        AuthValidateRequest validate_request;
        validate_request.token = request.token.empty() ? "mock-token" : request.token;
        validate_request.now_ts = request.now_ts;
        const AuthValidateResult validate = ValidateToken(validate_request);
        result.code = validate.code;
        result.message = validate.message;
        result.consumed = IsOkStatusCode(validate.code);
        result.bucket = request.capability;
        result.limit = 0;
        result.remaining = 0;
        result.auth_context = validate.auth_context;
        return result;
    }
};

class MockDeviceProvider : public ISdkDeviceProvider {
public:
    ~MockDeviceProvider() override {
        StopAllStreams();
    }

    std::string ProviderName() const override { return "mock-device-provider"; }
    std::vector<SdkDeviceDescriptor> ListDevices() const override {
        std::vector<SdkDeviceDescriptor> devices;
        SdkDeviceDescriptor first;
        first.device_id = "mock-device-01";
        first.model = "ET18 Pro";
        first.display_name = "CZUR ET18 Pro";
        first.vid = 4660;
        first.pid = 22136;
        first.status = "online";
        first.authorized = true;
        first.supports_video = true;
        first.image_transfer_protocol = true;
        devices.push_back(first);

        SdkDeviceDescriptor second;
        second.device_id = "mock-device-02";
        second.model = "Aura X";
        second.display_name = "CZUR Aura X";
        second.vid = 4660;
        second.pid = 22137;
        second.status = "online";
        second.authorized = true;
        second.supports_video = true;
        second.image_transfer_protocol = false;
        devices.push_back(second);
        return devices;
    }

    SdkDeviceOpenResult GetDevice(const SdkDeviceOpenRequest& request) override {
        SdkDeviceOpenResult result;
        const std::vector<SdkDeviceDescriptor> devices = ListDevices();
        for (std::vector<SdkDeviceDescriptor>::const_iterator it = devices.begin(); it != devices.end(); ++it) {
            if (it->device_id == request.device_id) {
                result.opened = IsOpened(request.device_id);
                result.device = *it;
                result.device.resolutions = DefaultResolutions();
                return result;
            }
        }
        result.code = ToCode(SdkStatusCode::DeviceNotFound);
        result.message = "device not found";
        return result;
    }

    SdkDeviceOpenResult OpenDevice(const SdkDeviceOpenRequest& request) override {
        SdkDeviceOpenResult result;
        result = GetDevice(request);
        if (!IsOkStatusCode(result.code)) {
            return result;
        }
        {
            std::lock_guard<std::mutex> lock(mu_);
            opened_devices_[request.device_id] = PickResolution(request.width, request.height, request.fps);
        }
        result.opened = true;
        return result;
    }

    SdkDeviceCloseResult CloseDevice(const SdkDeviceCloseRequest& request) override {
        SdkDeviceCloseResult result;
        SdkDeviceOpenRequest get_request;
        get_request.device_id = request.device_id;
        const SdkDeviceOpenResult device_result = GetDevice(get_request);
        if (!IsOkStatusCode(device_result.code)) {
            result.code = device_result.code;
            result.message = device_result.message;
            return result;
        }

        SdkVideoStopRequest stop_request;
        stop_request.device_id = request.device_id;
        StopVideo(stop_request);
        {
            std::lock_guard<std::mutex> lock(mu_);
            result.was_opened = opened_devices_.erase(request.device_id) > 0;
        }
        result.closed = true;
        return result;
    }

    void CaptureStill(const SdkCaptureRequest& request, SdkCaptureCallback callback) override {
        SdkCaptureResult result;
        if (!IsOpened(request.device_id)) {
            result.code = ToCode(SdkStatusCode::DeviceNotOpen);
            result.message = "device not open";
            if (callback) {
                callback(result);
            }
            return;
        }
        const std::string output_dir = request.output_dir.empty() ? JoinPath(GetSdkOpenCaptureDir(), "mock") : request.output_dir;
        if (!EnsureDirectoryRecursive(output_dir)) {
            result.code = ToCode(SdkStatusCode::ProviderCallFailed);
            result.message = "failed to create capture output directory";
            if (callback) {
                callback(result);
            }
            return;
        }
        const std::string output_path = JoinPath(output_dir, "mock-original-" + request.device_id + ".jpg");
        WriteBytes(output_path, TinyJpeg());
        result.captured = true;
        result.content_type = request.include_base64 ? "image/base64" : "image/file";
        result.payload = request.include_base64 ? "c2RrX29wZW5fY2FwdHVyZV9wbGFjZWhvbGRlcg==" : "";
        result.output_path = output_path;
        result.original_path = output_path;
        result.width = 1;
        result.height = 1;
        result.size = TinyJpeg().size();
        if (callback) {
            callback(result);
        }
    }

    SdkVideoStartResult StartVideo(const SdkVideoStartRequest& request, SdkVideoFrameCallback callback) override {
        SdkVideoStartResult result;
        SdkVideoResolution resolution;
        {
            std::lock_guard<std::mutex> lock(mu_);
            const std::map<std::string, SdkVideoResolution>::const_iterator it = opened_devices_.find(request.device_id);
            if (it == opened_devices_.end()) {
                result.code = ToCode(SdkStatusCode::DeviceNotOpen);
                result.message = "device not open";
                return result;
            }
            resolution = PickResolution(request.width > 0 ? request.width : it->second.width,
                                        request.height > 0 ? request.height : it->second.height,
                                        request.fps > 0 ? request.fps : it->second.fps);
            if (streams_.find(request.device_id) != streams_.end()) {
                result.code = ToCode(SdkStatusCode::DeviceBusy);
                result.message = "device already streaming";
                return result;
            }
            std::shared_ptr<MockStreamState> state(new MockStreamState());
            state->running.store(true);
            state->thread = std::thread([request, resolution, callback, state]() {
                while (state->running.load()) {
                    SdkVideoFrame frame;
                    frame.device_id = request.device_id;
                    frame.stream_id = request.stream_id;
                    frame.frame_seq = state->frame_seq.fetch_add(1);
                    frame.timestamp_ms = NowMs();
                    frame.width = resolution.width;
                    frame.height = resolution.height;
                    frame.pixel_format = "bgr24";
                    frame.payload = MakeBgrFrame(resolution.width, resolution.height, frame.frame_seq);
                    if (callback) {
                        callback(frame);
                    }
                    const int fps = resolution.fps > 0 ? resolution.fps : 15;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000 / fps));
                }
            });
            streams_[request.device_id] = state;
        }
        result.accepted = true;
        result.pixel_format = "bgr24";
        result.width = resolution.width;
        result.height = resolution.height;
        result.fps = resolution.fps;
        return result;
    }

    SdkVideoStopResult StopVideo(const SdkVideoStopRequest& request) override {
        SdkVideoStopResult result;
        std::shared_ptr<MockStreamState> state;
        {
            std::lock_guard<std::mutex> lock(mu_);
            const std::map<std::string, std::shared_ptr<MockStreamState>>::iterator it = streams_.find(request.device_id);
            if (it != streams_.end()) {
                state = it->second;
                streams_.erase(it);
            }
        }
        if (state) {
            state->running.store(false);
            if (state->thread.joinable()) {
                state->thread.join();
            }
        }
        result.stopped = true;
        return result;
    }

    SdkVideoFormatResult SetVideoFormat(const SdkVideoFormatRequest&) override {
        SdkVideoFormatResult result;
        result.applied = true;
        return result;
    }

    SdkVideoProfileResult SetVideoProfile(const SdkVideoProfileRequest& request) override {
        SdkVideoProfileResult result;
        std::lock_guard<std::mutex> lock(mu_);
        if (streams_.find(request.device_id) == streams_.end()) {
            result.code = ToCode(SdkStatusCode::StreamNotReady);
            result.message = "video stream not running";
            return result;
        }
        result.applied = true;
        result.page_processing = request.page_processing;
        result.single_page_realtime_detect_rects =
            request.page_processing == "single_page" && request.single_page_realtime_detect_rects;
        result.single_page_multi_target_paging =
            result.single_page_realtime_detect_rects && request.single_page_multi_target_paging;
        return result;
    }

private:
    struct MockStreamState {
        std::atomic<bool> running{false};
        std::atomic<uint64_t> frame_seq{1};
        std::thread thread;
    };

    static int64_t NowMs() {
        return static_cast<int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    }

    static std::vector<uint8_t> TinyJpeg() {
        static const uint8_t kJpeg[] = {
            0xff, 0xd8, 0xff, 0xe0, 0x00, 0x10, 0x4a, 0x46, 0x49, 0x46, 0x00, 0x01,
            0x01, 0x01, 0x00, 0x60, 0x00, 0x60, 0x00, 0x00, 0xff, 0xdb, 0x00, 0x43,
            0x00, 0x08, 0x06, 0x06, 0x07, 0x06, 0x05, 0x08, 0x07, 0x07, 0x07, 0x09,
            0x09, 0x08, 0x0a, 0x0c, 0x14, 0x0d, 0x0c, 0x0b, 0x0b, 0x0c, 0x19, 0x12,
            0x13, 0x0f, 0x14, 0x1d, 0x1a, 0x1f, 0x1e, 0x1d, 0x1a, 0x1c, 0x1c, 0x20,
            0x24, 0x2e, 0x27, 0x20, 0x22, 0x2c, 0x23, 0x1c, 0x1c, 0x28, 0x37, 0x29,
            0x2c, 0x30, 0x31, 0x34, 0x34, 0x34, 0x1f, 0x27, 0x39, 0x3d, 0x38, 0x32,
            0x3c, 0x2e, 0x33, 0x34, 0x32, 0xff, 0xc0, 0x00, 0x11, 0x08, 0x00, 0x01,
            0x00, 0x01, 0x03, 0x01, 0x22, 0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01,
            0xff, 0xc4, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0xff, 0xc4,
            0x00, 0x14, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xda, 0x00, 0x0c,
            0x03, 0x01, 0x00, 0x02, 0x11, 0x03, 0x11, 0x00, 0x3f, 0x00, 0xb2, 0xc0,
            0x07, 0xff, 0xd9,
        };
        return std::vector<uint8_t>(kJpeg, kJpeg + sizeof(kJpeg));
    }

    static std::vector<uint8_t> MakeBgrFrame(int width, int height, uint64_t frame_seq) {
        if (width <= 0 || height <= 0) {
            return std::vector<uint8_t>();
        }
        std::vector<uint8_t> data(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
        const uint8_t phase = static_cast<uint8_t>(frame_seq % 256);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const size_t offset = (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 3;
                data[offset + 0] = static_cast<uint8_t>((x + phase) % 256);
                data[offset + 1] = static_cast<uint8_t>((y + phase) % 256);
                data[offset + 2] = static_cast<uint8_t>((x + y + phase) % 256);
            }
        }
        return data;
    }

    static void WriteBytes(const std::string& path, const std::vector<uint8_t>& bytes) {
        std::ofstream out(path.c_str(), std::ios::binary);
        if (out.good() && !bytes.empty()) {
            out.write(reinterpret_cast<const char*>(&bytes[0]), static_cast<std::streamsize>(bytes.size()));
        }
    }

    static std::vector<SdkVideoResolution> DefaultResolutions() {
        std::vector<SdkVideoResolution> resolutions;
        SdkVideoResolution first;
        SdkVideoResolution capture;
        capture.width = 1536;
        capture.height = 1152;
        capture.real_width = 1536;
        capture.real_height = 1152;
        capture.fps = 15;
        capture.is_default = true;
        resolutions.push_back(capture);
        first.width = 1280;
        first.height = 720;
        first.real_width = 1280;
        first.real_height = 720;
        first.fps = 15;
        resolutions.push_back(first);
        SdkVideoResolution second;
        second.width = 640;
        second.height = 480;
        second.real_width = 640;
        second.real_height = 480;
        second.fps = 15;
        resolutions.push_back(second);
        return resolutions;
    }

    static SdkVideoResolution PickResolution(int width, int height, int fps) {
        std::vector<SdkVideoResolution> resolutions = DefaultResolutions();
        for (std::vector<SdkVideoResolution>::const_iterator it = resolutions.begin(); it != resolutions.end(); ++it) {
            if ((width <= 0 || it->width == width) && (height <= 0 || it->height == height)) {
                SdkVideoResolution resolution = *it;
                if (fps > 0) {
                    resolution.fps = fps;
                }
                return resolution;
            }
        }
        return resolutions.front();
    }

    bool IsOpened(const std::string& device_id) const {
        std::lock_guard<std::mutex> lock(mu_);
        return opened_devices_.find(device_id) != opened_devices_.end();
    }

    void StopAllStreams() {
        std::map<std::string, std::shared_ptr<MockStreamState>> streams;
        {
            std::lock_guard<std::mutex> lock(mu_);
            streams.swap(streams_);
        }
        for (std::map<std::string, std::shared_ptr<MockStreamState>>::iterator it = streams.begin(); it != streams.end(); ++it) {
            it->second->running.store(false);
            if (it->second->thread.joinable()) {
                it->second->thread.join();
            }
        }
    }

    mutable std::mutex mu_;
    std::map<std::string, SdkVideoResolution> opened_devices_;
    std::map<std::string, std::shared_ptr<MockStreamState>> streams_;
};

class MockGraphicProvider : public ISdkGraphicProvider {
public:
    std::string ProviderName() const override { return "mock-graphic-provider"; }
    SdkImageProcessResult Process(const SdkImageProcessRequest& request) override {
        SdkImageProcessResult result;
        CopyFile(request.input_path, request.output_path);
        result.processed = true;
        result.output_path = request.output_path;
        SdkImageProcessOutput output;
        output.output_id = "page-001";
        output.role = "page";
        output.index = 0;
        output.path = request.output_path;
        output.content_type = request.output_format == "png" ? "image/png" : "image/jpeg";
        result.outputs.push_back(output);
        return result;
    }

    SdkPageProcessResult ProcessPage(const SdkPageProcessRequest& request) override {
        SdkPageProcessResult result;
        CopyFile(request.input_path, request.output_path);
        result.processed = true;
        result.output_path = request.output_path;
        SdkPageOutput output;
        output.output_id = "page-001";
        output.role = "page";
        output.index = 0;
        output.path = request.output_path;
        output.content_type = "image/jpeg";
        output.width = request.width;
        output.height = request.height;
        result.outputs.push_back(output);
        return result;
    }

    SdkColorModeResult ApplyColorMode(const SdkColorModeRequest& request) override {
        SdkColorModeResult result;
        CopyFile(request.input_path, request.output_path);
        result.processed = true;
        result.output_path = request.output_path;
        return result;
    }

    SdkFormatConvertResult ConvertImageFormat(const SdkFormatConvertRequest& request) override {
        SdkFormatConvertResult result;
        CopyFile(request.input_path, request.output_path);
        result.converted = true;
        result.output_path = request.output_path;
        return result;
    }

    SdkThumbnailResult GenerateThumbnail(const SdkThumbnailRequest& request) override {
        SdkThumbnailResult result;
        CopyFile(request.input_path, request.output_path);
        result.generated = true;
        result.output_path = request.output_path;
        result.width = 1;
        result.height = 1;
        return result;
    }

private:
    static void CopyFile(const std::string& input_path, const std::string& output_path) {
        std::ifstream in(input_path.c_str(), std::ios::binary);
        std::ofstream out(output_path.c_str(), std::ios::binary);
        out << in.rdbuf();
    }
};

class MockOcrProvider : public ISdkOcrProvider {
public:
    std::string ProviderName() const override { return "mock-ocr-provider"; }
    SdkOcrRecognizeResult Recognize(const SdkOcrRecognizeRequest&) override {
        SdkOcrRecognizeResult result;
        result.task_id = "mock-ocr-task-1";
        return result;
    }
};

class MockOfdProvider : public ISdkOfdProvider {
public:
    std::string ProviderName() const override { return "mock-ofd-provider"; }
    SdkFileConvertResult Convert(const SdkFileConvertRequest& request) override {
        SdkFileConvertResult result;
        result.accepted = true;
        result.output_path = request.output_path;
        return result;
    }
};

} // namespace

ProviderBundle CreateProviderBundle() {
    ProviderBundle bundle;
    bundle.auth_provider = std::make_shared<MockAuthProvider>();
    bundle.device_provider = std::make_shared<MockDeviceProvider>();
    bundle.graphic_provider = std::make_shared<MockGraphicProvider>();
    bundle.ocr_provider = std::make_shared<MockOcrProvider>();
    bundle.ofd_provider = std::make_shared<MockOfdProvider>();
    return bundle;
}

} // namespace mock
} // namespace sdk
} // namespace editor
