// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#define SDK_OPEN_MAIN_TESTING
#include "../runtime/sdk_open_main.cpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

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

int main(int argc, char* argv[]) {
    if (argc != 2) return 2;

    const std::string config_path = argv[1];
    const std::string test_root = ParentDirectory(config_path);
    const std::string certificate_name =
        "sdk-runtime.localhost.fullchain.pem";
    const std::string private_key_name = "sdk-runtime.localhost.key.pem";
    const std::string current_certificate = "current certificate\n";
    const std::string legacy_certificate = "legacy certificate\n";
    const std::string default_private_key = "default private key\n";

    // First launch seeds the current packaged certificate and key.
    ExpectPrepareResult(test_root, "first-seed", true);
    const std::string first_tls = editor::sdk::JoinPath(
        editor::sdk::JoinPath(test_root, "first-seed"), "tls");
    const std::string first_certificate =
        editor::sdk::JoinPath(first_tls, certificate_name);
    Require(ReadFile(first_certificate) == current_certificate,
            "first launch did not seed the current certificate");
    Require(ReadFile(editor::sdk::JoinPath(first_tls, private_key_name)) ==
                default_private_key,
            "first launch did not seed the default private key");

    // A legacy default certificate is upgraded only with the matching default key.
    const std::string legacy_root =
        editor::sdk::JoinPath(test_root, "legacy-default");
    PrepareTlsDirectory(legacy_root);
    const std::string legacy_tls = editor::sdk::JoinPath(legacy_root, "tls");
    const std::string migrated_certificate =
        editor::sdk::JoinPath(legacy_tls, certificate_name);
    WriteFile(migrated_certificate, legacy_certificate);
    WriteFile(editor::sdk::JoinPath(legacy_tls, private_key_name),
              default_private_key);
    ExpectPrepareResult(test_root, "legacy-default", true);
    Require(ReadFile(migrated_certificate) == current_certificate,
            "legacy default certificate was not upgraded");

    // A missing key is re-seeded before the legacy certificate is evaluated.
    const std::string incomplete_root =
        editor::sdk::JoinPath(test_root, "legacy-missing-key");
    PrepareTlsDirectory(incomplete_root);
    const std::string incomplete_tls =
        editor::sdk::JoinPath(incomplete_root, "tls");
    WriteFile(editor::sdk::JoinPath(incomplete_tls, certificate_name),
              legacy_certificate);
    ExpectPrepareResult(test_root, "legacy-missing-key", true);
    Require(ReadFile(editor::sdk::JoinPath(incomplete_tls, certificate_name)) ==
                current_certificate,
            "legacy certificate with a missing key was not upgraded");
    Require(ReadFile(editor::sdk::JoinPath(incomplete_tls, private_key_name)) ==
                default_private_key,
            "missing default private key was not restored");

    // Re-running after migration must leave the certificate file untouched.
    struct stat migrated_stat;
    Require(::lstat(migrated_certificate.c_str(), &migrated_stat) == 0,
            "failed to stat migrated certificate");
    ExpectPrepareResult(test_root, "legacy-default", true);
    struct stat repeated_stat;
    Require(::lstat(migrated_certificate.c_str(), &repeated_stat) == 0 &&
                migrated_stat.st_dev == repeated_stat.st_dev &&
                migrated_stat.st_ino == repeated_stat.st_ino,
            "repeated preparation unexpectedly replaced the certificate");

    // Customer-provided certificates and keys are preserved independently.
    const std::string custom_certificate_root =
        editor::sdk::JoinPath(test_root, "custom-certificate");
    PrepareTlsDirectory(custom_certificate_root);
    const std::string custom_certificate_tls =
        editor::sdk::JoinPath(custom_certificate_root, "tls");
    WriteFile(editor::sdk::JoinPath(custom_certificate_tls, certificate_name),
              "customer certificate\n");
    WriteFile(editor::sdk::JoinPath(custom_certificate_tls, private_key_name),
              default_private_key);
    ExpectPrepareResult(test_root, "custom-certificate", true);
    Require(ReadFile(editor::sdk::JoinPath(
                custom_certificate_tls, certificate_name)) ==
                "customer certificate\n",
            "customer certificate was overwritten");

    const std::string custom_key_root =
        editor::sdk::JoinPath(test_root, "custom-key");
    PrepareTlsDirectory(custom_key_root);
    const std::string custom_key_tls =
        editor::sdk::JoinPath(custom_key_root, "tls");
    WriteFile(editor::sdk::JoinPath(custom_key_tls, certificate_name),
              legacy_certificate);
    WriteFile(editor::sdk::JoinPath(custom_key_tls, private_key_name),
              "customer private key\n");
    ExpectPrepareResult(test_root, "custom-key", true);
    Require(ReadFile(editor::sdk::JoinPath(custom_key_tls, certificate_name)) ==
                legacy_certificate,
            "legacy certificate with a customer key was overwritten");
    Require(ReadFile(editor::sdk::JoinPath(custom_key_tls, private_key_name)) ==
                "customer private key\n",
            "customer private key was overwritten");

    // Standalone LaunchAgent layouts may omit the legacy package marker.
    const std::string marker_path = editor::sdk::JoinPath(
        editor::sdk::JoinPath(test_root, "tls"),
        "legacy-sdk-runtime.localhost.pem");
    const std::string marker_backup = marker_path + ".backup";
    Require(::rename(marker_path.c_str(), marker_backup.c_str()) == 0,
            "failed to hide legacy marker");
    const std::string no_marker_root =
        editor::sdk::JoinPath(test_root, "custom-without-marker");
    PrepareTlsDirectory(no_marker_root);
    const std::string no_marker_tls =
        editor::sdk::JoinPath(no_marker_root, "tls");
    WriteFile(editor::sdk::JoinPath(no_marker_tls, certificate_name),
              "standalone customer certificate\n");
    WriteFile(editor::sdk::JoinPath(no_marker_tls, private_key_name),
              "standalone customer key\n");
    ExpectPrepareResult(test_root, "custom-without-marker", true);
    Require(ReadFile(editor::sdk::JoinPath(no_marker_tls, certificate_name)) ==
                "standalone customer certificate\n",
            "custom certificate changed without a legacy marker");
    Require(::rename(marker_backup.c_str(), marker_path.c_str()) == 0,
            "failed to restore legacy marker");

    // Existing symlinks are rejected instead of being followed or replaced.
    const std::string symlink_certificate_root =
        editor::sdk::JoinPath(test_root, "symlink-certificate");
    PrepareTlsDirectory(symlink_certificate_root);
    const std::string certificate_target = editor::sdk::JoinPath(
        test_root, "certificate-symlink-target.pem");
    WriteFile(certificate_target, "symlink target certificate\n");
    const std::string certificate_link = editor::sdk::JoinPath(
        editor::sdk::JoinPath(symlink_certificate_root, "tls"),
        certificate_name);
    Require(::symlink(certificate_target.c_str(), certificate_link.c_str()) == 0,
            "failed to create certificate symlink");
    ExpectPrepareResult(test_root, "symlink-certificate", false);
    Require(ReadFile(certificate_target) == "symlink target certificate\n",
            "certificate symlink target was modified");

    const std::string symlink_key_root =
        editor::sdk::JoinPath(test_root, "symlink-key");
    PrepareTlsDirectory(symlink_key_root);
    const std::string key_target =
        editor::sdk::JoinPath(test_root, "key-symlink-target.pem");
    WriteFile(key_target, "symlink target key\n");
    const std::string key_link = editor::sdk::JoinPath(
        editor::sdk::JoinPath(symlink_key_root, "tls"), private_key_name);
    Require(::symlink(key_target.c_str(), key_link.c_str()) == 0,
            "failed to create private-key symlink");
    ExpectPrepareResult(test_root, "symlink-key", false);
    Require(ReadFile(key_target) == "symlink target key\n",
            "private-key symlink target was modified");

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
