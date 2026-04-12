// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "mock_provider_factory.h"

#include <memory>
#include <utility>

#include "sdk_logger.h"
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
        result.auth_context.license_mode = "offline_token";
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
            "auth.destroy_session",
            "device.list",
            "device.get",
            "device.open",
            "capture.take",
            "video.start",
            "video.stop",
            "video.set_format",
            "image.process",
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
};

class MockDeviceProvider : public ISdkDeviceProvider {
public:
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
        devices.push_back(second);
        return devices;
    }

    SdkDeviceOpenResult OpenDevice(const SdkDeviceOpenRequest& request) override {
        SdkDeviceOpenResult result;
        result.opened = !request.device_id.empty();
        result.device.device_id = request.device_id;
        return result;
    }

    SdkCaptureResult CaptureStill(const SdkCaptureRequest& request) override {
        SdkCaptureResult result;
        result.captured = true;
        result.content_type = request.include_base64 ? "image/base64" : "image/file";
        result.payload = request.include_base64 ? "c2RrX29wZW5fY2FwdHVyZV9wbGFjZWhvbGRlcg==" : "";
        result.output_path = request.include_base64 ? "" : "/tmp/mock-capture-" + request.device_id + ".jpg";
        return result;
    }

    SdkVideoStartResult StartVideo(const SdkVideoStartRequest&) override {
        SdkVideoStartResult result;
        result.accepted = true;
        result.pixel_format = "jpeg";
        result.width = 1280;
        result.height = 720;
        result.fps = 15;
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
};

class MockGraphicProvider : public ISdkGraphicProvider {
public:
    std::string ProviderName() const override { return "mock-graphic-provider"; }
    SdkImageProcessResult Process(const SdkImageProcessRequest&) override {
        SdkImageProcessResult result;
        result.processed = true;
        return result;
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
    SdkFileConvertResult Convert(const SdkFileConvertRequest&) override {
        SdkFileConvertResult result;
        result.accepted = true;
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
