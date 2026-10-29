// Copyright (c) 2026 CZUR Tech. All rights reserved.

#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

#include "auth_manager.h"
#include "private_sdk_auth_service.h"
#include "private_sdk_support.h"

namespace {

using editor::sdk::SdkAccountType;
using editor::sdk::SdkDeviceGrant;
using editor::sdk::private_impl::ApiKeySpec;
using editor::sdk::private_impl::ParsedApiKey;
using editor::sdk::private_impl::PrivateSdkAuthService;

void PrintUsage() {
    std::cout << "Usage:\n"
              << "  sdk_auth_tool gen-apikey --mode offline|online --tier sp|sq|sr --device vid:pid[,vid:pid] [--host-auth-mode none|activation-required] [--expires-at ts] [--key-id id]\n"
              << "  sdk_auth_tool parse-apikey --api-key <key>\n"
              << "  sdk_auth_tool gen-machine-code\n"
              << "  sdk_auth_tool gen-auth-code --machine-code <code> --expires-date <YYYYMMDD> --auth-type <int>\n";
}

SdkAccountType ParseTier(const std::string& tier) {
    if (tier == "sp") {
        return SdkAccountType::SvipPlus;
    }
    if (tier == "sq") {
        return SdkAccountType::Svip;
    }
    if (tier == "sr") {
        return SdkAccountType::Vip;
    }
    return SdkAccountType::Unknown;
}

bool ParseDeviceList(const std::string& value, std::vector<SdkDeviceGrant>* device_scope) {
    if (device_scope == NULL) {
        return false;
    }
    device_scope->clear();
    if (value.empty()) {
        return true;
    }
    std::size_t pos = 0;
    while (pos <= value.size()) {
        const std::size_t comma_pos = value.find(',', pos);
        const std::string token =
            value.substr(pos, comma_pos == std::string::npos ? std::string::npos : comma_pos - pos);
        if (!token.empty()) {
            const std::size_t colon_pos = token.find(':');
            if (colon_pos == std::string::npos) {
                return false;
            }
            SdkDeviceGrant grant;
            grant.vid = std::atoi(token.substr(0, colon_pos).c_str());
            grant.pid = std::atoi(token.substr(colon_pos + 1).c_str());
            if (grant.vid <= 0 || grant.pid <= 0) {
                return false;
            }
            device_scope->push_back(grant);
        }
        if (comma_pos == std::string::npos) {
            break;
        }
        pos = comma_pos + 1;
    }
    return true;
}

std::string ParseHostAuthMode(const std::string& value) {
    if (value.empty() || value == "none") {
        return "none";
    }
    if (value == "activation-required" || value == "activation_required") {
        return "activation_required";
    }
    return value;
}

bool EnsureAuthManagerReady(std::string* err) {
    const std::string runtime_dir = editor::sdk::private_impl::GetSdkRuntimeDir();
    if (!editor::sdk::private_impl::EnsureDirectoryRecursive(runtime_dir)) {
        if (err != NULL) {
            *err = "failed to create sdk runtime dir";
        }
        return false;
    }

    editor::auth::AuthManager& auth_manager = editor::auth::AuthManager::GetInstance();
    if (!auth_manager.Initialize(runtime_dir + "/sdk_auth_tool.log",
                                 editor::auth::MachineUuidType::UUID_SSHORT_DMI_PRODUCT_ID)) {
        if (err != NULL) {
            *err = auth_manager.GetLastError();
        }
        return false;
    }
    return true;
}

int HandleGenerateApiKey(int argc, char** argv) {
    std::string mode = "offline";
    std::string tier;
    std::string device_arg;
    std::string host_auth_mode = "none";
    std::string key_id;
    std::int64_t expires_at = 0;
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::string name = argv[i];
        const std::string value = argv[i + 1];
        if (name == "--mode") {
            mode = value;
        } else if (name == "--tier") {
            tier = value;
        } else if (name == "--device") {
            device_arg = value;
        } else if (name == "--host-auth-mode") {
            host_auth_mode = ParseHostAuthMode(value);
        } else if (name == "--expires-at") {
            expires_at = std::atoll(value.c_str());
        } else if (name == "--key-id") {
            key_id = value;
        } else {
            PrintUsage();
            return 1;
        }
    }

    std::vector<SdkDeviceGrant> device_scope;
    if (tier.empty() || !ParseDeviceList(device_arg, &device_scope)) {
        PrintUsage();
        return 1;
    }

    ApiKeySpec spec;
    spec.account_type = ParseTier(tier);
    spec.license_mode = mode == "online" ? "online_api_key" : "offline_api_key";
    spec.host_auth_mode = host_auth_mode;
    spec.device_scope = device_scope;
    spec.issued_at = static_cast<std::int64_t>(std::time(NULL));
    spec.expires_at = expires_at;
    spec.key_id = key_id;

    if (spec.account_type == SdkAccountType::Unknown) {
        PrintUsage();
        return 1;
    }

    PrivateSdkAuthService auth_service;
    std::string api_key;
    std::string err;
    if (!auth_service.GenerateApiKey(spec, &api_key, &err)) {
        std::cerr << "generate failed: " << err << std::endl;
        return 2;
    }
    std::cout << api_key << std::endl;
    return 0;
}

int HandleParseApiKey(int argc, char** argv) {
    std::string api_key;
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::string name = argv[i];
        const std::string value = argv[i + 1];
        if (name == "--api-key") {
            api_key = value;
        } else {
            PrintUsage();
            return 1;
        }
    }

    if (api_key.empty()) {
        PrintUsage();
        return 1;
    }

    PrivateSdkAuthService auth_service;
    ParsedApiKey parsed;
    auth_service.ParseApiKey(api_key, static_cast<std::int64_t>(std::time(NULL)), &parsed);
    editor::sdk::AuthContext auth_context = parsed.auth_context;
    if (parsed.code == 0) {
        editor::sdk::AuthValidateRequest validate_request;
        validate_request.token = api_key;
        validate_request.now_ts = static_cast<std::int64_t>(std::time(NULL));
        editor::sdk::AuthValidateResult validate_result;
        auth_service.ValidateApiKey(validate_request, &validate_result);
        if (validate_result.code == 0) {
            auth_context = validate_result.auth_context;
        }
    }
    std::cout << "code=" << parsed.code << "\n";
    std::cout << "message=" << parsed.message << "\n";
    std::cout << "is_valid=" << (auth_context.is_valid ? "true" : "false") << "\n";
    std::cout << "account_type=" << editor::sdk::ToAccountTypeString(auth_context.account_type) << "\n";
    std::cout << "licensed_account_type=" << editor::sdk::ToAccountTypeString(auth_context.licensed_account_type) << "\n";
    std::cout << "license_mode=" << auth_context.license_mode << "\n";
    std::cout << "host_auth_mode=" << auth_context.host_auth_mode << "\n";
    std::cout << "entitlement_state=" << auth_context.entitlement_state << "\n";
    std::cout << "expires_at=" << auth_context.expires_at << "\n";
    for (std::size_t i = 0; i < auth_context.device_scope.size(); ++i) {
        std::cout << "device[" << i << "]=" << auth_context.device_scope[i].vid << ":"
                  << auth_context.device_scope[i].pid << "\n";
    }
    return parsed.code == 0 ? 0 : 2;
}

int HandleGenerateMachineCode() {
    std::string init_error;
    if (!EnsureAuthManagerReady(&init_error)) {
        std::cerr << "init auth manager failed: " << init_error << std::endl;
        return 2;
    }

    editor::auth::AuthManager& auth_manager = editor::auth::AuthManager::GetInstance();
    const std::string machine_code = auth_manager.GenerateMachineCode(
        editor::auth::LICENSE_KEY, editor::auth::MachineUuidType::UUID_SSHORT_DMI_PRODUCT_ID);
    if (machine_code.empty()) {
        std::cerr << "generate machine code failed: " << auth_manager.GetLastError() << std::endl;
        return 2;
    }
    std::cout << machine_code << std::endl;
    return 0;
}

int HandleGenerateAuthCode(int argc, char** argv) {
    std::string machine_code;
    std::string expires_date;
    int auth_type = -1;
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::string name = argv[i];
        const std::string value = argv[i + 1];
        if (name == "--machine-code") {
            machine_code = value;
        } else if (name == "--expires-date") {
            expires_date = value;
        } else if (name == "--auth-type") {
            auth_type = std::atoi(value.c_str());
        } else {
            PrintUsage();
            return 1;
        }
    }

    if (machine_code.empty() || expires_date.empty() || auth_type < 0) {
        PrintUsage();
        return 1;
    }

    PrivateSdkAuthService auth_service;
    const editor::sdk::private_impl::AuthCodeGenerateResult result =
        auth_service.GenerateAuthCode(machine_code, expires_date, auth_type);
    if (result.code != 0) {
        std::cerr << "generate auth code failed: " << result.message << std::endl;
        return 2;
    }
    std::cout << result.auth_code << std::endl;
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        PrintUsage();
        return 1;
    }

    const std::string command = argv[1];
    if (command == "gen-apikey") {
        return HandleGenerateApiKey(argc, argv);
    }
    if (command == "parse-apikey") {
        return HandleParseApiKey(argc, argv);
    }
    if (command == "gen-machine-code") {
        return HandleGenerateMachineCode();
    }
    if (command == "gen-auth-code") {
        return HandleGenerateAuthCode(argc, argv);
    }

    PrintUsage();
    return 1;
}
