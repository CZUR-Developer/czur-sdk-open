// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_tls_utils.h"

#include <algorithm>
#include <cstring>

#include "sdk_logger.h"

#if SDK_OPEN_ENABLE_TLS
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

namespace editor {
namespace sdk {

namespace {

#if SDK_OPEN_ENABLE_TLS
int PrivateKeyPasswordCallback(char* buffer, int size, int, void* user_data) {
    if (buffer == NULL || size <= 0 || user_data == NULL) {
        return 0;
    }
    const std::string* password = static_cast<const std::string*>(user_data);
    const int copy_size = static_cast<int>(std::min<std::size_t>(password->size(), static_cast<std::size_t>(size - 1)));
    if (copy_size > 0) {
        std::memcpy(buffer, password->data(), static_cast<std::size_t>(copy_size));
    }
    buffer[copy_size] = '\0';
    return copy_size;
}

std::string OpenSslError() {
    const unsigned long error = ERR_get_error();
    if (error == 0) {
        return "OpenSSL operation failed";
    }
    char buffer[256] = {0};
    ERR_error_string_n(error, buffer, sizeof(buffer));
    return buffer;
}
#endif

void SetError(std::string* error_message, const std::string& value) {
    if (error_message != NULL) {
        *error_message = value;
    }
}

} // namespace

bool ConfigureSdkTlsServerContext(void* native_ssl_ctx,
                                  const SdkTlsConfig& config,
                                  std::string* error_message) {
#if SDK_OPEN_ENABLE_TLS
    SSL_CTX* context = static_cast<SSL_CTX*>(native_ssl_ctx);
    if (context == NULL) {
        SetError(error_message, "TLS context is unavailable");
        return false;
    }
    if (config.certificate_chain_file.empty() || config.private_key_file.empty()) {
        SetError(error_message, "SDK_TLS_CERT_FILE and SDK_TLS_KEY_FILE are required when TLS is enabled");
        return false;
    }

    SSL_CTX_set_options(context, SSL_OP_NO_COMPRESSION | SSL_OP_NO_SESSION_RESUMPTION_ON_RENEGOTIATION);
    if (SSL_CTX_set_min_proto_version(context, TLS1_2_VERSION) != 1) {
        SetError(error_message, "failed to set TLS minimum protocol version: " + OpenSslError());
        return false;
    }
    if (!config.private_key_password.empty()) {
        SSL_CTX_set_default_passwd_cb(context, PrivateKeyPasswordCallback);
        SSL_CTX_set_default_passwd_cb_userdata(context, const_cast<std::string*>(&config.private_key_password));
    }
    if (SSL_CTX_use_certificate_chain_file(context, config.certificate_chain_file.c_str()) != 1) {
        SetError(error_message, "failed to load TLS certificate chain: " + OpenSslError());
        return false;
    }
    if (SSL_CTX_use_PrivateKey_file(context, config.private_key_file.c_str(), SSL_FILETYPE_PEM) != 1) {
        SetError(error_message, "failed to load TLS private key: " + OpenSslError());
        return false;
    }
    if (SSL_CTX_check_private_key(context) != 1) {
        SetError(error_message, "TLS certificate and private key do not match: " + OpenSslError());
        return false;
    }
    return true;
#else
    (void)native_ssl_ctx;
    (void)config;
    SetError(error_message, "sdk_open was built without TLS support");
    return false;
#endif
}

bool ValidateSdkTlsConfig(const SdkTlsConfig& config, std::string* error_message) {
    if (!config.enabled) {
        return true;
    }
#if SDK_OPEN_ENABLE_TLS
    SSL_CTX* context = SSL_CTX_new(TLS_server_method());
    if (context == NULL) {
        SetError(error_message, "failed to create TLS context: " + OpenSslError());
        return false;
    }
    const bool valid = ConfigureSdkTlsServerContext(context, config, error_message);
    SSL_CTX_free(context);
    return valid;
#else
    SetError(error_message, "SDK_TLS_ENABLED=1 requires an sdk_open build with SDK_OPEN_ENABLE_TLS=ON");
    return false;
#endif
}

} // namespace sdk
} // namespace editor
