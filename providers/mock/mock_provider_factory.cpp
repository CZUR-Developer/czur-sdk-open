// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "mock_provider_factory.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
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

std::string BaseNameWithoutExtension(const std::string& path) {
    const std::string::size_type slash_pos = path.find_last_of("/\\");
    const std::string leaf = slash_pos == std::string::npos ? path : path.substr(slash_pos + 1);
    const std::string::size_type dot_pos = leaf.find_last_of('.');
    if (dot_pos == std::string::npos || dot_pos == 0) {
        return leaf.empty() ? "output" : leaf;
    }
    return leaf.substr(0, dot_pos);
}

std::string ExtensionForOcrFormat(const std::string& format) {
    return format.empty() ? "docx" : format;
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
            "image.enhance_capabilities",
            "image.enhance",
            "image.enhance_get",
            "image.enhance_cancel",
            "image.enhance_workflow_list",
            "image.enhance_workflow_get",
            "image.enhance_workflow_save",
            "image.enhance_workflow_delete",
            "ocr.recognize",
            "ocr.get",
            "ocr.cancel",
            "ocr.extract_text",
            "recognition.barcode_detect",
            "file.convert",
            "sane.status",
            "sane.list",
            "sane.watch_start",
            "sane.watch_stop",
            "sane.open",
            "sane.close",
            "sane.get_options",
            "sane.set_options",
            "sane.profile_list",
            "sane.profile_save",
            "sane.profile_apply",
            "sane.profile_delete",
            "sane.scan",
            "sane.scan_get",
            "sane.scan_cancel",
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

class MockImageEnhanceProvider : public ISdkImageEnhanceProvider {
public:
    std::string ProviderName() const override { return "mock-image-enhance-provider"; }

    SdkImageEnhanceCapabilityResult ListCapabilities() override {
        SdkImageEnhanceCapabilityResult result;
        result.provider = ProviderName();
        result.kind = "mixed";
        result.available = true;
        result.capabilities.push_back(MakeCapability("crop_enhance", "Crop enhancement", "cleanup", "offline", "transform", R"({"mode":"crop","top_percent":0,"bottom_percent":0,"left_percent":0,"right_percent":0})"));
        result.capabilities.push_back(MakeCapability("normalize_spec", "Normalize specification", "normalize", "offline", "transform", R"json({"auto":true,"format":"A4-auto","width":210,"height":297,"dpi":300,"color":"rgb(255, 255, 255)","horizontal":1,"vertical":1,"fill":1,"fillValue":1.0,"useOriginalPaperScale":false,"originalWidth":0,"originalHeight":0,"quality":95})json"));
        result.capabilities.push_back(MakeCapability("rotate", "Rotate", "normalize", "offline", "transform", R"({"mode":"auto","angle":0,"deskew":true,"max_angle":20,"border_mode":1,"border_val":0,"dl_flag":0})"));
        result.capabilities.push_back(MakeCapability("blank_page_detect", "Blank page detection", "detect", "offline", "filter", R"({"action":"drop","threshold":0.98})"));
        result.capabilities.push_back(MakeCapability("red_green_head", "Red/green head enhancement", "enhance", "offline", "transform", R"({"auto_flag":true,"rois":[],"stroke_width":0,"blur_level":0,"color_thresh":50,"texture_degree":1.2,"specify_color":false,"src_color":[0,0,0],"target_color":[67,67,222]})"));
        SdkImageEnhanceCapability online = MakeCapability("document_rectify_enhance", "Document rectification enhancement", "enhance", "online", "transform", "{}");
        online.available = false;
        online.unavailable_reason = "mock online provider is not configured";
        online.unavailable_reason_zh_cn = "在线增强服务未配置";
        result.capabilities.push_back(online);
        online.type = "remove_handwriting";
        online.title = "Remove handwriting";
        online.i18n_key = "image_enhance.remove_handwriting";
        FillLocalizedCapability(&online);
        result.capabilities.push_back(online);
        online.type = "remove_background_texture";
        online.title = "Remove background texture";
        online.i18n_key = "image_enhance.remove_background_texture";
        FillLocalizedCapability(&online);
        result.capabilities.push_back(online);
        return result;
    }

    SdkImageEnhanceStepResult RunStep(const SdkImageEnhanceStepRequest& request) override {
        if (request.step.type == "document_rectify_enhance" ||
            request.step.type == "remove_handwriting" ||
            request.step.type == "remove_background_texture") {
            SdkImageEnhanceStepResult result;
            result.code = ToCode(SdkStatusCode::UnsupportedMethod);
            result.message = "mock online image enhance provider is not configured";
            return result;
        }
        SdkImageEnhanceStepResult result;
        result.processed = true;
        int index = 1;
        for (std::vector<SdkImageEnhancePage>::const_iterator it = request.pages.begin(); it != request.pages.end(); ++it) {
            SdkImageEnhancePage page = *it;
            page.output_index = index;
            page.path = JoinEnhancePath(request.output_dir, request.step.type, index, it->path);
            page.metadata_json = "{\"mock\":true}";
            EnsureDirectory(request.output_dir);
            CopyFile(it->path, page.path);
            result.pages.push_back(page);
            ++index;
        }
        result.metadata_json = "{\"mock\":true}";
        return result;
    }

private:
    static void FillLocalizedCapability(SdkImageEnhanceCapability* capability) {
        if (capability == NULL) {
            return;
        }
        if (capability->type == "crop_enhance") {
            capability->description = "Crops pages by top, bottom, left, and right percentages, or keeps the original canvas and fills the outside area with white.";
            capability->title_zh_cn = "裁剪增强";
            capability->description_zh_cn = "按上下左右百分比裁剪页面，支持直接裁剪或裁剪区域外留白。";
        } else if (capability->type == "normalize_spec") {
            capability->description = "Standardizes pages to a unified paper size, DPI, background, alignment, and fill rule for consistent export.";
            capability->title_zh_cn = "统一规格";
            capability->description_zh_cn = "将页面按统一宽高、DPI 和适配方式处理，便于批量导出规格一致。";
        } else if (capability->type == "rotate") {
            capability->description = "Rotates pages manually by any angle, or automatically rotates and deskews pages by text direction.";
            capability->title_zh_cn = "旋转";
            capability->description_zh_cn = "支持任意手动角度旋转，也可按文字方向自动转正并纠偏。";
        } else if (capability->type == "blank_page_detect") {
            capability->description = "Detects blank pages and can mark or remove them from the output sequence.";
            capability->title_zh_cn = "空白页检测";
            capability->description_zh_cn = "检测空白页，可选择标记空白页或从输出序列中移除。";
        } else if (capability->type == "red_green_head") {
            capability->description = "Enhances red or green header documents with the same offline algorithm used by the desktop client.";
            capability->title_zh_cn = "红绿头增强";
            capability->description_zh_cn = "使用客户端同款离线算法增强红头、绿头文件。";
        } else if (capability->type == "document_rectify_enhance") {
            capability->description = "Online service for document perspective correction, cleanup, and visual enhancement.";
            capability->title_zh_cn = "文档矫正增强";
            capability->description_zh_cn = "在线服务能力，用于文档透视矫正、清理和视觉增强。";
        } else if (capability->type == "remove_handwriting") {
            capability->description = "Online service for removing handwriting traces from document images.";
            capability->title_zh_cn = "文档去手写";
            capability->description_zh_cn = "在线服务能力，用于去除文档图片中的手写痕迹。";
        } else if (capability->type == "remove_background_texture") {
            capability->description = "Online service for reducing paper texture, watermark-like background, and patterned noise.";
            capability->title_zh_cn = "文档图片去底纹";
            capability->description_zh_cn = "在线服务能力，用于减弱纸张底纹、水印类背景和纹理噪声。";
        }
        if (!capability->unavailable_reason.empty()) {
            capability->unavailable_reason_zh_cn = "在线增强服务未配置";
        }
    }

    static SdkImageEnhanceCapability MakeCapability(const std::string& type,
                                                    const std::string& title,
                                                    const std::string& category,
                                                    const std::string& runtime,
                                                    const std::string& page_effect,
                                                    const std::string& defaults_json) {
        SdkImageEnhanceCapability capability;
        capability.type = type;
        capability.title = title;
        capability.description = title;
        capability.i18n_key = "image_enhance." + type;
        capability.category = category;
        capability.runtime = runtime;
        capability.page_effect = page_effect;
        capability.metadata = page_effect != "transform";
        capability.requires_capability = runtime == "online" ? "image.enhance.online" : "image.enhance";
        capability.source_types.push_back("image");
        capability.source_types.push_back("images");
        capability.defaults_json = defaults_json;
        capability.schema_json = "{}";
        FillLocalizedCapability(&capability);
        return capability;
    }

    static std::string ExtensionFromPath(const std::string& path) {
        const std::string::size_type pos = path.find_last_of('.');
        if (pos == std::string::npos || pos + 1 >= path.size()) {
            return "jpg";
        }
        return path.substr(pos + 1);
    }

    static std::string JoinEnhancePath(const std::string& dir, const std::string& step, int index, const std::string& input_path) {
        std::ostringstream name;
        name << step << "-" << index << "." << ExtensionFromPath(input_path);
        if (dir.empty() || dir == ".") {
            return name.str();
        }
        return dir[dir.size() - 1] == '/' ? dir + name.str() : dir + "/" + name.str();
    }

    static void EnsureDirectory(const std::string&) {}
};

class MockOcrProvider : public ISdkOcrProvider {
public:
    std::string ProviderName() const override { return "mock-ocr-provider"; }
    SdkOcrRecognizeResult Recognize(const SdkOcrRecognizeRequest& request) override {
        SdkOcrRecognizeResult result;
        result.task_id = "mock-ocr-task-" + std::to_string(next_task_id_++);
        result.task.task_id = result.task_id;
        result.task.status = "completed";
        result.task.progress = 100;
        result.task.output_path = request.output_path;
        result.task.export_type = request.export_type;
        result.task.format = request.format;
        result.task.message = "mock ocr completed";
        if (request.export_type == "single-page") {
            const std::string output_dir = request.output_dir.empty() ? request.output_path : request.output_dir;
            for (std::vector<std::string>::const_iterator it = request.input_files.begin(); it != request.input_files.end(); ++it) {
                result.task.output_paths.push_back(JoinPath(output_dir, BaseNameWithoutExtension(*it) + "." + ExtensionForOcrFormat(request.format)));
            }
            result.task.output_path = output_dir;
        } else {
            result.task.output_paths.push_back(request.output_path);
        }
        {
            std::lock_guard<std::mutex> lock(mu_);
            tasks_[result.task_id] = result.task;
        }
        return result;
    }
    SdkOcrGetResult GetTask(const SdkOcrGetRequest& request) override {
        SdkOcrGetResult result;
        std::lock_guard<std::mutex> lock(mu_);
        const std::map<std::string, SdkOcrTaskSnapshot>::const_iterator it = tasks_.find(request.task_id);
        if (it == tasks_.end()) {
            result.code = ToCode(SdkStatusCode::InvalidParams);
            result.message = "ocr task not found";
            return result;
        }
        result.task = it->second;
        return result;
    }
    SdkOcrCancelResult Cancel(const SdkOcrCancelRequest& request) override {
        SdkOcrCancelResult result;
        std::lock_guard<std::mutex> lock(mu_);
        std::map<std::string, SdkOcrTaskSnapshot>::iterator it = tasks_.find(request.task_id);
        if (it == tasks_.end()) {
            result.code = ToCode(SdkStatusCode::InvalidParams);
            result.message = "ocr task not found";
            return result;
        }
        it->second.status = "cancelled";
        it->second.message = "mock ocr cancelled";
        result.cancelled = true;
        result.task = it->second;
        return result;
    }
    SdkOcrExtractTextResult ExtractText(const SdkOcrExtractTextRequest& request) override {
        SdkOcrExtractTextResult result;
        result.recognized = true;
        result.input_path = request.input_path;
        result.width = 1200;
        result.height = 800;
        SdkOcrTextBlock block;
        block.text = "SDK OCR demo text";
        block.x = 80.0f;
        block.y = 96.0f;
        block.width = 420.0f;
        block.height = 48.0f;
        block.confidence = 0.98f;
        block.font_size = 18.0f;
        result.blocks.push_back(block);
        return result;
    }

private:
    std::mutex mu_;
    std::map<std::string, SdkOcrTaskSnapshot> tasks_;
    int next_task_id_ = 1;
};

class MockRecognitionProvider : public ISdkRecognitionProvider {
public:
    std::string ProviderName() const override { return "mock-recognition-provider"; }
    SdkBarcodeDetectResult DetectBarcode(const SdkBarcodeDetectRequest& request) override {
        SdkBarcodeDetectResult result;
        result.detected = true;
        result.input_path = request.input_path;
        result.width = 1200;
        result.height = 800;
        SdkBarcodeResult barcode;
        barcode.format_name = "QR_CODE";
        barcode.text = "https://www.czur.com";
        SdkPoint2f point;
        point.x = 120.0f;
        point.y = 120.0f;
        barcode.points.push_back(point);
        point.x = 280.0f;
        point.y = 120.0f;
        barcode.points.push_back(point);
        point.x = 280.0f;
        point.y = 280.0f;
        barcode.points.push_back(point);
        point.x = 120.0f;
        point.y = 280.0f;
        barcode.points.push_back(point);
        result.barcodes.push_back(barcode);
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
        if (request.export_type == "single-page") {
            const std::string output_dir = request.output_dir.empty() ? "." : request.output_dir;
            const std::string extension = request.output_format.empty() ? "pdf" : request.output_format;
            for (std::vector<std::string>::const_iterator it = request.input_paths.begin(); it != request.input_paths.end(); ++it) {
                result.output_paths.push_back(JoinPath(output_dir, BaseNameWithoutExtension(*it) + "." + extension));
            }
            if (!result.output_paths.empty()) {
                result.output_path = result.output_paths.front();
            }
        } else if (!request.output_path.empty()) {
            result.output_paths.push_back(request.output_path);
        }
        return result;
    }
};

class MockSaneProvider : public ISdkSaneProvider {
public:
    std::string ProviderName() const override { return "mock-sane-provider"; }

    void SetDeviceEventSink(SdkSaneDeviceEventCallback sink) override {
        event_sink_ = std::move(sink);
    }

    void SetScanTaskEventSink(SdkSaneScanTaskEventCallback sink) override {
        scan_task_event_sink_ = std::move(sink);
    }

    SdkSaneStatusResult GetStatus() override {
        SdkSaneStatusResult result;
        result.available = true;
        result.platform = "mock";
        result.supported_platforms.push_back("linux");
        result.sane_major = 1;
        result.sane_minor = 2;
        result.sane_version = "1.2 mock";
        result.reason = "mock provider; real SANE is Linux-only";
        return result;
    }

    SdkSaneListResult ListDevices(const SdkSaneListRequest& request) override {
        SdkSaneListResult result;
        result.generation = generation_;
        result.devices.push_back(DefaultDevice());
        if (request.include_detected) {
            result.detected_devices.push_back(DefaultDetectedDevice());
        }
        return result;
    }

    SdkSaneWatchResult WatchStart(const SdkSaneWatchRequest& request) override {
        SdkSaneWatchResult result;
        result.watching = true;
        result.generation = generation_;
        if (event_sink_ && !request.connection_id.empty()) {
            SdkSaneDeviceEvent event;
            event.connection_id = request.connection_id;
            event.event_name = "sane.device_snapshot";
            event.generation = generation_;
            event.devices.push_back(DefaultDevice());
            event.detected_devices.push_back(DefaultDetectedDevice());
            event.added_devices.push_back(DefaultDevice());
            event_sink_(event);
        }
        return result;
    }

    SdkSaneWatchResult WatchStop(const SdkSaneWatchRequest&) override {
        SdkSaneWatchResult result;
        result.watching = false;
        result.generation = generation_;
        return result;
    }

    SdkSaneOpenResult OpenDevice(const SdkSaneOpenRequest& request) override {
        SdkSaneOpenResult result;
        result.opened = true;
        result.session_id = "sane-session-mock";
        result.device = DefaultDevice();
        if (!request.device_id.empty()) {
            result.device.device_id = request.device_id;
        }
        return result;
    }

    SdkSaneCloseResult CloseDevice(const SdkSaneCloseRequest&) override {
        SdkSaneCloseResult result;
        result.closed = true;
        result.was_opened = true;
        return result;
    }

    SdkSaneGetOptionsResult GetOptions(const SdkSaneGetOptionsRequest&) override {
        SdkSaneGetOptionsResult result;
        result.options.push_back(MakeStringOption(1, "mode", "Scan Mode", "General", "\"Color\"", {"\"Color\"", "\"Gray\"", "\"Lineart\""}));
        result.options.push_back(MakeNumberOption(2, "resolution", "Scan Resolution", "General", "300", 75, 600, 75));
        result.options.push_back(MakeStringOption(3, "source", "Scan Source", "General", "\"Flatbed\"", {"\"Flatbed\"", "\"ADF Front\"", "\"ADF Duplex\""}));
        result.options.push_back(MakeNumberOption(4, "brightness", "Brightness", "Image", "0", -100, 100, 1));
        return result;
    }

    SdkSaneSetOptionsResult SetOptions(const SdkSaneSetOptionsRequest& request) override {
        SdkSaneSetOptionsResult result;
        result.applied = true;
        for (std::vector<SdkSaneOptionSetItem>::const_iterator it = request.options.begin(); it != request.options.end(); ++it) {
            SdkSaneOptionSetResultItem item;
            item.key = it->key;
            item.index = it->index;
            item.status = "applied";
            item.message = "ok";
            item.value_json = it->value_json;
            result.results.push_back(item);
        }
        return result;
    }

    SdkSaneProfileListResult ListProfiles(const SdkSaneProfileRequest&) override {
        std::lock_guard<std::mutex> lock(mu_);
        SdkSaneProfileListResult result;
        for (std::map<std::string, SdkSaneProfile>::const_iterator it = profiles_.begin(); it != profiles_.end(); ++it) {
            result.profiles.push_back(it->second);
        }
        return result;
    }

    SdkSaneProfileResult SaveProfile(const SdkSaneProfileRequest& request) override {
        std::lock_guard<std::mutex> lock(mu_);
        SdkSaneProfileResult result;
        result.saved = true;
        SdkSaneProfile profile;
        profile.profile_id = request.profile_id.empty() ? "sane-profile-" + std::to_string(next_profile_id_++) : request.profile_id;
        profile.device_key = request.device_key.empty() ? "mock:sane0" : request.device_key;
        profile.name = request.name;
        profile.options = request.options;
        profile.created_at = "mock";
        profile.updated_at = "mock";
        profiles_[profile.profile_id] = profile;
        result.profile = profile;
        return result;
    }

    SdkSaneProfileResult ApplyProfile(const SdkSaneProfileRequest& request) override {
        std::lock_guard<std::mutex> lock(mu_);
        SdkSaneProfileResult result;
        std::map<std::string, SdkSaneProfile>::const_iterator it = profiles_.find(request.profile_id);
        if (it == profiles_.end()) {
            result.code = ToCode(SdkStatusCode::SaneProfileNotFound);
            result.message = "SANE profile not found";
            return result;
        }
        result.applied = true;
        result.profile = it->second;
        return result;
    }

    SdkSaneProfileResult DeleteProfile(const SdkSaneProfileRequest& request) override {
        std::lock_guard<std::mutex> lock(mu_);
        SdkSaneProfileResult result;
        std::map<std::string, SdkSaneProfile>::iterator it = profiles_.find(request.profile_id);
        if (it == profiles_.end()) {
            result.code = ToCode(SdkStatusCode::SaneProfileNotFound);
            result.message = "SANE profile not found";
            return result;
        }
        result.deleted = true;
        result.profile = it->second;
        profiles_.erase(it);
        return result;
    }

    SdkSaneScanResult Scan(const SdkSaneScanRequest& request) override {
        SdkSaneScanResult result;
        {
            std::lock_guard<std::mutex> lock(mu_);
            result.task_id = "sane-scan-" + std::to_string(next_task_id_++);
            result.accepted = true;
            result.task.task_id = result.task_id;
            result.task.connection_id = request.connection_id;
            result.task.session_id = request.session_id;
            result.task.status = "queued";
            result.task.phase = "queued";
            result.task.progress = 0;
            result.task.output_type = request.output_type.empty() ? "images" : request.output_type;
            result.task.output_format = request.output_format.empty() ? "jpg" : request.output_format;
            result.task.output_dir = request.output_dir.empty() ? "/tmp" : request.output_dir;
            result.task.export_type = request.export_type.empty() ? "multi-page" : request.export_type;
            result.task.output_path = request.output_path;
            result.task.message = "mock scan queued";
            tasks_[result.task_id] = result.task;
        }
        EmitScanTask(result.task);
        const std::string task_id = result.task_id;
        std::thread([this, request, task_id]() {
            SdkSaneScanTask task;
            {
                std::lock_guard<std::mutex> lock(mu_);
                task = tasks_[task_id];
                task.status = "running";
                task.phase = "scanning";
                task.message = "mock scan running";
                tasks_[task_id] = task;
            }
            EmitScanTask(task);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            {
                std::lock_guard<std::mutex> lock(mu_);
                task = tasks_[task_id];
                if (task.cancel_requested) {
                    task.status = "cancelled";
                    task.phase = "cancelled";
                    task.progress = 100;
                    task.message = "cancelled";
                    tasks_[task_id] = task;
                    EmitScanTask(task);
                    return;
                }
                const int page_count = 3;
                for (int page = 1; page <= page_count; ++page) {
                    std::ostringstream path;
                    if (!request.output_path.empty() && task.output_type == "images" && page == 1) {
                        path << request.output_path;
                    } else if (!request.output_path.empty() && task.output_type == "images") {
                        path << request.output_path << "_page" << page;
                    } else {
                        path << "/tmp/czur-sdk-mock-sane-page-" << page << ".jpg";
                    }
                    task.output_paths.push_back(path.str());
                    task.last_page_path = path.str();
                    if (task.output_type == "images" && task.output_path.empty()) {
                        task.output_path = path.str();
                    }
                }
                task.status = "completed";
                task.phase = "completed";
                task.progress = 100;
                task.page_count = page_count;
                task.current_page = page_count;
                task.message = "mock scan completed";
                tasks_[task_id] = task;
            }
            EmitScanTask(task);
        }).detach();
        return result;
    }

    SdkSaneScanResult GetScan(const SdkSaneScanGetRequest& request) override {
        std::lock_guard<std::mutex> lock(mu_);
        SdkSaneScanResult result;
        std::map<std::string, SdkSaneScanTask>::const_iterator it = tasks_.find(request.task_id);
        if (it == tasks_.end()) {
            result.code = ToCode(SdkStatusCode::InvalidParams);
            result.message = "SANE scan task not found";
            return result;
        }
        result.task_id = request.task_id;
        result.task = it->second;
        result.accepted = true;
        return result;
    }

    SdkSaneScanResult CancelScan(const SdkSaneScanCancelRequest& request) override {
        std::lock_guard<std::mutex> lock(mu_);
        SdkSaneScanResult result;
        result.task_id = request.task_id;
        std::map<std::string, SdkSaneScanTask>::iterator it = tasks_.find(request.task_id);
        if (it == tasks_.end()) {
            result.code = ToCode(SdkStatusCode::InvalidParams);
            result.message = "SANE scan task not found";
            return result;
        }
        it->second.cancel_requested = true;
        it->second.message = "cancel requested";
        result.task = it->second;
        result.accepted = true;
        EmitScanTask(result.task);
        return result;
    }

private:
    void EmitScanTask(const SdkSaneScanTask& task) {
        SdkSaneScanTaskEventCallback sink = scan_task_event_sink_;
        if (!sink || task.connection_id.empty()) {
            return;
        }
        SdkSaneScanTaskEvent event;
        event.connection_id = task.connection_id;
        event.event_name = "sane.scan_changed";
        event.task = task;
        event.message = task.message;
        sink(event);
    }

    SdkSaneDevice DefaultDevice() const {
        SdkSaneDevice device;
        device.device_id = "sane-mock-0";
        device.device_name = "test:0";
        device.vendor = "CZUR";
        device.model = "SANE Mock Scanner";
        device.type = "flatbed";
        device.backend = "test";
        device.status = "online";
        device.discovery_source = "mock";
        device.openable = true;
        return device;
    }

    SdkSaneDevice DefaultDetectedDevice() const {
        SdkSaneDevice device;
        device.device_id = "sane-finder-mock-0";
        device.device_name = "USB Scanner";
        device.vendor = "CZUR";
        device.model = "SANE Mock Scanner";
        device.type = "Document Scanner";
        device.backend = "finder";
        device.status = "detected";
        device.discovery_source = "finder";
        device.openable = false;
        return device;
    }

    SdkSaneOption MakeStringOption(int index,
                                   const std::string& name,
                                   const std::string& title,
                                   const std::string& group,
                                   const std::string& value_json,
                                   const std::vector<std::string>& values_json) const {
        SdkSaneOption option;
        option.index = index;
        option.name = name;
        option.title = title;
        option.group = group;
        option.type = "string";
        option.value_json = value_json;
        option.constraint.type = "string_list";
        option.constraint.values_json = values_json;
        return option;
    }

    SdkSaneOption MakeNumberOption(int index,
                                   const std::string& name,
                                   const std::string& title,
                                   const std::string& group,
                                   const std::string& value_json,
                                   double min,
                                   double max,
                                   double quant) const {
        SdkSaneOption option;
        option.index = index;
        option.name = name;
        option.title = title;
        option.group = group;
        option.type = "int";
        option.value_json = value_json;
        option.constraint.type = "range";
        option.constraint.min = min;
        option.constraint.max = max;
        option.constraint.quant = quant;
        return option;
    }

    int generation_ = 1;
    SdkSaneDeviceEventCallback event_sink_;
    SdkSaneScanTaskEventCallback scan_task_event_sink_;
    std::mutex mu_;
    std::map<std::string, SdkSaneProfile> profiles_;
    std::map<std::string, SdkSaneScanTask> tasks_;
    int next_profile_id_ = 1;
    int next_task_id_ = 1;
};

} // namespace

ProviderBundle CreateProviderBundle() {
    ProviderBundle bundle;
    bundle.auth_provider = std::make_shared<MockAuthProvider>();
    bundle.device_provider = std::make_shared<MockDeviceProvider>();
    bundle.graphic_provider = std::make_shared<MockGraphicProvider>();
    bundle.image_enhance_provider = std::make_shared<MockImageEnhanceProvider>();
    bundle.ocr_provider = std::make_shared<MockOcrProvider>();
    bundle.ofd_provider = std::make_shared<MockOfdProvider>();
    bundle.recognition_provider = std::make_shared<MockRecognitionProvider>();
    bundle.sane_provider = std::make_shared<MockSaneProvider>();
    return bundle;
}

} // namespace mock
} // namespace sdk
} // namespace editor
