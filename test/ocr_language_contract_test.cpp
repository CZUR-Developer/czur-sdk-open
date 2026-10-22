// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "command_application_service.h"
#include "mock_provider_factory.h"
#include "ocr_facade.h"
#include "sdk_ocr_language.h"
#include "sdk_runtime_paths.h"

namespace editor {
namespace sdk {
namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class CaptureOcrProvider : public ISdkOcrProvider {
public:
    std::string ProviderName() const override { return "capture-ocr-provider"; }

    SdkOcrRecognizeResult Recognize(const SdkOcrRecognizeRequest& request) override {
        ++recognize_count;
        recognize_requests.push_back(request);
        SdkOcrRecognizeResult result;
        result.task_id = "capture-ocr-task";
        result.task.task_id = result.task_id;
        result.task.status = "completed";
        result.task.progress = 100;
        result.task.output_path = request.output_path;
        result.task.output_paths.push_back(request.output_path);
        result.task.format = request.format;
        result.task.export_type = request.export_type;
        return result;
    }

    SdkOcrGetResult GetTask(const SdkOcrGetRequest&) override {
        return SdkOcrGetResult();
    }

    SdkOcrCancelResult Cancel(const SdkOcrCancelRequest&) override {
        return SdkOcrCancelResult();
    }

    SdkOcrExtractTextResult ExtractText(const SdkOcrExtractTextRequest& request) override {
        ++extract_count;
        extract_requests.push_back(request);
        SdkOcrExtractTextResult result;
        result.recognized = true;
        result.input_path = request.input_path;
        return result;
    }

    int recognize_count = 0;
    int extract_count = 0;
    std::vector<SdkOcrRecognizeRequest> recognize_requests;
    std::vector<SdkOcrExtractTextRequest> extract_requests;
};

Json Request(const std::string& id, const std::string& method, const Json& params) {
    return Json{{"request_id", id}, {"method", method}, {"params", params}};
}

void RequireCode(const Json& response, SdkStatusCode expected, const std::string& message) {
    Require(response.value("code", -1) == ToCode(expected),
            message + ": " + response.value("message", std::string("missing response message")));
}

void TestLanguageHelpers() {
    const std::size_t language_count =
        sizeof(kSupportedSdkOcrLanguages) / sizeof(kSupportedSdkOcrLanguages[0]);
    Require(language_count == 90, "public OCR language list must contain auto and 89 concrete languages");
    Require(IsSupportedSdkOcrLanguage("auto"), "auto must be supported");
    Require(IsSupportedSdkOcrLanguage("ch"), "ch must be supported");
    Require(IsSupportedSdkOcrLanguage("en"), "en must be supported");
    Require(IsSupportedSdkOcrLanguage("chinese_cht"), "chinese_cht must be supported");
    Require(!IsSupportedSdkOcrLanguage("az"), "disabled language az must be rejected");
    Require(!IsSupportedSdkOcrLanguage("dar"), "disabled language dar must be rejected");
    Require(!IsSupportedSdkOcrLanguage("English"), "ABBYY language names must be rejected");
    Require(SdkOcrEngineLanguage("auto") == "ch", "auto must restore the current CZOCR default model");
    Require(SdkOcrEngineLanguage("en") == "en", "concrete CZOCR codes must pass through");

    std::string language;
    std::string error;
    Require(ReadSdkOcrLanguage(Json::object(), &language, &error) && language == "auto",
            "missing JSON language must default to auto");
    Require(!ReadSdkOcrLanguage(Json{{"language", 1}}, &language, &error),
            "non-string JSON language must fail");
    Require(!ReadSdkOcrLanguage(Json{{"language", ""}}, &language, &error),
            "empty JSON language must fail");
    Require(!ReadSdkOcrLanguage(Json{{"language", "az"}}, &language, &error),
            "disabled JSON language must fail");
}

void TestFacadeRejectsInvalidDirectCalls() {
    std::shared_ptr<CaptureOcrProvider> capture(new CaptureOcrProvider());
    ProviderBundle providers;
    providers.ocr_provider = capture;
    OcrFacade facade(providers);

    SdkOcrRecognizeRequest recognize;
    recognize.language = "en";
    RequireCode(Json{{"code", facade.Recognize(recognize).code}}, SdkStatusCode::Ok,
                "facade must forward supported recognize language");
    recognize.language = "";
    RequireCode(Json{{"code", facade.Recognize(recognize).code}}, SdkStatusCode::InvalidParams,
                "facade must reject empty recognize language");
    recognize.language = "az";
    RequireCode(Json{{"code", facade.Recognize(recognize).code}}, SdkStatusCode::InvalidParams,
                "facade must reject disabled recognize language");
    Require(capture->recognize_count == 1, "invalid recognize calls must not enter provider");

    SdkOcrExtractTextRequest extract;
    extract.language = "chinese_cht";
    RequireCode(Json{{"code", facade.ExtractText(extract).code}}, SdkStatusCode::Ok,
                "facade must forward supported extract language");
    extract.language = "unknown";
    RequireCode(Json{{"code", facade.ExtractText(extract).code}}, SdkStatusCode::InvalidParams,
                "facade must reject unknown extract language");
    Require(capture->extract_count == 1, "invalid extract calls must not enter provider");
}

void TestCommandProtocol(const std::string& test_dir) {
    Require(EnsureDirectoryRecursive(test_dir), "test directory must be created");
    const std::string input_path = JoinPath(test_dir, "input.jpg");
    const std::string output_path = JoinPath(test_dir, "recognized.txt");
    {
        std::ofstream input(input_path.c_str(), std::ios::binary | std::ios::trunc);
        input << "test image payload";
        Require(input.good(), "test input must be writable");
    }

    std::shared_ptr<CaptureOcrProvider> capture(new CaptureOcrProvider());
    ProviderBundle providers = mock::CreateProviderBundle();
    providers.ocr_provider = capture;
    SdkConfig config;
    CommandApplicationService service(config, providers);
    const std::string connection_id = "ocr-language-contract";

    RequireCode(service.HandleRequest(connection_id,
                                      Request("auth", "auth.create_session", Json{{"token", "mock-token"}})),
                SdkStatusCode::Ok,
                "mock session must be created");

    const std::vector<std::string> valid_languages = {"auto", "ch", "en", "chinese_cht"};
    for (std::vector<std::string>::const_iterator it = valid_languages.begin();
         it != valid_languages.end();
         ++it) {
        const Json params = Json{{"input_path", input_path},
                                 {"output_path", output_path},
                                 {"format", "txt"},
                                 {"language", *it}};
        RequireCode(service.HandleRequest(connection_id, Request("recognize-" + *it, "ocr.recognize", params)),
                    SdkStatusCode::Ok,
                    "recognize must accept " + *it);
        Require(capture->recognize_requests.back().language == *it,
                "recognize provider must receive public language " + *it);

        const Json extract_params = Json{{"input_path", input_path}, {"language", *it}};
        RequireCode(service.HandleRequest(connection_id,
                                          Request("extract-" + *it, "ocr.extract_text", extract_params)),
                    SdkStatusCode::Ok,
                    "extract_text must accept " + *it);
        Require(capture->extract_requests.back().language == *it,
                "extract provider must receive public language " + *it);
    }

    Json default_params = Json{{"input_path", input_path},
                               {"output_path", output_path},
                               {"format", "txt"},
                               {"ext_params", Json{{"language", "unknown"}}}};
    RequireCode(service.HandleRequest(connection_id, Request("recognize-default", "ocr.recognize", default_params)),
                SdkStatusCode::Ok,
                "recognize missing language must default to auto");
    Require(capture->recognize_requests.back().language == "auto",
            "nested ext language must not override the public default");
    const Json captured_ext = Json::parse(capture->recognize_requests.back().ext_params_json);
    Require(captured_ext.find("language") == captured_ext.end(),
            "unvalidated nested language must be removed before provider dispatch");

    RequireCode(service.HandleRequest(connection_id,
                                      Request("extract-default", "ocr.extract_text", Json{{"input_path", input_path}})),
                SdkStatusCode::Ok,
                "extract_text missing language must default to auto");
    Require(capture->extract_requests.back().language == "auto",
            "extract_text provider must receive default auto");

    const int recognize_count = capture->recognize_count;
    const std::vector<Json> invalid_values = {Json(""), Json(1), Json("unknown"), Json("az"), Json("dar")};
    for (std::vector<Json>::const_iterator it = invalid_values.begin(); it != invalid_values.end(); ++it) {
        const Json params = Json{{"input_path", input_path},
                                 {"output_path", output_path},
                                 {"format", "txt"},
                                 {"language", *it}};
        RequireCode(service.HandleRequest(connection_id, Request("recognize-invalid", "ocr.recognize", params)),
                    SdkStatusCode::InvalidParams,
                    "recognize invalid language must fail");
    }
    Require(capture->recognize_count == recognize_count,
            "invalid recognize language must not enter provider");

    const int extract_count = capture->extract_count;
    for (std::vector<Json>::const_iterator it = invalid_values.begin(); it != invalid_values.end(); ++it) {
        const Json params = Json{{"input_path", input_path}, {"language", *it}};
        RequireCode(service.HandleRequest(connection_id, Request("extract-invalid", "ocr.extract_text", params)),
                    SdkStatusCode::InvalidParams,
                    "extract_text invalid language must fail");
    }
    Require(capture->extract_count == extract_count,
            "invalid extract language must not enter provider");

    service.OnConnectionClosed(connection_id);
    std::remove(input_path.c_str());
}

} // namespace
} // namespace sdk
} // namespace editor

int main(int argc, char** argv) {
    try {
        editor::sdk::Require(argc == 2, "test output directory argument required");
        editor::sdk::TestLanguageHelpers();
        editor::sdk::TestFacadeRejectsInvalidDirectCalls();
        editor::sdk::TestCommandProtocol(argv[1]);
        std::cout << "sdk_open_ocr_language_contract_test passed" << std::endl;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "sdk_open_ocr_language_contract_test failed: " << error.what() << std::endl;
        return 1;
    }
}
