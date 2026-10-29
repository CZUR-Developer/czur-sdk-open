// Copyright (c) 2026 CZUR Tech. All rights reserved.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

#include "application/command_application_service.h"
#include "mock_provider_factory.h"
#include "runtime/sdk_config.h"

namespace {

std::string TempWorkDir() {
    return "/tmp/czur-sdk-open-upload-smoke-" + std::to_string(::getpid());
}

int Expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << message << std::endl;
        return 1;
    }
    return 0;
}

int ExpectUploadResult(const editor::sdk::CommandApplicationService::ImageUploadResult& result,
                       int expected_code,
                       const std::string& expected_message) {
    if (result.code != expected_code) {
        std::cerr << "unexpected code: got " << result.code << ", expected " << expected_code << std::endl;
        return 1;
    }
    if (result.message != expected_message) {
        std::cerr << "unexpected message: got '" << result.message << "', expected '" << expected_message << "'" << std::endl;
        return 1;
    }
    return 0;
}

int ExpectUploadOk(const editor::sdk::CommandApplicationService::ImageUploadResult& result,
                   const std::string& expected_content_type_suffix) {
    if (result.code != editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok)) {
        std::cerr << "upload failed: code=" << result.code << " message=" << result.message << std::endl;
        return 1;
    }
    if (result.upload_id.empty()) {
        std::cerr << "upload_id is empty" << std::endl;
        return 1;
    }
    if (result.asset.path.empty()) {
        std::cerr << "asset path is empty" << std::endl;
        return 1;
    }
    if (result.asset.content_type.find(expected_content_type_suffix) == std::string::npos) {
        std::cerr << "unexpected content_type: " << result.asset.content_type << std::endl;
        return 1;
    }
    std::ifstream input(result.asset.path.c_str(), std::ios::binary);
    if (!input.good()) {
        std::cerr << "uploaded asset file not found: " << result.asset.path << std::endl;
        return 1;
    }
    return 0;
}


class OnlineTrialAuthProvider : public editor::sdk::ISdkAuthProvider {
public:
    std::string ProviderName() const override { return "online-trial-auth-provider"; }

    editor::sdk::AuthValidateResult ValidateToken(const editor::sdk::AuthValidateRequest& request) override {
        editor::sdk::AuthValidateResult result;
        result.auth_context = BuildContext(request.token);
        return result;
    }

    editor::sdk::AuthRefreshResult CreateSession(const editor::sdk::AuthValidateRequest& request) override {
        editor::sdk::AuthRefreshResult result;
        result.session_token = "online-trial-session-token";
        result.expires_in = 7200;
        result.auth_context = BuildContext(request.token);
        return result;
    }

    editor::sdk::AuthRefreshResult RefreshSession(const editor::sdk::AuthRefreshRequest&) override {
        editor::sdk::AuthRefreshResult result;
        result.session_token = "online-trial-session-token-refreshed";
        result.expires_in = 7200;
        result.auth_context = BuildContext("online-token");
        return result;
    }

    editor::sdk::SessionValidateResult ValidateSession(const editor::sdk::SessionValidateRequest&) override {
        editor::sdk::SessionValidateResult result;
        result.auth_context = BuildContext("online-token");
        return result;
    }

    editor::sdk::AuthContextResult GetAuthContext(const editor::sdk::AuthLookupRequest&) override {
        editor::sdk::AuthContextResult result;
        result.auth_context = BuildContext("online-token");
        return result;
    }

    editor::sdk::OfflineActivateResult ActivateOffline(const editor::sdk::OfflineActivateRequest&) override {
        editor::sdk::OfflineActivateResult result;
        result.code = editor::sdk::ToCode(editor::sdk::SdkStatusCode::InvalidParams);
        result.message = "online key cannot activate offline";
        return result;
    }

    editor::sdk::QuotaConsumeResult ConsumeQuota(const editor::sdk::QuotaConsumeRequest&) override {
        editor::sdk::QuotaConsumeResult result;
        result.auth_context = BuildContext("online-token");
        return result;
    }

private:
    editor::sdk::AuthContext BuildContext(const std::string&) const {
        editor::sdk::AuthContext context;
        context.is_valid = true;
        context.account_type = editor::sdk::SdkAccountType::Trial;
        context.account_type_code = editor::sdk::AccountTypeCode(context.account_type);
        context.licensed_account_type = editor::sdk::SdkAccountType::SvipPlus;
        context.licensed_account_type_code = editor::sdk::AccountTypeCode(context.licensed_account_type);
        context.auth_scene = "plugin";
        context.license_mode = "online_api_key";
        context.host_auth_mode = "none";
        context.entitlement_state = "online_trial";
        context.commercial_authorized = false;
        context.machine_code.clear();
        context.expires_at = 0;
        context.capabilities.push_back("system.ping");
        context.capabilities.push_back("auth.create_session");
        context.capabilities.push_back("auth.get_context");
        context.capabilities.push_back("ocr.recognize");
        editor::sdk::AuthQuotaBucket bucket;
        bucket.bucket = "trial_vip";
        bucket.limit = 5;
        bucket.remaining = 5;
        bucket.enforcement = "local_trial_quota";
        context.quota_buckets.push_back(bucket);
        return context;
    }
};

class CapturingOcrProvider : public editor::sdk::ISdkOcrProvider {
public:
    std::string ProviderName() const override { return "capturing-ocr-provider"; }

    editor::sdk::SdkOcrRecognizeResult Recognize(const editor::sdk::SdkOcrRecognizeRequest& request) override {
        last_request = request;
        recognize_called = true;
        editor::sdk::SdkOcrRecognizeResult result;
        result.task_id = "captured-ocr-task";
        result.task.task_id = result.task_id;
        result.task.status = "completed";
        result.task.progress = 100;
        result.task.output_path = request.output_path;
        result.task.output_paths.push_back(request.output_path);
        result.task.format = request.format;
        result.task.export_type = request.export_type;
        return result;
    }

    editor::sdk::SdkOcrGetResult GetTask(const editor::sdk::SdkOcrGetRequest&) override {
        return editor::sdk::SdkOcrGetResult();
    }

    editor::sdk::SdkOcrCancelResult Cancel(const editor::sdk::SdkOcrCancelRequest&) override {
        return editor::sdk::SdkOcrCancelResult();
    }

    editor::sdk::SdkOcrExtractTextResult ExtractText(const editor::sdk::SdkOcrExtractTextRequest&) override {
        return editor::sdk::SdkOcrExtractTextResult();
    }

    bool recognize_called = false;
    editor::sdk::SdkOcrRecognizeRequest last_request;
};

} // namespace

int main() {
    const std::string work_dir = TempWorkDir();
    ::setenv("SDK_OPEN_WORK_DIR", work_dir.c_str(), 1);

    editor::sdk::SdkConfig config;
    config.bind_host = "127.0.0.1";
    config.asset_http_port = 17082;

    editor::sdk::ProviderBundle providers = editor::sdk::mock::CreateProviderBundle();
    editor::sdk::CommandApplicationService service(config, providers);

    const std::string connection_id = "upload-smoke-connection";
    const std::string token = "mock-token";
    const editor::sdk::Json create_session_request = {
        {"request_id", "req-create-session"},
        {"method", "auth.create_session"},
        {"params", {{"token", token}}},
    };
    const editor::sdk::Json create_session_response = service.HandleRequest(connection_id, create_session_request);
    if (Expect(create_session_response.value("code", -1) == editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok),
               "auth.create_session failed") != 0) {
        return 1;
    }
    const editor::sdk::Json create_session_data = create_session_response.value("data", editor::sdk::Json::object());
    const std::string session_token = create_session_data.value("session_token", std::string());
    if (Expect(!session_token.empty(), "session_token is empty") != 0) {
        return 1;
    }
    const editor::sdk::Json auth_context_json = create_session_data.value("auth_context", editor::sdk::Json::object());
    if (Expect(auth_context_json.value("is_valid", false), "auth_context is not valid") != 0) {
        return 1;
    }

    const editor::sdk::Json get_context_request = {
        {"request_id", "req-get-context"},
        {"method", "auth.get_context"},
        {"params", editor::sdk::Json::object()},
    };
    const editor::sdk::Json get_context_response = service.HandleRequest(connection_id, get_context_request);
    if (Expect(get_context_response.value("code", -1) == editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok),
               "auth.get_context failed") != 0) {
        return 1;
    }
    const editor::sdk::Json get_context_data = get_context_response.value("data", editor::sdk::Json::object());
    const editor::sdk::Json context_snapshot = get_context_data.value("auth_context", editor::sdk::Json::object());
    if (Expect(context_snapshot.value("account_type", std::string()) == auth_context_json.value("account_type", std::string()),
               "auth.get_context changed account_type") != 0 ||
        Expect(context_snapshot.value("license_mode", std::string()) == auth_context_json.value("license_mode", std::string()),
               "auth.get_context changed license_mode") != 0 ||
        Expect(context_snapshot.value("entitlement_state", std::string()) == auth_context_json.value("entitlement_state", std::string()),
               "auth.get_context changed entitlement_state") != 0 ||
        Expect(context_snapshot.value("machine_code", std::string()) == auth_context_json.value("machine_code", std::string()),
               "auth.get_context changed machine_code") != 0 ||
        Expect(context_snapshot.value("device_scope", editor::sdk::Json::array()).size() == auth_context_json.value("device_scope", editor::sdk::Json::array()).size(),
               "auth.get_context changed device_scope") != 0) {
        std::cerr << "create_session auth_context=" << auth_context_json.dump()
                  << " get_context auth_context=" << context_snapshot.dump() << std::endl;
        return 1;
    }

    editor::sdk::ProviderBundle online_trial_providers = providers;
    online_trial_providers.auth_provider.reset(new OnlineTrialAuthProvider());
    editor::sdk::CommandApplicationService online_trial_service(config, online_trial_providers);
    const editor::sdk::Json online_create_session_request = {
        {"request_id", "req-online-create-session"},
        {"method", "auth.create_session"},
        {"params", {{"token", "online-token"}}},
    };
    const std::string online_connection_id = "online-trial-context-connection";
    const editor::sdk::Json online_create_response =
        online_trial_service.HandleRequest(online_connection_id, online_create_session_request);
    if (Expect(online_create_response.value("code", -1) == editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok),
               "online auth.create_session failed") != 0) {
        return 1;
    }
    const editor::sdk::Json online_create_context = online_create_response
        .value("data", editor::sdk::Json::object())
        .value("auth_context", editor::sdk::Json::object());
    const editor::sdk::Json online_get_context_request = {
        {"request_id", "req-online-get-context"},
        {"method", "auth.get_context"},
        {"params", editor::sdk::Json::object()},
    };
    const editor::sdk::Json online_get_response =
        online_trial_service.HandleRequest(online_connection_id, online_get_context_request);
    const editor::sdk::Json online_get_context = online_get_response
        .value("data", editor::sdk::Json::object())
        .value("auth_context", editor::sdk::Json::object());
    if (Expect(online_get_response.value("code", -1) == editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok),
               "online auth.get_context failed") != 0 ||
        Expect(online_create_context.value("account_type", std::string()) == "trial",
               "online test setup should start with trial account_type") != 0 ||
        Expect(online_get_context.value("license_mode", std::string()) == "online_api_key",
               "online auth.get_context must preserve online_api_key license_mode") != 0 ||
        Expect(online_get_context.value("licensed_account_type", std::string()) == "svip_plus",
               "online auth.get_context must preserve licensed_account_type") != 0 ||
        Expect(online_get_context.value("entitlement_state", std::string()) == "online_trial",
               "online auth.get_context must preserve entitlement_state") != 0 ||
        Expect(online_get_context.value("machine_code", std::string()) == "",
               "online auth.get_context must not invent an offline machine_code") != 0) {
        std::cerr << "online create_session auth_context=" << online_create_context.dump()
                  << " get_context auth_context=" << online_get_context.dump() << std::endl;
        return 1;
    }

    const std::string tiny_jpeg_bytes("\xFF\xD8\xFF\xD9", 4);
    const std::string tiny_png_bytes("\x89PNG\r\n\x1A\n", 8);
    const std::string tiny_bmp_bytes("BMxxxx", 6);
    const std::string tiny_tiff_bytes("II*\x00xxxx", 8);
    const std::string fake_pdf_bytes("%PDF-1.7\n", 9);

    if (ExpectUploadResult(service.UploadImage(session_token, "empty.jpg", "image/jpeg", ""),
                           editor::sdk::ToCode(editor::sdk::SdkStatusCode::UploadFileEmpty),
                           "uploaded file is empty") != 0) {
        return 1;
    }

    const std::string too_large(50U * 1024U * 1024U + 1U, 'x');
    if (ExpectUploadResult(service.UploadImage(session_token, "large.tiff", "application/tiff", too_large),
                           editor::sdk::ToCode(editor::sdk::SdkStatusCode::UploadFileTooLarge),
                           "uploaded file exceeds 50MB limit") != 0) {
        return 1;
    }

    if (ExpectUploadOk(service.UploadImage(session_token, "sample.bmp", "application/bmp", tiny_bmp_bytes), "bmp") != 0) {
        return 1;
    }
    if (ExpectUploadOk(service.UploadImage(session_token, "sample.tiff", "application/tiff", tiny_tiff_bytes), "tiff") != 0) {
        return 1;
    }
    if (ExpectUploadOk(service.UploadImage(session_token, "", "image/png", tiny_png_bytes), "png") != 0) {
        return 1;
    }
    if (ExpectUploadOk(service.UploadImage(session_token, "", "application/bmp", tiny_bmp_bytes), "bmp") != 0) {
        return 1;
    }
    if (ExpectUploadOk(service.UploadImage(session_token, "", "application/tiff", tiny_tiff_bytes), "tiff") != 0) {
        return 1;
    }

    if (ExpectUploadResult(service.UploadImage(session_token, "bad.bin", "image/png", tiny_png_bytes),
                           editor::sdk::ToCode(editor::sdk::SdkStatusCode::InvalidParams),
                           "uploaded file must be an image, PDF, OFD, or TIFF") != 0) {
        return 1;
    }

    const editor::sdk::CommandApplicationService::ImageUploadResult pdf_upload =
        service.UploadImage(session_token, "sample.pdf", "application/pdf", fake_pdf_bytes);
    if (pdf_upload.code != editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok)) {
        std::cerr << "pdf upload should succeed for later non-image validation: code="
                  << pdf_upload.code << " message=" << pdf_upload.message << std::endl;
        return 1;
    }

    std::shared_ptr<CapturingOcrProvider> capturing_ocr(new CapturingOcrProvider());
    providers.ocr_provider = capturing_ocr;
    editor::sdk::CommandApplicationService ocr_service(config, providers);
    const std::string ocr_connection_id = "ocr-quality-smoke-connection";
    const editor::sdk::Json ocr_create_session_response = ocr_service.HandleRequest(ocr_connection_id, create_session_request);
    if (Expect(ocr_create_session_response.value("code", -1) == editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok),
               "ocr auth.create_session failed") != 0) {
        return 1;
    }
    const editor::sdk::Json ocr_create_session_data = ocr_create_session_response.value("data", editor::sdk::Json::object());
    const std::string ocr_session_token = ocr_create_session_data.value("session_token", std::string());
    if (Expect(!ocr_session_token.empty(), "ocr session_token is empty") != 0) {
        return 1;
    }

    const editor::sdk::CommandApplicationService::ImageUploadResult ocr_input =
        ocr_service.UploadImage(ocr_session_token, "ocr-input.png", "image/png", tiny_png_bytes);
    if (ExpectUploadOk(ocr_input, "png") != 0) {
        return 1;
    }

    const editor::sdk::Json ocr_recognize_request = {
        {"request_id", "req-ocr-quality"},
        {"method", "ocr.recognize"},
        {"params", {
            {"input_upload_id", ocr_input.upload_id},
            {"format", "pdf"},
            {"exportType", "multi-page"},
            {"quality", 45}
        }},
    };
    const editor::sdk::Json ocr_recognize_response = ocr_service.HandleRequest(ocr_connection_id, ocr_recognize_request);
    if (Expect(ocr_recognize_response.value("code", -1) == editor::sdk::ToCode(editor::sdk::SdkStatusCode::Ok),
               "ocr.recognize with quality failed") != 0) {
        std::cerr << ocr_recognize_response.dump() << std::endl;
        return 1;
    }
    if (Expect(capturing_ocr->recognize_called, "capturing OCR provider was not called") != 0) {
        return 1;
    }
    if (Expect(!capturing_ocr->last_request.input_files.empty(), "OCR input files are empty") != 0) {
        return 1;
    }
    if (Expect(capturing_ocr->last_request.input_files.front() == ocr_input.asset.path,
               "OCR recognition input must remain the original uploaded asset") != 0) {
        return 1;
    }

    const editor::sdk::Json ext_params = editor::sdk::Json::parse(capturing_ocr->last_request.ext_params_json);
    if (Expect(ext_params.value("quality", -1) == 45, "OCR quality was not preserved as numeric public parameter") != 0) {
        return 1;
    }
    if (Expect(ext_params.contains("tmp_dir") && ext_params["tmp_dir"].is_string() && !ext_params["tmp_dir"].get<std::string>().empty(),
               "OCR PDF/OFD export should receive an SDK-managed tmp_dir for image-layer compression") != 0) {
        return 1;
    }
    if (Expect(ext_params["tmp_dir"].get<std::string>().find("/ocr/") != std::string::npos,
               "OCR tmp_dir should be scoped under the SDK OCR task directory") != 0) {
        return 1;
    }

    std::cout << "sdk_open_upload_smoke_test passed session_token_len=" << session_token.size() << std::endl;
    return 0;
}
