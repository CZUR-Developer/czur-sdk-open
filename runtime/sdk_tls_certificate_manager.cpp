// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_tls_certificate_manager.h"

#ifndef SDK_OPEN_ENABLE_TLS
#define SDK_OPEN_ENABLE_TLS 1
#endif

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <sstream>
#include <sys/stat.h>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#if SDK_OPEN_ENABLE_TLS
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#endif

#include "sdk_logger.h"
#include "sdk_runtime_paths.h"

namespace editor {
namespace sdk {

namespace {
void SetError(std::string* error_message, const std::string& value) {
    if (error_message != NULL) {
        *error_message = value;
    }
}
} // namespace

#if SDK_OPEN_ENABLE_TLS
namespace {

const char kManagedState[] = "managed";
const char kHostName[] = "sdk-runtime.localhost";

class TlsFileLock {
public:
    TlsFileLock() : acquired_(false)
#if defined(_WIN32)
        , handle_(NULL)
#else
        , file_descriptor_(-1)
#endif
    {}

    ~TlsFileLock() { Release(); }

    bool TryAcquire(const std::string& lock_path, bool* busy, std::string* error_message) {
        if (busy != NULL) *busy = false;
#if defined(_WIN32)
        const std::string name = "Local\\CZUR_SDK_OPEN_TLS_" +
                                 std::to_string(std::hash<std::string>()(lock_path));
        handle_ = ::CreateMutexA(NULL, FALSE, name.c_str());
        if (handle_ == NULL) {
            SetError(error_message, "cannot create TLS rotation mutex: " +
                                      std::to_string(::GetLastError()));
            return false;
        }
        const DWORD result = ::WaitForSingleObject(handle_, 0);
        if (result == WAIT_TIMEOUT) {
            if (busy != NULL) *busy = true;
            return true;
        }
        if (result != WAIT_OBJECT_0 && result != WAIT_ABANDONED) {
            SetError(error_message, "cannot acquire TLS rotation mutex: " +
                                      std::to_string(::GetLastError()));
            ::CloseHandle(handle_);
            handle_ = NULL;
            return false;
        }
        acquired_ = true;
        return true;
#else
        file_descriptor_ = ::open(lock_path.c_str(), O_RDWR | O_CREAT, 0600);
        if (file_descriptor_ < 0) {
            SetError(error_message, "cannot open TLS rotation lock: " + lock_path + ": " +
                                      std::strerror(errno));
            return false;
        }
        if (::flock(file_descriptor_, LOCK_EX | LOCK_NB) != 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                if (busy != NULL) *busy = true;
                return true;
            }
            SetError(error_message, "cannot acquire TLS rotation lock: " + lock_path + ": " +
                                      std::strerror(errno));
            ::close(file_descriptor_);
            file_descriptor_ = -1;
            return false;
        }
        acquired_ = true;
        return true;
#endif
    }

private:
    void Release() {
#if defined(_WIN32)
        if (acquired_) {
            ::ReleaseMutex(handle_);
        }
        if (handle_ != NULL) {
            ::CloseHandle(handle_);
            handle_ = NULL;
        }
#else
        if (file_descriptor_ >= 0) {
            if (acquired_) ::flock(file_descriptor_, LOCK_UN);
            ::close(file_descriptor_);
            file_descriptor_ = -1;
        }
#endif
        acquired_ = false;
    }

    bool acquired_;
#if defined(_WIN32)
    HANDLE handle_;
#else
    int file_descriptor_;
#endif
};

std::string OpenSslError() {
    char buffer[256] = {0};
    const unsigned long error = ERR_get_error();
    if (error == 0) {
        return "OpenSSL operation failed";
    }
    ERR_error_string_n(error, buffer, sizeof(buffer));
    return buffer;
}

bool FileExists(const std::string& path) {
#if defined(_WIN32)
    struct _stat st;
    return !path.empty() && ::_stat(path.c_str(), &st) == 0 && (st.st_mode & _S_IFREG) != 0;
#else
    struct stat st;
    return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
#endif
}

bool ReadText(const std::string& path, std::string* value) {
    std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
    if (!input.is_open() || value == NULL) {
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    *value = buffer.str();
    return input.good() || input.eof();
}

bool WriteTextAtomically(const std::string& path,
                        const std::string& value,
                        int mode,
                        std::string* error_message) {
    const std::string temporary = path + ".next";
#if defined(_WIN32)
    {
        std::ofstream output(temporary.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            SetError(error_message, "cannot open temporary TLS file: " + temporary);
            return false;
        }
        output.write(value.data(), static_cast<std::streamsize>(value.size()));
        output.flush();
        if (!output) {
            SetError(error_message, "cannot write temporary TLS file: " + temporary);
            return false;
        }
    }
    if (!::MoveFileExA(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        SetError(error_message, "cannot replace TLS file: " + path + ": " + std::to_string(::GetLastError()));
        std::remove(temporary.c_str());
        return false;
    }
#else
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC, static_cast<mode_t>(mode));
    if (fd < 0) {
        SetError(error_message, "cannot open temporary TLS file: " + temporary + ": " + std::strerror(errno));
        return false;
    }
    const char* cursor = value.data();
    std::size_t remaining = value.size();
    bool ok = true;
    while (remaining > 0) {
        const ssize_t written = ::write(fd, cursor, remaining);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            ok = false;
            break;
        }
        cursor += written;
        remaining -= static_cast<std::size_t>(written);
    }
    if (ok && ::fchmod(fd, static_cast<mode_t>(mode)) != 0) {
        ok = false;
    }
    if (ok && ::fsync(fd) != 0) {
        ok = false;
    }
    const int close_result = ::close(fd);
    if (!ok || close_result != 0 || ::rename(temporary.c_str(), path.c_str()) != 0) {
        SetError(error_message, "cannot replace TLS file: " + path + ": " + std::strerror(errno));
        ::unlink(temporary.c_str());
        return false;
    }
#endif
    return true;
}

EVP_PKEY* GenerateKey() {
    EVP_PKEY_CTX* context = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    if (context == NULL || EVP_PKEY_keygen_init(context) <= 0 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(context, 2048) <= 0) {
        if (context != NULL) EVP_PKEY_CTX_free(context);
        return NULL;
    }
    EVP_PKEY* key = NULL;
    if (EVP_PKEY_keygen(context, &key) <= 0) {
        key = NULL;
    }
    EVP_PKEY_CTX_free(context);
    return key;
}

bool SetExtension(X509* certificate, int critical, const char* name, const char* value) {
    X509V3_CTX context;
    X509V3_set_ctx_nodb(&context);
    X509V3_set_ctx(&context, certificate, certificate, NULL, NULL, 0);
    X509_EXTENSION* extension = X509V3_EXT_conf_nid(NULL, &context,
                                                      OBJ_txt2nid(name),
                                                      const_cast<char*>(value));
    if (extension == NULL) {
        return false;
    }
    if (critical) {
        X509_EXTENSION_set_critical(extension, 1);
    }
    const int result = X509_add_ext(certificate, extension, -1);
    X509_EXTENSION_free(extension);
    return result == 1;
}

bool SetCertificateTimes(X509* certificate, int days) {
    return X509_gmtime_adj(X509_getm_notBefore(certificate), 0) != NULL &&
           X509_gmtime_adj(X509_getm_notAfter(certificate), static_cast<long>(days) * 24L * 60L * 60L) != NULL;
}

bool WritePrivateKey(EVP_PKEY* key, const std::string& path, int mode, std::string* error_message) {
    BIO* memory = BIO_new(BIO_s_mem());
    if (memory == NULL || PEM_write_bio_PrivateKey(memory, key, NULL, NULL, 0, NULL, NULL) != 1) {
        if (memory != NULL) BIO_free(memory);
        SetError(error_message, OpenSslError());
        return false;
    }
    BUF_MEM* buffer = NULL;
    BIO_get_mem_ptr(memory, &buffer);
    const std::string contents(buffer->data, buffer->length);
    BIO_free(memory);
    return WriteTextAtomically(path, contents, mode, error_message);
}

bool WriteCertificate(X509* certificate, const std::string& path, int mode, std::string* error_message) {
    BIO* memory = BIO_new(BIO_s_mem());
    if (memory == NULL || PEM_write_bio_X509(memory, certificate) != 1) {
        if (memory != NULL) BIO_free(memory);
        SetError(error_message, OpenSslError());
        return false;
    }
    BUF_MEM* buffer = NULL;
    BIO_get_mem_ptr(memory, &buffer);
    const std::string contents(buffer->data, buffer->length);
    BIO_free(memory);
    return WriteTextAtomically(path, contents, mode, error_message);
}

std::string CertificateFingerprint(X509* certificate) {
    if (certificate == NULL) return std::string();
    const int der_length = i2d_X509(certificate, NULL);
    if (der_length <= 0) return std::string();
    std::vector<unsigned char> der(static_cast<std::size_t>(der_length));
    unsigned char* der_cursor = der.data();
    if (i2d_X509(certificate, &der_cursor) != der_length) return std::string();
    unsigned char digest[EVP_MAX_MD_SIZE] = {0};
    unsigned int digest_length = 0;
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    if (context == NULL || EVP_DigestInit_ex(context, EVP_sha256(), NULL) != 1 ||
        EVP_DigestUpdate(context, der.data(), der.size()) != 1 ||
        EVP_DigestFinal_ex(context, digest, &digest_length) != 1) {
        if (context != NULL) EVP_MD_CTX_free(context);
        return std::string();
    }
    EVP_MD_CTX_free(context);
    std::ostringstream value;
    value << std::hex;
    for (unsigned int i = 0; i < digest_length; ++i) {
        value.width(2);
        value.fill('0');
        value << static_cast<unsigned int>(digest[i]);
    }
    return value.str();
}

X509* ReadCertificate(const std::string& path) {
    BIO* input = BIO_new_file(path.c_str(), "rb");
    if (input == NULL) return NULL;
    X509* certificate = PEM_read_bio_X509(input, NULL, NULL, NULL);
    BIO_free(input);
    return certificate;
}

EVP_PKEY* ReadPrivateKey(const std::string& path) {
    BIO* input = BIO_new_file(path.c_str(), "rb");
    if (input == NULL) return NULL;
    EVP_PKEY* key = PEM_read_bio_PrivateKey(input, NULL, NULL, NULL);
    BIO_free(input);
    return key;
}

X509* CreateRootCertificate(EVP_PKEY* key, int days) {
    X509* certificate = X509_new();
    if (certificate == NULL) return NULL;
    X509_set_version(certificate, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(certificate), 1);
    X509_set_pubkey(certificate, key);
    X509_NAME* name = X509_get_subject_name(certificate);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>("Local Runtime Root CA"), -1, -1, 0);
    X509_set_issuer_name(certificate, name);
    if (!SetCertificateTimes(certificate, days) || !SetExtension(certificate, 1, "basicConstraints", "critical,CA:TRUE") ||
        !SetExtension(certificate, 1, "keyUsage", "critical,keyCertSign,cRLSign") ||
        !SetExtension(certificate, 0, "subjectKeyIdentifier", "hash") ||
        X509_sign(certificate, key, EVP_sha256()) <= 0) {
        X509_free(certificate);
        return NULL;
    }
    return certificate;
}

X509* CreateLeafCertificate(EVP_PKEY* leaf_key, X509* root, EVP_PKEY* root_key, int days) {
    X509* certificate = X509_new();
    if (certificate == NULL) return NULL;
    X509_set_version(certificate, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(certificate), static_cast<long>(std::time(NULL)));
    X509_set_pubkey(certificate, leaf_key);
    X509_NAME* subject = X509_get_subject_name(certificate);
    X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
                               reinterpret_cast<const unsigned char*>(kHostName), -1, -1, 0);
    X509_set_issuer_name(certificate, X509_get_subject_name(root));
    if (!SetCertificateTimes(certificate, days) || !SetExtension(certificate, 0, "basicConstraints", "critical,CA:FALSE") ||
        !SetExtension(certificate, 1, "keyUsage", "critical,digitalSignature,keyEncipherment") ||
        !SetExtension(certificate, 0, "extendedKeyUsage", "serverAuth") ||
        !SetExtension(certificate, 0, "subjectAltName", "DNS:sdk-runtime.localhost,DNS:localhost,IP:127.0.0.1") ||
        X509_sign(certificate, root_key, EVP_sha256()) <= 0) {
        X509_free(certificate);
        return NULL;
    }
    return certificate;
}

bool IsManagedState(const std::string& path) {
    std::string state;
    return ReadText(path, &state) && state.find("\"mode\":\"managed\"") != std::string::npos;
}

std::string StateLeafFingerprint(const std::string& path) {
    std::string state;
    if (!ReadText(path, &state)) return std::string();
    const std::string marker = "\"leafSha256\":\"";
    const std::string::size_type begin = state.find(marker);
    if (begin == std::string::npos) return std::string();
    const std::string::size_type value_begin = begin + marker.size();
    const std::string::size_type value_end = state.find('"', value_begin);
    return value_end == std::string::npos ? std::string() : state.substr(value_begin, value_end - value_begin);
}

std::string RootFingerprint(const std::string& path) {
    X509* root = ReadCertificate(path);
    if (root == NULL) return std::string();
    const std::string fingerprint = CertificateFingerprint(root);
    X509_free(root);
    return fingerprint;
}

std::string ManagedStateJson(const SdkTlsCertificatePaths& paths,
                             const std::string& leaf_fingerprint) {
    return "{\"mode\":\"managed\",\"leafValidityDays\":397,\"rotateBeforeDays\":90,\"leafSha256\":\"" +
           leaf_fingerprint + "\",\"rootSha256\":\"" + RootFingerprint(paths.root_certificate) + "\"}\n";
}

bool IsManagedLeaf(const SdkTlsCertificatePaths& paths) {
    if (!IsManagedState(paths.state)) return false;
    X509* leaf = ReadCertificate(paths.certificate_chain);
    X509* root = ReadCertificate(paths.root_certificate);
    if (leaf == NULL || root == NULL) {
        if (leaf != NULL) X509_free(leaf);
        if (root != NULL) X509_free(root);
        return false;
    }
    const std::string expected_fingerprint = StateLeafFingerprint(paths.state);
    const std::string actual_fingerprint = CertificateFingerprint(leaf);
    bool matches = !expected_fingerprint.empty() &&
                   expected_fingerprint == actual_fingerprint;
    if (expected_fingerprint.empty()) {
        EVP_PKEY* root_key = ReadPrivateKey(paths.root_private_key);
        matches = root_key != NULL &&
                  X509_NAME_cmp(X509_get_issuer_name(leaf), X509_get_subject_name(root)) == 0 &&
                  X509_verify(leaf, root_key) == 1;
        if (root_key != NULL) EVP_PKEY_free(root_key);
    }
    X509_free(leaf);
    X509_free(root);
    return matches;
}

bool LeafNeedsRotation(const std::string& path, int rotate_before_days) {
    X509* certificate = ReadCertificate(path);
    if (certificate == NULL) return true;
    std::time_t threshold = std::time(NULL) + static_cast<std::time_t>(rotate_before_days) * 24 * 60 * 60;
    struct tm threshold_tm;
#if defined(_WIN32)
    gmtime_s(&threshold_tm, &threshold);
#else
    gmtime_r(&threshold, &threshold_tm);
#endif
    const int result = X509_cmp_time(X509_get0_notAfter(certificate), &threshold);
    X509_free(certificate);
    return result <= 0;
}

bool BuildLeaf(const SdkTlsCertificatePaths& paths,
               const SdkTlsRotationPolicy& policy,
               std::string* leaf_fingerprint,
               std::string* error_message) {
    X509* root = ReadCertificate(paths.root_certificate);
    EVP_PKEY* root_key = ReadPrivateKey(paths.root_private_key);
    if (root == NULL || root_key == NULL) {
        if (root != NULL) X509_free(root);
        if (root_key != NULL) EVP_PKEY_free(root_key);
        SetError(error_message, "managed TLS root certificate or private key is unavailable");
        return false;
    }
    EVP_PKEY* leaf_key = GenerateKey();
    X509* leaf = leaf_key == NULL ? NULL : CreateLeafCertificate(leaf_key, root, root_key, policy.leaf_validity_days);
    const std::string staged_key = paths.private_key + ".rotation";
    const std::string staged_certificate = paths.certificate_chain + ".rotation";
    bool ok = leaf != NULL && WritePrivateKey(leaf_key, staged_key, 0600, error_message) &&
              WriteCertificate(leaf, staged_certificate, 0644, error_message);
    if (ok) {
        // Keep the old pair until both staged files have been moved.  If the
        // second move fails, restore the previous pair before returning.
        const std::string previous_key = paths.private_key + ".previous";
        const std::string previous_certificate = paths.certificate_chain + ".previous";
        const bool had_key = FileExists(paths.private_key);
        const bool had_certificate = FileExists(paths.certificate_chain);
        std::remove(previous_key.c_str());
        std::remove(previous_certificate.c_str());
        bool moved_old_key = !had_key || std::rename(paths.private_key.c_str(), previous_key.c_str()) == 0;
        bool moved_old_certificate = !had_certificate ||
                                     std::rename(paths.certificate_chain.c_str(), previous_certificate.c_str()) == 0;
        if (moved_old_key && moved_old_certificate &&
            std::rename(staged_key.c_str(), paths.private_key.c_str()) == 0 &&
            std::rename(staged_certificate.c_str(), paths.certificate_chain.c_str()) == 0) {
            std::remove(previous_key.c_str());
            std::remove(previous_certificate.c_str());
        } else {
            ok = false;
            SetError(error_message, "failed to install rotated TLS certificate pair");
            std::remove(staged_key.c_str());
            std::remove(staged_certificate.c_str());
            std::remove(paths.private_key.c_str());
            std::remove(paths.certificate_chain.c_str());
            if (moved_old_key && had_key) std::rename(previous_key.c_str(), paths.private_key.c_str());
            if (moved_old_certificate && had_certificate) {
                std::rename(previous_certificate.c_str(), paths.certificate_chain.c_str());
            }
        }
        if (ok && leaf_fingerprint != NULL) {
            *leaf_fingerprint = CertificateFingerprint(leaf);
            if (leaf_fingerprint->empty()) {
                ok = false;
                SetError(error_message, "cannot fingerprint rotated TLS certificate");
            }
        }
    }
    if (leaf != NULL) X509_free(leaf);
    if (leaf_key != NULL) EVP_PKEY_free(leaf_key);
    X509_free(root);
    EVP_PKEY_free(root_key);
    return ok;
}

} // namespace
#endif

SdkTlsRotationPolicy::SdkTlsRotationPolicy()
    : leaf_validity_days(397), rotate_before_days(90), check_interval_seconds(6 * 60 * 60) {}

SdkTlsCertificatePaths SdkTlsCertificateManager::PathsFromConfig(const SdkTlsConfig& config) {
    SdkTlsCertificatePaths paths;
    paths.certificate_chain = config.certificate_chain_file;
    paths.private_key = config.private_key_file;
    const std::string::size_type separator = paths.certificate_chain.find_last_of("/\\");
    paths.tls_dir = separator == std::string::npos ? "." : paths.certificate_chain.substr(0, separator);
    paths.root_certificate = JoinPath(paths.tls_dir, "local-runtime-root-ca.crt");
    paths.root_private_key = JoinPath(paths.tls_dir, "local-runtime-root-ca.key.pem");
    paths.state = JoinPath(paths.tls_dir, "tls-state.json");
    return paths;
}

bool SdkTlsCertificateManager::Provision(const SdkTlsCertificatePaths& paths,
                                         const SdkTlsRotationPolicy& policy,
                                         bool force,
                                         std::string* error_message) {
#if SDK_OPEN_ENABLE_TLS
    if (!EnsureDirectoryRecursive(paths.tls_dir)) {
        SetError(error_message, "cannot create TLS directory: " + paths.tls_dir);
        return false;
    }
    TlsFileLock lock;
    bool busy = false;
    if (!lock.TryAcquire(paths.state + ".lock", &busy, error_message)) return false;
    if (busy) {
        SetError(error_message, "another SDK Open process is provisioning TLS credentials");
        return false;
    }
    const bool has_certificate = FileExists(paths.certificate_chain);
    const bool has_private_key = FileExists(paths.private_key);
    if (has_certificate != has_private_key) {
        SetError(error_message, "TLS certificate and private key must be replaced as a matching pair");
        return false;
    }
    if (!force && FileExists(paths.root_certificate) && FileExists(paths.root_private_key) &&
        has_certificate && has_private_key && IsManagedState(paths.state)) {
        return true;
    }
    const bool has_root_certificate = FileExists(paths.root_certificate);
    const bool has_root_private_key = FileExists(paths.root_private_key);
    if (has_root_certificate != has_root_private_key) {
        SetError(error_message, "managed TLS root certificate and private key are incomplete");
        return false;
    }
    if (!has_root_certificate) {
        EVP_PKEY* root_key = GenerateKey();
        X509* root = root_key == NULL ? NULL : CreateRootCertificate(root_key, 3650);
        if (root == NULL || !WritePrivateKey(root_key, paths.root_private_key, 0600, error_message) ||
            !WriteCertificate(root, paths.root_certificate, 0644, error_message)) {
            if (root != NULL) X509_free(root);
            if (root_key != NULL) EVP_PKEY_free(root_key);
            return false;
        }
        X509_free(root);
        EVP_PKEY_free(root_key);
    }
    if (has_certificate && has_private_key && !IsManagedState(paths.state)) {
        // A customer pair is preserved byte-for-byte.  Keep a state marker so
        // installers can distinguish it from a managed pair; the runtime will
        // never rotate a certificate in this mode.
        return WriteTextAtomically(paths.state, "{\"mode\":\"custom\"}\n", 0600, error_message);
    }
    std::string leaf_fingerprint;
    if (!BuildLeaf(paths, policy, &leaf_fingerprint, error_message)) return false;
    const std::string state = ManagedStateJson(paths, leaf_fingerprint);
    return WriteTextAtomically(paths.state, state, 0600, error_message);
#else
    (void)paths;
    (void)policy;
    (void)force;
    SetError(error_message, "sdk_open was built without TLS support");
    return false;
#endif
}

bool SdkTlsCertificateManager::RenewLeaf(const SdkTlsCertificatePaths& paths,
                                         const SdkTlsRotationPolicy& policy,
                                         std::string* error_message) {
#if SDK_OPEN_ENABLE_TLS
    if (!FileExists(paths.root_certificate) || !FileExists(paths.root_private_key)) {
        SetError(error_message, "managed TLS root certificate or private key is unavailable");
        return false;
    }
    if (!IsManagedLeaf(paths)) {
        // A customer may have replaced the leaf pair while retaining the
        // metadata created by an earlier installation.  Never overwrite it
        // during a package upgrade.
        return true;
    }
    TlsFileLock lock;
    bool busy = false;
    if (!lock.TryAcquire(paths.state + ".lock", &busy, error_message)) return false;
    if (busy) {
        SetError(error_message, "another SDK Open process is renewing TLS credentials");
        return false;
    }
    std::string leaf_fingerprint;
    if (!BuildLeaf(paths, policy, &leaf_fingerprint, error_message)) return false;
    const std::string state = ManagedStateJson(paths, leaf_fingerprint);
    return WriteTextAtomically(paths.state, state, 0600, error_message);
#else
    (void)paths;
    (void)policy;
    SetError(error_message, "sdk_open was built without TLS support");
    return false;
#endif
}

bool SdkTlsCertificateManager::RotateIfNeeded(const SdkTlsCertificatePaths& paths,
                                              const SdkTlsRotationPolicy& policy,
                                              bool* rotated,
                                              std::string* error_message) {
#if SDK_OPEN_ENABLE_TLS
    if (rotated != NULL) *rotated = false;
    if (!IsManagedLeaf(paths)) return true;
    TlsFileLock lock;
    bool busy = false;
    if (!lock.TryAcquire(paths.state + ".lock", &busy, error_message)) return false;
    if (busy) return true;
    if (!FileExists(paths.root_certificate) || !FileExists(paths.root_private_key) ||
        !FileExists(paths.certificate_chain) || !FileExists(paths.private_key)) {
        SetError(error_message, "managed TLS files are incomplete");
        return false;
    }
    if (!LeafNeedsRotation(paths.certificate_chain, policy.rotate_before_days)) return true;
    SDK_OPEN_LOG_INFO("[tls.rotation.started] certificate={}, rotate_before_days={}",
                      paths.certificate_chain, policy.rotate_before_days);
    std::string leaf_fingerprint;
    if (!BuildLeaf(paths, policy, &leaf_fingerprint, error_message)) {
        SDK_OPEN_LOG_ERROR("[tls.rotation.failed] certificate={}, error={}",
                           paths.certificate_chain, error_message == NULL ? "unknown" : *error_message);
        return false;
    }
    const std::string state = ManagedStateJson(paths, leaf_fingerprint);
    if (!WriteTextAtomically(paths.state, state, 0600, error_message)) {
        SDK_OPEN_LOG_ERROR("[tls.rotation.failed] state update failed, certificate={}", paths.certificate_chain);
        return false;
    }
    if (rotated != NULL) *rotated = true;
    SDK_OPEN_LOG_INFO("[tls.rotation.succeeded] certificate={}", paths.certificate_chain);
    return true;
#else
    (void)paths;
    (void)policy;
    if (rotated != NULL) *rotated = false;
    SetError(error_message, "sdk_open was built without TLS support");
    return false;
#endif
}

} // namespace sdk
} // namespace editor
