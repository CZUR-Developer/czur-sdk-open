// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_tls_certificate_manager.h"
#include "sdk_runtime_paths.h"

#include <fstream>
#include <iostream>
#include <sstream>

namespace {

bool Exists(const std::string& path) {
    std::ifstream input(path.c_str(), std::ios::binary);
    return input.good();
}

std::string ReadFile(const std::string& path) {
    std::ifstream input(path.c_str(), std::ios::binary);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: tls_certificate_manager_test <tls-dir>" << std::endl;
        return 2;
    }

    editor::sdk::SdkTlsCertificatePaths paths;
    paths.tls_dir = argv[1];
    paths.root_certificate = editor::sdk::JoinPath(paths.tls_dir, "local-runtime-root-ca.crt");
    paths.root_private_key = editor::sdk::JoinPath(paths.tls_dir, "local-runtime-root-ca.key.pem");
    paths.certificate_chain = editor::sdk::JoinPath(paths.tls_dir, "sdk-runtime.localhost.fullchain.pem");
    paths.private_key = editor::sdk::JoinPath(paths.tls_dir, "sdk-runtime.localhost.key.pem");
    paths.state = editor::sdk::JoinPath(paths.tls_dir, "tls-state.json");

    editor::sdk::SdkTlsRotationPolicy policy;
    std::string error;
    if (!editor::sdk::SdkTlsCertificateManager::Provision(paths, policy, false, &error)) {
        std::cerr << error << std::endl;
        return 1;
    }
    if (!Exists(paths.root_certificate) || !Exists(paths.root_private_key) ||
        !Exists(paths.certificate_chain) || !Exists(paths.private_key) ||
        !Exists(paths.state)) {
        std::cerr << "TLS provisioning did not create the complete managed pair" << std::endl;
        return 1;
    }

    const std::string root_before = ReadFile(paths.root_certificate);
    bool rotated = false;
    policy.rotate_before_days = 400;
    if (!editor::sdk::SdkTlsCertificateManager::RotateIfNeeded(
            paths, policy, &rotated, &error) || !rotated) {
        std::cerr << "TLS rotation threshold did not rotate the managed leaf: " << error << std::endl;
        return 1;
    }

    if (ReadFile(paths.root_certificate) != root_before ||
        !editor::sdk::SdkTlsCertificateManager::Provision(paths, editor::sdk::SdkTlsRotationPolicy(), false, &error)) {
        std::cerr << "rotation changed the CA or left an invalid pair: " << error << std::endl;
        return 1;
    }

    // A customer-supplied pair is preserved and is never put into the
    // automatic managed rotation path.
    editor::sdk::SdkTlsCertificatePaths custom = paths;
    custom.tls_dir = editor::sdk::JoinPath(paths.tls_dir, "custom");
    custom.root_certificate = editor::sdk::JoinPath(custom.tls_dir, "local-runtime-root-ca.crt");
    custom.root_private_key = editor::sdk::JoinPath(custom.tls_dir, "local-runtime-root-ca.key.pem");
    custom.certificate_chain = editor::sdk::JoinPath(custom.tls_dir, "sdk-runtime.localhost.fullchain.pem");
    custom.private_key = editor::sdk::JoinPath(custom.tls_dir, "sdk-runtime.localhost.key.pem");
    custom.state = editor::sdk::JoinPath(custom.tls_dir, "tls-state.json");
    if (!editor::sdk::EnsureDirectoryRecursive(custom.tls_dir)) return 1;
    {
        std::ifstream source_certificate(paths.certificate_chain.c_str(), std::ios::binary);
        std::ifstream source_key(paths.private_key.c_str(), std::ios::binary);
        std::ofstream certificate(custom.certificate_chain.c_str(), std::ios::binary);
        std::ofstream private_key(custom.private_key.c_str(), std::ios::binary);
        certificate << source_certificate.rdbuf();
        private_key << source_key.rdbuf();
    }
    if (!editor::sdk::SdkTlsCertificateManager::Provision(custom, policy, false, &error)) {
        std::cerr << "customer TLS pair was not preserved: " << error << std::endl;
        return 1;
    }
    bool custom_rotated = false;
    if (!editor::sdk::SdkTlsCertificateManager::RotateIfNeeded(custom, policy, &custom_rotated, &error) ||
        custom_rotated) {
        std::cerr << "customer TLS pair entered managed rotation unexpectedly" << std::endl;
        return 1;
    }

#if defined(__APPLE__)
    // Explicit managed state cannot fall back to custom validation on damage.
    const std::string root_key = ReadFile(paths.root_private_key);
    {
        std::ofstream broken(paths.root_private_key.c_str(), std::ios::binary | std::ios::trunc);
        broken << ReadFile(custom.private_key);
    }
    if (editor::sdk::SdkTlsCertificateManager::Provision(paths, policy, false, &error) ||
        editor::sdk::SdkTlsCertificateManager::RenewLeaf(paths, policy, &error)) {
        std::cerr << "damaged managed CA was reported as ready" << std::endl;
        return 1;
    }
    {
        std::ofstream restore(paths.root_private_key.c_str(), std::ios::binary | std::ios::trunc);
        restore << root_key;
    }
#endif
    std::cout << "PASS: TLS certificate provisioning and rotation" << std::endl;
    return 0;
}
