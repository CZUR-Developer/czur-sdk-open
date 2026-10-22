// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

#include "sdk_config.h"

namespace editor {
namespace sdk {

// The runtime keeps the CA private key on the local machine/user profile.  It
// is never part of an installer payload.  A missing state file therefore means
// that an installer has not provisioned managed TLS yet; in that case this
// manager leaves an existing customer certificate untouched.
struct SdkTlsCertificatePaths {
    std::string tls_dir;
    std::string root_certificate;
    std::string root_private_key;
    std::string certificate_chain;
    std::string private_key;
    std::string state;
};

struct SdkTlsRotationPolicy {
    int leaf_validity_days;
    int rotate_before_days;
    int check_interval_seconds;

    SdkTlsRotationPolicy();
};

class SdkTlsCertificateManager {
public:
    // Provision a new local CA and leaf pair.  Existing files are preserved
    // unless force is true.  This entry point is used by platform installers.
    static bool Provision(const SdkTlsCertificatePaths& paths,
                          const SdkTlsRotationPolicy& policy,
                          bool force,
                          std::string* error_message);

    static bool RenewLeaf(const SdkTlsCertificatePaths& paths,
                          const SdkTlsRotationPolicy& policy,
                          std::string* error_message);

    // Rotate only a CA/key pair that was provisioned by the installer.  The
    // manager detects that state from tls-state.json and never replaces a
    // customer supplied certificate implicitly.
    static bool RotateIfNeeded(const SdkTlsCertificatePaths& paths,
                               const SdkTlsRotationPolicy& policy,
                               bool* rotated,
                               std::string* error_message);

    static SdkTlsCertificatePaths PathsFromConfig(const SdkTlsConfig& config);
};

} // namespace sdk
} // namespace editor
