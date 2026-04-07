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

std::string MaskApiKey(const std::string& api_key) {
    if (api_key.empty()) {
        return "<empty>";
    }
    if (api_key.size() <= 16) {
        return api_key.substr(0, api_key.size() / 2) + "...(len=" + std::to_string(api_key.size()) + ")";
    }
    return api_key.substr(0, 8) + "..." + api_key.substr(api_key.size() - 6) +
           " (len=" + std::to_string(api_key.size()) + ")";
}

class MockAuthProvider : public ISdkAuthProvider {
public:
    std::string ProviderName() const override { return "mock-auth-provider"; }

    AuthValidateResult ValidateApiKey(const AuthValidateRequest& request) override {
        AuthValidateResult result;
        SDK_OPEN_LOG_INFO("[mock_auth_provider] ValidateApiKey begin, api_key={}, now_ts={}",
                          MaskApiKey(request.api_key),
                          request.now_ts);
        if (request.api_key.empty()) {
            result.code = ToCode(SdkStatusCode::AuthRequired);
            result.message = "api key required";
            SDK_OPEN_LOG_WARN("[mock_auth_provider] ValidateApiKey reject, code={}, message={}",
                              result.code,
                              result.message);
            return result;
        }
        if (request.api_key != "demo-key-42F8" && request.api_key != "mock-key") {
            result.code = ToCode(SdkStatusCode::ApiKeyInvalid);
            result.message = "api key invalid";
            SDK_OPEN_LOG_WARN("[mock_auth_provider] ValidateApiKey reject, code={}, message={}, note=mock provider only accepts demo-key-42F8/mock-key",
                              result.code,
                              result.message);
            return result;
        }
        result.auth_context.is_valid = true;
        result.auth_context.account_type = SdkAccountType::SvipPlus;
        result.auth_context.account_type_code = AccountTypeCode(SdkAccountType::SvipPlus);
        result.auth_context.auth_scene = "plugin";
        result.auth_context.license_mode = "offline_api_key";
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
            "auth.validate",
            "auth.refresh",
            "auth.get_context",
            "device.list",
            "device.get",
            "device.open",
            "capture.take",
            "image.process",
            "ocr.recognize",
            "file.convert",
        };
        SDK_OPEN_LOG_INFO("[mock_auth_provider] ValidateApiKey success, account_type={}, auth_scene={}, device_scope_count={}",
                          ToAccountTypeString(result.auth_context.account_type),
                          result.auth_context.auth_scene,
                          result.auth_context.device_scope.size());
        return result;
    }

    AuthRefreshResult RefreshSession(const AuthRefreshRequest& request) override {
        AuthRefreshResult result;
        AuthValidateRequest validate_request;
        validate_request.api_key = request.api_key;
        validate_request.now_ts = request.now_ts;
        AuthValidateResult validate = ValidateApiKey(validate_request);
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
        validate_request.api_key = "mock-key";
        validate_request.now_ts = request.now_ts;
        AuthValidateResult validate = ValidateApiKey(validate_request);
        result.auth_context = validate.auth_context;
        return result;
    }

    AuthContextResult GetAuthContext(const AuthLookupRequest& request) override {
        AuthContextResult result;
        if (!request.session_token.empty()) {
            SessionValidateRequest session_request;
            session_request.session_token = request.session_token;
            session_request.now_ts = request.now_ts;
            SessionValidateResult session = ValidateSession(session_request);
            if (IsOkStatusCode(session.code)) {
                result.via_session = true;
                result.auth_context = session.auth_context;
                return result;
            }
        }
        if (!request.api_key.empty()) {
            AuthValidateRequest validate_request;
            validate_request.api_key = request.api_key;
            validate_request.now_ts = request.now_ts;
            AuthValidateResult validate = ValidateApiKey(validate_request);
            result.code = validate.code;
            result.message = validate.message;
            result.via_api_key = IsOkStatusCode(validate.code);
            result.auth_context = validate.auth_context;
            return result;
        }
        result.code = ToCode(SdkStatusCode::AuthRequired);
        result.message = "auth required";
        return result;
    }
};

class MockDeviceProvider : public ISdkDeviceProvider {
public:
    std::string ProviderName() const override { return "mock-device-provider"; }
    std::vector<std::string> ListDevices() const override { return {"mock-device-01"}; }
};

class MockGraphicProvider : public ISdkGraphicProvider {
public:
    std::string ProviderName() const override { return "mock-graphic-provider"; }
    bool ProcessSampleTask(const std::string&, const std::string&) override { return true; }
};

class MockOcrProvider : public ISdkOcrProvider {
public:
    std::string ProviderName() const override { return "mock-ocr-provider"; }
    std::string SubmitTask(const std::vector<std::string>&, const std::string&) override { return "mock-ocr-task-1"; }
};

class MockOfdProvider : public ISdkOfdProvider {
public:
    std::string ProviderName() const override { return "mock-ofd-provider"; }
    bool OpenDocument(const std::string&) override { return true; }
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
