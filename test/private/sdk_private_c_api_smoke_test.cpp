// Copyright (c) 2026 CZUR Tech. All rights reserved.

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#include "auth/private_sdk_support.h"
#include "bindings/c/czur_sdk_private_c_api.h"

namespace {

void SetEnvValue(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value);
#else
    setenv(name, value, 1);
#endif
}

std::string ReadFileText(const std::string& path) {
    std::ifstream input(path.c_str());
    if (!input.is_open()) {
        return std::string();
    }
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

bool Contains(const std::string& value, const std::string& needle) {
    return value.find(needle) != std::string::npos;
}

int ExpectContains(const std::string& value, const std::string& needle) {
    if (!Contains(value, needle)) {
        std::cerr << "expected response to contain: " << needle << "\nresponse: " << value << std::endl;
        return 1;
    }
    return 0;
}

} // namespace

int main() {
    SetEnvValue("CZUR_SDK_MASTER_SECRET", "czur-sdk-private-c-api-smoke-secret");

    const char* generate_request =
        "{"
        "\"tier\":\"svip\","
        "\"license_mode\":\"offline_api_key\","
        "\"commercial_authorized\":true,"
        "\"device_scope\":[{\"vid\":4660,\"pid\":22136}],"
        "\"issued_at\":1710000000,"
        "\"expires_at\":0,"
        "\"key_id\":\"key_c_api_smoke\""
        "}";
    const char* generated = czur_sdk_private_generate_api_key_json(generate_request);
    if (generated == NULL) {
        std::cerr << "generate returned null" << std::endl;
        return 1;
    }
    const std::string generate_response(generated);
    czur_sdk_private_free_string(generated);

    if (ExpectContains(generate_response, "\"code\":0") != 0 ||
        ExpectContains(generate_response, "\"api_key\":\"sk-sq-v1-") != 0 ||
        ExpectContains(generate_response, "\"key_id\":\"key_c_api_smoke\"") != 0 ||
        ExpectContains(generate_response, "\"license_mode\":\"offline_api_key\"") != 0) {
        return 1;
    }

    const std::size_t api_key_name = generate_response.find("\"api_key\":\"");
    if (api_key_name == std::string::npos) {
        std::cerr << "api_key field missing: " << generate_response << std::endl;
        return 1;
    }
    const std::size_t api_key_begin = api_key_name + std::strlen("\"api_key\":\"");
    const std::size_t api_key_end = generate_response.find('"', api_key_begin);
    if (api_key_end == std::string::npos) {
        std::cerr << "api_key field unterminated: " << generate_response << std::endl;
        return 1;
    }
    const std::string api_key = generate_response.substr(api_key_begin, api_key_end - api_key_begin);

    const std::string parse_request = std::string("{\"api_key\":\"") + api_key + "\",\"now_ts\":1710000001}";
    const char* parsed = czur_sdk_private_parse_api_key_json(parse_request.c_str());
    if (parsed == NULL) {
        std::cerr << "parse returned null" << std::endl;
        return 1;
    }
    const std::string parse_response(parsed);
    czur_sdk_private_free_string(parsed);

    if (ExpectContains(parse_response, "\"code\":0") != 0 ||
        ExpectContains(parse_response, "\"tier\":\"svip\"") != 0 ||
        ExpectContains(parse_response, "\"license_mode\":\"offline_api_key\"") != 0 ||
        ExpectContains(parse_response, "\"vid\":4660") != 0 ||
        ExpectContains(parse_response, "\"pid\":22136") != 0) {
        return 1;
    }

    const char* host_auth_generate_request =
        "{"
        "\"tier\":\"svip\","
        "\"license_mode\":\"offline_api_key\","
        "\"host_auth_mode\":\"activation_required\","
        "\"commercial_authorized\":true,"
        "\"device_scope\":[{\"vid\":4660,\"pid\":22136}],"
        "\"issued_at\":1710000000,"
        "\"expires_at\":0,"
        "\"key_id\":\"key_c_api_host_auth_smoke\""
        "}";
    const char* host_auth_generated = czur_sdk_private_generate_api_key_json(host_auth_generate_request);
    if (host_auth_generated == NULL) {
        std::cerr << "host auth generate returned null" << std::endl;
        return 1;
    }
    const std::string host_auth_generate_response(host_auth_generated);
    czur_sdk_private_free_string(host_auth_generated);

    if (ExpectContains(host_auth_generate_response, "\"code\":0") != 0 ||
        ExpectContains(host_auth_generate_response, "\"host_auth_mode\":\"activation_required\"") != 0 ||
        ExpectContains(host_auth_generate_response, "\"licensed_account_type\":\"svip\"") != 0 ||
        ExpectContains(host_auth_generate_response, "\"tier\":\"trial\"") != 0 ||
        ExpectContains(host_auth_generate_response, "\"entitlement_state\":\"offline_trial\"") != 0) {
        return 1;
    }

    const std::size_t host_api_key_name = host_auth_generate_response.find("\"api_key\":\"");
    if (host_api_key_name == std::string::npos) {
        std::cerr << "host auth api_key field missing: " << host_auth_generate_response << std::endl;
        return 1;
    }
    const std::size_t host_api_key_begin = host_api_key_name + std::strlen("\"api_key\":\"");
    const std::size_t host_api_key_end = host_auth_generate_response.find('"', host_api_key_begin);
    if (host_api_key_end == std::string::npos) {
        std::cerr << "host auth api_key field unterminated: " << host_auth_generate_response << std::endl;
        return 1;
    }
    const std::string host_api_key =
        host_auth_generate_response.substr(host_api_key_begin, host_api_key_end - host_api_key_begin);

    const std::string host_parse_request =
        std::string("{\"api_key\":\"") + host_api_key + "\",\"now_ts\":1710000001}";
    const char* host_parsed = czur_sdk_private_parse_api_key_json(host_parse_request.c_str());
    if (host_parsed == NULL) {
        std::cerr << "host auth parse returned null" << std::endl;
        return 1;
    }
    const std::string host_parse_response(host_parsed);
    czur_sdk_private_free_string(host_parsed);

    if (ExpectContains(host_parse_response, "\"code\":0") != 0 ||
        ExpectContains(host_parse_response, "\"host_auth_mode\":\"activation_required\"") != 0 ||
        ExpectContains(host_parse_response, "\"licensed_account_type\":\"svip\"") != 0 ||
        ExpectContains(host_parse_response, "\"tier\":\"trial\"") != 0 ||
        ExpectContains(host_parse_response, "\"entitlement_state\":\"offline_trial\"") != 0) {
        return 1;
    }

    const char* auth_code_generated = czur_sdk_private_generate_auth_code_json(
        "{\"machine_code\":\"sdk-private-machine-smoke\",\"expires_date\":\"20261231\",\"auth_type\":13}");
    if (auth_code_generated == NULL) {
        std::cerr << "generate auth code returned null" << std::endl;
        return 1;
    }
    const std::string auth_code_response(auth_code_generated);
    czur_sdk_private_free_string(auth_code_generated);
    if (ExpectContains(auth_code_response, "\"code\":0") != 0 ||
        ExpectContains(auth_code_response, "\"auth_code\":\"") != 0 ||
        ExpectContains(auth_code_response, "\"machine_code\":\"sdk-private-machine-smoke\"") != 0 ||
        ExpectContains(auth_code_response, "\"expires_date\":\"20261231\"") != 0 ||
        ExpectContains(auth_code_response, "\"auth_type\":13") != 0) {
        return 1;
    }

    const char* missing_machine = czur_sdk_private_generate_auth_code_json(
        "{\"expires_date\":\"20261231\",\"auth_type\":13}");
    if (missing_machine == NULL) {
        std::cerr << "missing machine response returned null" << std::endl;
        return 1;
    }
    const std::string missing_machine_response(missing_machine);
    czur_sdk_private_free_string(missing_machine);
    if (ExpectContains(missing_machine_response, "\"code\":1002") != 0 ||
        ExpectContains(missing_machine_response, "machine_code required") != 0) {
        return 1;
    }

    const char* invalid_expires = czur_sdk_private_generate_auth_code_json(
        "{\"machine_code\":\"sdk-private-machine-smoke\",\"expires_date\":\"2026-12-31\",\"auth_type\":13}");
    if (invalid_expires == NULL) {
        std::cerr << "invalid expires response returned null" << std::endl;
        return 1;
    }
    const std::string invalid_expires_response(invalid_expires);
    czur_sdk_private_free_string(invalid_expires);
    if (ExpectContains(invalid_expires_response, "\"code\":1002") != 0 ||
        ExpectContains(invalid_expires_response, "expires_date must be YYYYMMDD") != 0) {
        return 1;
    }

    const char* client_only_type = czur_sdk_private_generate_auth_code_json(
        "{\"machine_code\":\"sdk-private-machine-smoke\",\"expires_date\":\"20261231\",\"auth_type\":12}");
    if (client_only_type == NULL) {
        std::cerr << "client-only type response returned null" << std::endl;
        return 1;
    }
    const std::string client_only_type_response(client_only_type);
    czur_sdk_private_free_string(client_only_type);
    if (ExpectContains(client_only_type_response, "\"code\":1002") != 0 ||
        ExpectContains(client_only_type_response, "auth_type must grant plugin access") != 0) {
        return 1;
    }

    SetEnvValue("CZUR_SDK_MASTER_SECRET", "czur-sdk-private-c-api-smoke-secret-mismatch");
    const char* mismatched = czur_sdk_private_parse_api_key_json(parse_request.c_str());
    if (mismatched == NULL) {
        std::cerr << "mismatched parse returned null" << std::endl;
        return 1;
    }
    const std::string mismatched_response(mismatched);
    czur_sdk_private_free_string(mismatched);
    if (ExpectContains(mismatched_response, "\"code\":1101") != 0) {
        return 1;
    }

    const std::string private_log_path = editor::sdk::private_impl::GetSdkRuntimeLogDir() + "/sdk_private_main.log";
    const std::string private_log = ReadFileText(private_log_path);
    if (!Contains(private_log, "auth.api_key parse_fail reason=signature_mismatch") ||
        !Contains(private_log, "token_digest=") ||
        !Contains(private_log, "master_secret_digest=")) {
        std::cerr << "expected detailed signature mismatch diagnostics in " << private_log_path << std::endl;
        return 1;
    }

    const char* invalid = czur_sdk_private_generate_api_key_json("{bad-json");
    if (invalid == NULL) {
        std::cerr << "invalid json response returned null" << std::endl;
        return 1;
    }
    const std::string invalid_response(invalid);
    czur_sdk_private_free_string(invalid);
    if (ExpectContains(invalid_response, "\"code\":1000") != 0) {
        return 1;
    }

    return 0;
}
