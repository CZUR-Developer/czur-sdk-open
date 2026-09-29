// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "command_application_service.h"
#include "image_enhance_workflow_store.h"
#include "sdk_runtime_paths.h"
#include "sdk_entitlement_policy.h"

namespace editor {
namespace sdk {
namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string MakeTempDir() {
    char directory_template[] = "/tmp/sdk-open-workflow-store-XXXXXX";
    char* directory = mkdtemp(directory_template);
    Require(directory != NULL, "failed to create workflow test directory");
    return directory;
}

void WriteTextFile(const std::string& path, const std::string& content) {
    std::ofstream output(path.c_str(), std::ios::binary | std::ios::trunc);
    output << content;
    Require(output.good(), "failed to write " + path);
}

Json Request(const std::string& request_id, const std::string& method, const Json& params = Json::object()) {
    return Json{{"request_id", request_id}, {"method", method}, {"params", params}};
}

AuthContext MakeAuthContext() {
    AuthContext context;
    context.is_valid = true;
    context.account_type = SdkAccountType::SvipPlus;
    context.account_type_code = AccountTypeCode(SdkAccountType::SvipPlus);
    context.licensed_account_type = SdkAccountType::SvipPlus;
    context.licensed_account_type_code = AccountTypeCode(SdkAccountType::SvipPlus);
    context.auth_scene = "workflow-test";
    context.license_mode = "offline_api_key";
    context.entitlement_state = "offline_unlocked";
    context.capabilities.push_back("auth.create_session");
    context.capabilities.push_back("image.enhance");
    return context;
}

class WorkflowAuthProvider : public ISdkAuthProvider {
public:
    std::string ProviderName() const override { return "workflow-auth-test"; }

    AuthValidateResult ValidateToken(const AuthValidateRequest& request) override {
        AuthValidateResult result;
        if (request.token != "workflow-token") {
            result.code = ToCode(SdkStatusCode::TokenInvalid);
            result.message = "token invalid";
            return result;
        }
        result.auth_context = MakeAuthContext();
        return result;
    }

    AuthRefreshResult CreateSession(const AuthValidateRequest& request) override {
        AuthRefreshResult result;
        const AuthValidateResult validate = ValidateToken(request);
        result.code = validate.code;
        result.message = validate.message;
        result.auth_context = validate.auth_context;
        result.session_token = "workflow-session-token";
        result.expires_in = 7200;
        return result;
    }

    AuthRefreshResult RefreshSession(const AuthRefreshRequest&) override {
        AuthRefreshResult result;
        result.auth_context = MakeAuthContext();
        result.session_token = "workflow-session-token";
        result.expires_in = 7200;
        return result;
    }

    SessionValidateResult ValidateSession(const SessionValidateRequest&) override {
        SessionValidateResult result;
        result.auth_context = MakeAuthContext();
        return result;
    }

    AuthContextResult GetAuthContext(const AuthLookupRequest&) override {
        AuthContextResult result;
        result.via_session = true;
        result.auth_context = MakeAuthContext();
        return result;
    }

    OfflineActivateResult ActivateOffline(const OfflineActivateRequest&) override {
        OfflineActivateResult result;
        result.activated = true;
        result.session_token = "workflow-session-token";
        result.expires_in = 7200;
        result.auth_context = MakeAuthContext();
        return result;
    }

    QuotaConsumeResult ConsumeQuota(const QuotaConsumeRequest&) override {
        QuotaConsumeResult result;
        result.consumed = true;
        return result;
    }
};

std::unique_ptr<CommandApplicationService> MakeService() {
    ProviderBundle providers;
    providers.auth_provider = std::shared_ptr<ISdkAuthProvider>(new WorkflowAuthProvider());
    SdkConfig config;
    std::unique_ptr<CommandApplicationService> service(new CommandApplicationService(config, providers));
    service->SetProviderNames(Json{{"imageEnhance", "workflow-provider"}});
    return service;
}

void TestStoreCrudAndCompatibility(const std::string& root) {
    const ImageEnhanceWorkflowResult empty = ListImageEnhanceWorkflows();
    Require(IsOkStatusCode(empty.code), "empty list should succeed");
    Require(empty.data.value("count", 1U) == 0U, "empty list count should be zero");
    Require(empty.data.find("provider") == empty.data.end(), "shared store list must not add transport provider");

    const std::string store_path = JoinPath(JoinPath(root, "profiles"), "image_enhance_workflows.json");

    const ImageEnhanceWorkflowResult saved = SaveImageEnhanceWorkflow(Json{{"name", ""}, {"custom_field", "kept"}});
    Require(IsOkStatusCode(saved.code), "save with defaults should succeed");
    const Json workflow = saved.data["workflow"];
    const std::string generated_id = workflow.value("workflow_id", std::string());
    Require(!generated_id.empty(), "save should generate workflow_id");
    Require(workflow.value("name", std::string()) == "Untitled workflow", "save should normalize default name");
    Require(workflow.value("description", std::string()) == "", "save should normalize description");
    Require(workflow["pipeline"].value("version", std::string()) == "image.enhance.pipeline.v1", "save should normalize pipeline version");
    Require(workflow["pipeline"]["target"].value("format", std::string()) == "jpg", "save should normalize target format");
    Require(workflow.value("custom_field", std::string()) == "kept", "save should preserve custom fields");
    const std::string created_at = workflow.value("created_at", std::string());
    Require(!created_at.empty(), "save should set created_at");
    Require(!workflow.value("updated_at", std::string()).empty(), "save should set updated_at");

    const ImageEnhanceWorkflowResult got = GetImageEnhanceWorkflow(generated_id);
    Require(IsOkStatusCode(got.code), "get saved workflow should succeed");
    Require(got.data["workflow"].value("workflow_id", std::string()) == generated_id, "get should return requested workflow");

    const ImageEnhanceWorkflowResult updated = SaveImageEnhanceWorkflow(Json{{"workflow_id", generated_id},
                                                                            {"name", "Renamed"},
                                                                            {"created_at", "client-created"},
                                                                            {"extra_after_update", 7}});
    Require(IsOkStatusCode(updated.code), "update should succeed");
    Require(updated.data.value("updated", false), "update should mark updated=true");
    Require(updated.data["workflow"].value("created_at", std::string()) == created_at, "update should preserve existing created_at");
    Require(updated.data["workflow"].value("extra_after_update", 0) == 7, "update should preserve new custom field");

    const std::string reserved_temp_path = store_path + ".tmp";
    WriteTextFile(reserved_temp_path, "reserved by host");
    const ImageEnhanceWorkflowResult strict_saved = SaveImageEnhanceWorkflow(Json{{"workflow_id", "strict-valid"}, {"name", "Strict"}}, true);
    Require(IsOkStatusCode(strict_saved.code), "strict save should succeed for valid store");
    std::ifstream reserved_temp_input(reserved_temp_path.c_str(), std::ios::binary);
    std::string reserved_temp((std::istreambuf_iterator<char>(reserved_temp_input)), std::istreambuf_iterator<char>());
    Require(reserved_temp == "reserved by host", "strict writer must not remove pre-existing fixed .tmp path");
    std::remove(reserved_temp_path.c_str());
    const ImageEnhanceWorkflowResult strict_got = GetImageEnhanceWorkflow("strict-valid", true);
    Require(IsOkStatusCode(strict_got.code), "strict get should succeed for valid store");
    const ImageEnhanceWorkflowResult strict_deleted = DeleteImageEnhanceWorkflow("strict-valid", true);
    Require(IsOkStatusCode(strict_deleted.code), "strict delete should succeed for valid store");

    const ImageEnhanceWorkflowResult missing = GetImageEnhanceWorkflow("missing-workflow");
    Require(missing.code == ToCode(SdkStatusCode::InvalidParams), "missing get should keep InvalidParams");
    Require(missing.message == "image enhance workflow not found", "missing get message should be stable");

    const ImageEnhanceWorkflowResult deleted = DeleteImageEnhanceWorkflow(generated_id);
    Require(IsOkStatusCode(deleted.code), "delete should succeed");
    Require(deleted.data.value("deleted", false), "delete should mark deleted=true");
    Require(deleted.data.value("count", 99U) == 0U, "delete should return remaining count");

    const ImageEnhanceWorkflowResult missing_delete = DeleteImageEnhanceWorkflow(generated_id);
    Require(missing_delete.code == ToCode(SdkStatusCode::InvalidParams), "missing delete should keep InvalidParams");
    Require(missing_delete.message == "image enhance workflow not found", "missing delete message should be stable");

    const std::string trailing_payload = "{\"workflows\":[]} trailing";
    WriteTextFile(store_path, trailing_payload);
    const ImageEnhanceWorkflowResult legacy_trailing = ListImageEnhanceWorkflows();
    Require(IsOkStatusCode(legacy_trailing.code), "legacy list should keep accepting trailing garbage store");
    const ImageEnhanceWorkflowResult strict_trailing = ListImageEnhanceWorkflows(true);
    Require(strict_trailing.code == ToCode(SdkStatusCode::InternalError), "strict list should reject trailing garbage store");
    const ImageEnhanceWorkflowResult strict_trailing_save = SaveImageEnhanceWorkflow(Json{{"workflow_id", "strict-trailing"}}, true);
    Require(strict_trailing_save.code == ToCode(SdkStatusCode::InternalError), "strict save should not overwrite trailing garbage store");
    std::ifstream trailing_input(store_path.c_str(), std::ios::binary);
    std::string trailing_preserved((std::istreambuf_iterator<char>(trailing_input)), std::istreambuf_iterator<char>());
    Require(trailing_preserved == trailing_payload, "strict save should preserve trailing garbage store bytes");

    WriteTextFile(store_path, "{not-valid-json");
    const ImageEnhanceWorkflowResult after_invalid = ListImageEnhanceWorkflows();
    Require(IsOkStatusCode(after_invalid.code), "invalid json store should degrade to empty list");
    Require(after_invalid.data.value("count", 1U) == 0U, "invalid json store should list as empty");

    const ImageEnhanceWorkflowResult strict_list = ListImageEnhanceWorkflows(true);
    Require(strict_list.code == ToCode(SdkStatusCode::InternalError), "strict list should reject corrupt workflow store");
    const ImageEnhanceWorkflowResult strict_save = SaveImageEnhanceWorkflow(Json{{"workflow_id", "strict-wf"}}, true);
    Require(strict_save.code == ToCode(SdkStatusCode::InternalError), "strict save should not overwrite corrupt workflow store");
    std::ifstream input(store_path.c_str(), std::ios::binary);
    std::string preserved((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    Require(preserved == "{not-valid-json", "strict save should preserve corrupt workflow store bytes");
}

void TestCommandApplicationWorkflowBehavior() {
    std::unique_ptr<CommandApplicationService> service = MakeService();

    const Json unauthorized = service->HandleRequest("connection-a", Request("unauth", "image.enhance_workflow_list"));
    Require(unauthorized.value("code", -1) == ToCode(SdkStatusCode::AuthRequired), "workflow list should require session");

    const Json auth = service->HandleRequest("connection-a",
                                            Request("auth", "auth.create_session", Json{{"token", "workflow-token"}}));
    Require(auth.value("code", -1) == ToCode(SdkStatusCode::Ok), "auth.create_session should succeed");

    const Json list = service->HandleRequest("connection-a", Request("list-1", "image.enhance_workflow_list"));
    Require(list.value("code", -1) == ToCode(SdkStatusCode::Ok), "handler list should succeed");
    Require(list["data"].value("provider", std::string()) == "workflow-provider", "Open handler should append provider to list data");

    const Json save = service->HandleRequest("connection-a",
                                            Request("save-1",
                                                    "image.enhance_workflow_save",
                                                    Json{{"workflow", Json{{"workflow_id", "handler-wf"},
                                                                           {"name", "Handler workflow"},
                                                                           {"unknown", "kept"}}}}));
    Require(save.value("code", -1) == ToCode(SdkStatusCode::Ok), "handler save should succeed");
    Require(save["data"].value("saved", false), "handler save should keep saved field");
    Require(!save["data"].value("updated", true), "first handler save should keep updated=false");
    Require(save["data"]["workflow"].value("unknown", std::string()) == "kept", "handler save should preserve custom fields");

    const Json get = service->HandleRequest("connection-a",
                                           Request("get-1", "image.enhance_workflow_get", Json{{"workflow_id", "handler-wf"}}));
    Require(get.value("code", -1) == ToCode(SdkStatusCode::Ok), "handler get should succeed");
    Require(get["data"]["workflow"].value("name", std::string()) == "Handler workflow", "handler get should return workflow");

    const Json missing_get = service->HandleRequest("connection-a",
                                                   Request("get-missing", "image.enhance_workflow_get", Json{{"workflow_id", "missing"}}));
    Require(missing_get.value("code", -1) == ToCode(SdkStatusCode::InvalidParams), "handler missing get should keep InvalidParams");
    Require(missing_get.value("message", std::string()) == "image enhance workflow not found", "handler missing get message should be stable");

    const Json delete_response = service->HandleRequest("connection-a",
                                                       Request("delete-1", "image.enhance_workflow_delete", Json{{"workflow_id", "handler-wf"}}));
    Require(delete_response.value("code", -1) == ToCode(SdkStatusCode::Ok), "handler delete should succeed");
    Require(delete_response["data"].value("deleted", false), "handler delete should keep deleted field");

    const Json missing_id = service->HandleRequest("connection-a",
                                                  Request("get-empty", "image.enhance_workflow_get", Json::object()));
    Require(missing_id.value("code", -1) == ToCode(SdkStatusCode::InvalidParams), "missing workflow_id should keep InvalidParams");
    Require(missing_id.value("message", std::string()) == "workflow_id required", "missing workflow_id message should be stable");
}

void TestWriteFailure(const std::string& root) {
    const std::string profiles_dir = JoinPath(root, "profiles");
    const std::string command = "rm -rf '" + profiles_dir + "'";
    Require(std::system(command.c_str()) == 0, "failed to remove profiles directory for write failure test");
    WriteTextFile(profiles_dir, "not a directory");
    const ImageEnhanceWorkflowResult result = SaveImageEnhanceWorkflow(Json{{"workflow_id", "write-fails"}}, true);
    Require(result.code == ToCode(SdkStatusCode::InternalError), "strict save should fail when profiles path is not a directory");
    Require(result.message == "failed to read image enhance workflow store" ||
                result.message == "failed to save image enhance workflow",
            "strict directory obstruction should fail before data loss");
}

} // namespace
} // namespace sdk
} // namespace editor

int main() {
    try {
        const std::string root = editor::sdk::MakeTempDir();
        editor::sdk::Require(editor::sdk::ConfigureSdkCorePaths(root, "", false), "failed to configure SDK core paths");
        editor::sdk::TestStoreCrudAndCompatibility(root);
        editor::sdk::TestCommandApplicationWorkflowBehavior();
        editor::sdk::TestWriteFailure(root);
    } catch (const std::exception& e) {
        std::cerr << "sdk_open_image_enhance_workflow_store_test failed: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
