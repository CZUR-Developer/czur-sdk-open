// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#define SDK_OPEN_MAIN_TESTING
#include "../runtime/sdk_open_main.cpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <sys/file.h>
#include <sys/wait.h>
#include "sdk_tls_file_transaction.h"

namespace editor {
namespace sdk {
std::shared_ptr<spdlog::logger> GetSdkOpenLogger() {
    return spdlog::default_logger();
}
} // namespace sdk
} // namespace editor

void ExpectUrl(const editor::sdk::SdkConfig& config, const std::string& expected) {
    const std::string actual = editor::sdk::BuildSdkAssetBaseUrl(config);
    if (actual != expected) {
        throw std::runtime_error("expected " + expected + ", got " + actual);
    }
}

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void WriteFile(const std::string& path, const std::string& contents) {
    std::ofstream output(path.c_str(), std::ios::binary | std::ios::trunc);
    output << contents;
    if (!output) {
        throw std::runtime_error("failed to write " + path);
    }
}

std::string ReadFile(const std::string& path) {
    std::ifstream input(path.c_str(), std::ios::binary);
    std::ostringstream contents;
    contents << input.rdbuf();
    if (!input && !input.eof()) {
        throw std::runtime_error("failed to read " + path);
    }
    return contents.str();
}

std::string ParentDirectory(const std::string& path) {
    const std::string::size_type separator = path.find_last_of('/');
    return separator == std::string::npos ? "." : path.substr(0, separator);
}

void ResetPreparedEnvironment(const std::string& state_root,
                              const std::string& log_root) {
    ::setenv("CZUR_SDK_RUNTIME_DIR", state_root.c_str(), 1);
    ::setenv("SDK_OPEN_LOG_DIR", log_root.c_str(), 1);
    ::unsetenv("SDK_TLS_ENABLED");
    ::unsetenv("SDK_TLS_BIND_HOST");
    ::unsetenv("SDK_TLS_CERT_FILE");
    ::unsetenv("SDK_TLS_KEY_FILE");
}

void PrepareTlsDirectory(const std::string& state_root) {
    Require(editor::sdk::EnsureDirectoryRecursive(
                editor::sdk::JoinPath(state_root, "tls")),
            "failed to create scenario TLS directory");
}

void ExpectPrepareResult(const std::string& test_root,
                         const std::string& scenario,
                         bool expected) {
    const std::string scenario_root =
        editor::sdk::JoinPath(test_root, scenario);
    ResetPreparedEnvironment(
        scenario_root, editor::sdk::JoinPath(test_root, scenario + "-logs"));
    Require(PrepareMacRuntimeEnvironment() == expected,
            "unexpected preparation result for " + scenario);
}

editor::sdk::SdkTlsCertificatePaths TestPaths(const std::string& directory) {
    editor::sdk::SdkTlsConfig config;
    config.certificate_chain_file = directory + "/sdk-runtime.localhost.fullchain.pem";
    config.private_key_file = directory + "/sdk-runtime.localhost.key.pem";
    return editor::sdk::SdkTlsCertificateManager::PathsFromConfig(config);
}

void Generate(const std::string& directory) {
    std::string error;
    Require(editor::sdk::SdkTlsCertificateManager::Provision(
        TestPaths(directory), editor::sdk::SdkTlsRotationPolicy(), false, &error), error);
}

void CopyTls(const std::string& source, const std::string& destination) {
    Require(editor::sdk::EnsureDirectoryRecursive(destination), "create copy directory");
    const char* names[] = {"local-runtime-root-ca.crt", "local-runtime-root-ca.key.pem",
        "sdk-runtime.localhost.fullchain.pem", "sdk-runtime.localhost.key.pem", "tls-state.json"};
    for (const char* name : names) WriteFile(destination + "/" + name, ReadFile(source + "/" + name));
}

int main(int argc, char* argv[]) {
    if (argc != 2) return 2;
    Require(::getuid() != 0, "permission regression must run as an ordinary user");
    const std::string config_path = argv[1];
    const std::string test_root = ParentDirectory(config_path);
    const std::string system_tls = test_root + "/tls";
    const std::string certificate_name = "sdk-runtime.localhost.fullchain.pem";
    const std::string private_key_name = "sdk-runtime.localhost.key.pem";
    Generate(system_tls);
    const std::string current_certificate = ReadFile(system_tls + "/" + certificate_name);
    const std::string current_key = ReadFile(system_tls + "/" + private_key_name);
    const std::string first_tls = test_root + "/first-seed/tls";
    ExpectPrepareResult(test_root, "first-seed", true);
    Require(ReadFile(first_tls + "/" + certificate_name) == current_certificate, "fresh certificate");
    Require(ReadFile(first_tls + "/" + private_key_name) == current_key, "fresh key");
    struct stat permissions;
    Require(::stat(first_tls.c_str(), &permissions) == 0 &&
            (permissions.st_mode & 0777) == 0700, "TLS directory must be private");
    Require(::stat((first_tls + "/" + private_key_name).c_str(), &permissions) == 0 &&
            (permissions.st_mode & 0777) == 0600, "user private key mode");

    // Previously managed credentials from a removed/reinstalled system CA.
    Generate(test_root + "/old-managed/tls");
    ExpectPrepareResult(test_root, "old-managed", true);
    Require(ReadFile(test_root + "/old-managed/tls/" + certificate_name) == current_certificate,
            "old managed CA was not replaced");
    Require(ReadFile(test_root + "/old-managed/tls/local-runtime-root-ca.crt") ==
            ReadFile(system_tls + "/local-runtime-root-ca.crt"), "root metadata did not follow leaf");

    // Reproduce 0.1.1.80: the leaf was replaced, but copying the key failed.
    Generate(test_root + "/partial/tls");
    WriteFile(test_root + "/partial/tls/" + certificate_name, current_certificate);
    ExpectPrepareResult(test_root, "partial", true);
    Require(ReadFile(test_root + "/partial/tls/" + private_key_name) == current_key,
            "partial migration did not repair the key");

    // Read permission must be checked before any destination changes.
    CopyTls(system_tls, test_root + "/unreadable/tls");
    ::unlink((test_root + "/unreadable/tls/" + private_key_name).c_str());
    const std::string before = ReadFile(test_root + "/unreadable/tls/" + certificate_name);
    Require(::chmod((system_tls + "/" + private_key_name).c_str(), 0000) == 0, "hide system key");
    ExpectPrepareResult(test_root, "unreadable", false);
    Require(ReadFile(test_root + "/unreadable/tls/" + certificate_name) == before,
            "unreadable source changed the certificate");
    Require(::chmod((system_tls + "/" + private_key_name).c_str(), 0600) == 0, "restore system key");
    ExpectPrepareResult(test_root, "unreadable", true);

    // A per-user rotation is newer than the system leaf and must survive restarts.
    CopyTls(system_tls, test_root + "/rotated/tls");
    editor::sdk::SdkTlsRotationPolicy policy;
    policy.rotate_before_days = 400;
    bool rotated = false;
    std::string error;
    Require(editor::sdk::SdkTlsCertificateManager::RotateIfNeeded(
        TestPaths(test_root + "/rotated/tls"), policy, &rotated, &error) && rotated, error);
    const std::string rotated_leaf = ReadFile(test_root + "/rotated/tls/" + certificate_name);
    Require(rotated_leaf != current_certificate, "rotation did not create new leaf");
    ExpectPrepareResult(test_root, "rotated", true);
    ExpectPrepareResult(test_root, "rotated", true);
    Require(ReadFile(test_root + "/rotated/tls/" + certificate_name) == rotated_leaf,
            "restart replaced a valid rotated leaf");

    // Interrupted old rotation can leave a valid new leaf with stale state.
    WriteFile(test_root + "/rotated/tls/tls-state.json", ReadFile(system_tls + "/tls-state.json"));
    ExpectPrepareResult(test_root, "rotated", true);
    Require(ReadFile(test_root + "/rotated/tls/" + certificate_name) == rotated_leaf,
            "stale metadata discarded a valid rotated leaf");

    // A valid customer replacement can retain old managed metadata.
    Generate(test_root + "/custom-stale/tls");
    const std::string stale_custom_leaf = ReadFile(test_root + "/custom-stale/tls/" + certificate_name);
    WriteFile(test_root + "/custom-stale/tls/tls-state.json", ReadFile(system_tls + "/tls-state.json"));
    ExpectPrepareResult(test_root, "custom-stale", true);
    Require(ReadFile(test_root + "/custom-stale/tls/" + certificate_name) == stale_custom_leaf,
            "customer replacement with stale state was overwritten");
    rotated = false;
    Require(editor::sdk::SdkTlsCertificateManager::RotateIfNeeded(
        TestPaths(test_root + "/custom-stale/tls"), policy, &rotated, &error) && !rotated,
        "customer replacement entered managed rotation");

    // Custom certificates must be validated and preserved, never silently repaired.
    Generate(test_root + "/custom/tls");
    WriteFile(test_root + "/custom/tls/tls-state.json", "{\"mode\":\"custom\"}\n");
    const std::string custom_leaf = ReadFile(test_root + "/custom/tls/" + certificate_name);
    ExpectPrepareResult(test_root, "custom", true);
    Require(ReadFile(test_root + "/custom/tls/" + certificate_name) == custom_leaf, "custom changed");
    WriteFile(test_root + "/custom/tls/" + private_key_name, current_key);
    ExpectPrepareResult(test_root, "custom", false);
    Require(ReadFile(test_root + "/custom/tls/" + certificate_name) == custom_leaf, "bad custom overwritten");

    PrepareTlsDirectory(test_root + "/unknown");
    WriteFile(test_root + "/unknown/tls/" + certificate_name, "unknown damaged certificate\n");
    WriteFile(test_root + "/unknown/tls/" + private_key_name, "unknown damaged key\n");
    ExpectPrepareResult(test_root, "unknown", false);
    Require(ReadFile(test_root + "/unknown/tls/" + certificate_name) == "unknown damaged certificate\n",
            "unknown credentials overwritten");

    // Reject symlinks in any member, including metadata and the directory itself.
    const char* names[] = {"local-runtime-root-ca.crt", "local-runtime-root-ca.key.pem",
        "sdk-runtime.localhost.fullchain.pem", "sdk-runtime.localhost.key.pem", "tls-state.json"};
    int index = 0;
    for (const char* name : names) {
        const std::string scenario = "symlink-" + std::to_string(index++);
        CopyTls(system_tls, test_root + "/" + scenario + "/tls");
        const std::string link = test_root + "/" + scenario + "/tls/" + name;
        ::unlink(link.c_str());
        Require(::symlink((system_tls + "/" + name).c_str(), link.c_str()) == 0, "create symlink");
        ExpectPrepareResult(test_root, scenario, false);
    }

    // Both released legacy leaf variants use the same known key.
    for (int variant = 0; variant < 2; ++variant) {
        const std::string scenario = "legacy-" + std::to_string(variant);
        PrepareTlsDirectory(test_root + "/" + scenario);
        WriteFile(test_root + "/" + scenario + "/tls/" + certificate_name,
            ReadFile(test_root + (variant == 0 ? "/legacy-old.pem" : "/legacy-new.pem")));
        WriteFile(test_root + "/" + scenario + "/tls/" + private_key_name,
            ReadFile(test_root + "/legacy-key.pem"));
        ExpectPrepareResult(test_root, scenario, true);
        Require(ReadFile(test_root + "/" + scenario + "/tls/" + certificate_name) == current_certificate,
            "legacy pair was not migrated");
    }
    // Real certificate transactions recover from every individual commit point.
    const std::vector<std::string> members = {"local-runtime-root-ca.crt", "local-runtime-root-ca.key.pem",
        certificate_name, private_key_name, "tls-state.json"};
    for (int point = 0; point < 5; ++point) {
        const std::string scenario = "crash-" + std::to_string(point);
        const std::string tls = test_root + "/" + scenario + "/tls";
        Generate(tls);
        std::vector<std::string> original, paths;
        for (const std::string& name : members) {
            paths.push_back(tls + "/" + name);
            original.push_back(ReadFile(paths.back()));
        }
        const pid_t child = ::fork();
        Require(child >= 0, "fork migration crash");
        if (child == 0) {
            editor::sdk::SetTlsTransactionTestFault(point, editor::sdk::kTlsTransactionTestExit);
            ExpectPrepareResult(test_root, scenario, true);
            ::_exit(1);
        }
        int child_status = 0;
        Require(::waitpid(child, &child_status, 0) == child && WIFEXITED(child_status) &&
                WEXITSTATUS(child_status) == 86, "migration crash was not injected");
        Require(editor::sdk::RecoverTlsTransaction(tls, paths, &error), error);
        for (size_t i = 0; i < paths.size(); ++i)
            Require(ReadFile(paths[i]) == original[i], "recovery changed an original credential");
        ExpectPrepareResult(test_root, scenario, true);
        Require(ReadFile(tls + "/" + certificate_name) == current_certificate, "retry failed");
    }

    // A second process must not initialize while rotation/migration owns the lock.
    const std::string locked_tls = test_root + "/locked/tls";
    CopyTls(system_tls, locked_tls);
    const int lock_fd = ::open((locked_tls + "/tls-state.json.lock").c_str(), O_RDWR | O_CREAT, 0600);
    Require(lock_fd >= 0 && ::flock(lock_fd, LOCK_EX | LOCK_NB) == 0, "hold TLS lock");
    ExpectPrepareResult(test_root, "locked", false);
    ::flock(lock_fd, LOCK_UN); ::close(lock_fd);
    ExpectPrepareResult(test_root, "locked", true);

    // A custom system installation need not expose a CA signing key to users.
    const std::string system_state = ReadFile(system_tls + "/tls-state.json");
    WriteFile(system_tls + "/tls-state.json", "{\"mode\":\"custom\"}\n");
    Require(::chmod((system_tls + "/local-runtime-root-ca.key.pem").c_str(), 0000) == 0, "hide custom CA key");
    ExpectPrepareResult(test_root, "custom-first-seed", true);
    Require(ReadFile(test_root + "/custom-first-seed/tls/" + certificate_name) == current_certificate,
            "custom system leaf not seeded");
    ExpectPrepareResult(test_root, "custom-first-seed", true);
    Require(::chmod((system_tls + "/local-runtime-root-ca.key.pem").c_str(), 0600) == 0, "restore CA key");
    WriteFile(system_tls + "/tls-state.json", system_state);

    // Restore a successfully prepared environment for URL configuration checks.
    ExpectPrepareResult(test_root, "first-seed", true);
    ExpectUrl(LoadSdkOpenConfig("", true), "https://sdk-runtime.localhost:18082");
    ExpectUrl(LoadSdkOpenConfig("", false), "https://127.0.0.1:18082");

    ::setenv("SDK_ASSET_HTTPS_PORT", "19082", 1);
    ExpectUrl(LoadSdkOpenConfig("", true), "https://sdk-runtime.localhost:19082");

    ::setenv("SDK_ASSET_BASE_URL", "https://custom.example:20443", 1);
    ExpectUrl(LoadSdkOpenConfig("", true), "https://custom.example:20443");
    ::unsetenv("SDK_ASSET_BASE_URL");
    ::unsetenv("SDK_ASSET_HTTPS_PORT");

    {
        std::ofstream config(config_path.c_str());
        config << "SDK_ASSET_BASE_URL=https://config.example:21443\n";
        config << "SDK_ASSET_HTTPS_PORT=21443\n";
        if (!config) return 1;
    }
    ExpectUrl(LoadSdkOpenConfig(config_path, true), "https://config.example:21443");
    ::setenv("SDK_ASSET_BASE_URL", "https://env.example:22443", 1);
    ExpectUrl(LoadSdkOpenConfig(config_path, true), "https://env.example:22443");
    ::unsetenv("SDK_ASSET_BASE_URL");
    {
        std::ofstream config(config_path.c_str());
        config << "SDK_ASSET_HTTPS_PORT=21443\n";
        if (!config) return 1;
    }
    ExpectUrl(LoadSdkOpenConfig(config_path, true), "https://sdk-runtime.localhost:21443");

    ::setenv("SDK_TLS_ENABLED", "0", 1);
    ExpectUrl(LoadSdkOpenConfig("", true), "http://127.0.0.1:17082");
    std::cout << "PASS: macOS runtime TLS migration and URL configuration scenarios"
              << std::endl;
}
