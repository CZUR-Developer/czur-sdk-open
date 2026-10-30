// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_tls_certificate_manager.h"
#include "sdk_runtime_paths.h"

#include <fstream>
#include <iostream>
#include <openssl/pem.h>
#include <openssl/x509v3.h>

namespace {

bool Exists(const std::string& path) {
    std::ifstream input(path.c_str(), std::ios::binary);
    return input.good();
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

    bool rotated = false;
    policy.rotate_before_days = 400;
    if (!editor::sdk::SdkTlsCertificateManager::RotateIfNeeded(
            paths, policy, &rotated, &error) || !rotated) {
        std::cerr << "TLS rotation threshold did not rotate the managed leaf: " << error << std::endl;
        return 1;
    }

    // Both desktop's DNS endpoint and Harmony's loopback URL must pass real
    // hostname verification; trusting the CA alone must not hide a SAN error.
    BIO* input = BIO_new_file(paths.certificate_chain.c_str(), "r");
    X509* leaf = input ? PEM_read_bio_X509(input, NULL, NULL, NULL) : NULL;
    BIO_free(input);
    const bool names_valid = leaf &&
        X509_check_host(leaf, "sdk-runtime.localhost", 0, 0, NULL) == 1 &&
        X509_check_host(leaf, "localhost", 0, 0, NULL) == 1 &&
        X509_check_ip_asc(leaf, "127.0.0.1", 0) == 1 &&
        X509_check_ip_asc(leaf, "192.168.1.116", 0) == 0;
    X509_free(leaf);
    if (!names_valid) {
        std::cerr << "TLS leaf SAN does not match the local runtime endpoints" << std::endl;
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
        std::ofstream certificate(custom.certificate_chain.c_str());
        std::ofstream private_key(custom.private_key.c_str());
        certificate << "customer certificate\n";
        private_key << "customer private key\n";
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

    std::cout << "PASS: TLS certificate provisioning and rotation" << std::endl;
    return 0;
}
