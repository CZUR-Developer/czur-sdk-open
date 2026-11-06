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
#include <iostream>
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

#if defined(__APPLE__) && SDK_OPEN_ENABLE_TLS
#include "sdk_tls_file_transaction.h"
#include "nlohmann/json.hpp"
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
        file_descriptor_ = ::open(lock_path.c_str(), O_RDWR | O_CREAT
#if defined(__APPLE__)
                                  | O_NOFOLLOW
#endif
                                  , 0600);
        if (file_descriptor_ < 0) {
            SetError(error_message, "cannot open TLS rotation lock: " + lock_path + ": " +
                                      std::strerror(errno));
            return false;
        }
        struct stat lock_stat;
        if (::fstat(file_descriptor_, &lock_stat) != 0 || !S_ISREG(lock_stat.st_mode)) {
            SetError(error_message, "invalid TLS lock: " + lock_path);
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
#if defined(__APPLE__)
    if (value == NULL) return false;
    const int fd = ::open(path.c_str(), O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return false;
    struct stat st;
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) { ::close(fd); return false; }
    value->clear();
    char data[16384];
    for (;;) {
        const ssize_t count = ::read(fd, data, sizeof(data));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) { ::close(fd); return false; }
        if (count == 0) break;
        value->append(data, static_cast<size_t>(count));
    }
    return ::close(fd) == 0;
#else
    std::ifstream input(path.c_str(), std::ios::in | std::ios::binary);
    if (!input.is_open() || value == NULL) {
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    *value = buffer.str();
    return input.good() || input.eof();
#endif
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
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC
#if defined(__APPLE__)
                          | O_NOFOLLOW
#endif
                          , static_cast<mode_t>(mode));
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
#if defined(__APPLE__)
    std::string contents;
    if (!ReadText(path, &contents)) return NULL;
    BIO* input = BIO_new_mem_buf(contents.data(), static_cast<int>(contents.size()));
#else
    BIO* input = BIO_new_file(path.c_str(), "rb");
#endif
    if (input == NULL) return NULL;
    X509* certificate = PEM_read_bio_X509(input, NULL, NULL, NULL);
    BIO_free(input);
    return certificate;
}

EVP_PKEY* ReadPrivateKey(const std::string& path) {
#if defined(__APPLE__)
    std::string contents;
    if (!ReadText(path, &contents)) return NULL;
    BIO* input = BIO_new_mem_buf(contents.data(), static_cast<int>(contents.size()));
#else
    BIO* input = BIO_new_file(path.c_str(), "rb");
#endif
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
        !SetExtension(certificate, 0, "subjectAltName", "DNS:sdk-runtime.localhost") ||
        X509_sign(certificate, root_key, EVP_sha256()) <= 0) {
        X509_free(certificate);
        return NULL;
    }
    return certificate;
}

#if defined(__APPLE__)
nlohmann::json ReadTlsState(const std::string& path) {
    std::string text;
    if (!ReadText(path, &text)) return nlohmann::json::object();
    nlohmann::json state = nlohmann::json::parse(text, NULL, false);
    return state.is_object() ? state : nlohmann::json::object();
}

std::string StateString(const nlohmann::json& state, const char* name) {
    const nlohmann::json::const_iterator value = state.find(name);
    return value != state.end() && value->is_string() ? value->get<std::string>() : std::string();
}

bool SignedByRoot(const SdkTlsCertificatePaths& paths);
#endif

bool IsManagedState(const std::string& path) {
#if defined(__APPLE__)
    return StateString(ReadTlsState(path), "mode") == "managed";
#else
    std::string state;
    return ReadText(path, &state) && state.find("\"mode\":\"managed\"") != std::string::npos;
#endif
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
#if defined(__APPLE__)
    const std::string expected_root = StateString(ReadTlsState(paths.state), "rootSha256");
    matches = matches && (expected_root.empty() || expected_root == RootFingerprint(paths.root_certificate)) &&
              SignedByRoot(paths);
#endif
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

#if defined(__APPLE__)
std::vector<std::string> ManagedFiles(const SdkTlsCertificatePaths& paths) {
    return {paths.root_certificate, paths.root_private_key, paths.certificate_chain,
            paths.private_key, paths.state};
}

bool CheckTlsFiles(const SdkTlsCertificatePaths& paths, bool private_directory,
                   std::string* error) {
    struct stat st;
    if (::lstat(paths.tls_dir.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
        SetError(error, "TLS directory is missing or not a real directory: " + paths.tls_dir);
        return false;
    }
    if (private_directory && ::chmod(paths.tls_dir.c_str(), 0700) != 0) {
        SetError(error, "cannot protect TLS directory: " + paths.tls_dir);
        return false;
    }
    const std::vector<std::string> files = ManagedFiles(paths);
    for (size_t i = 0; i < files.size(); ++i) {
        if (::lstat(files[i].c_str(), &st) == 0) {
            if (!S_ISREG(st.st_mode)) {
                SetError(error, "TLS member must be a regular file: " + files[i]);
                return false;
            }
            if (private_directory && ::chmod(files[i].c_str(),
                    (i == 1 || i == 3 || i == 4) ? 0600 : 0644) != 0) {
                SetError(error, "cannot protect TLS member: " + files[i]);
                return false;
            }
        } else if (errno != ENOENT) {
            SetError(error, "cannot inspect TLS member: " + files[i] + ": " + std::strerror(errno));
            return false;
        }
    }
    return true;
}

bool CertificateKeyMatches(const std::string& certificate, const std::string& key) {
    X509* cert = ReadCertificate(certificate);
    EVP_PKEY* private_key = ReadPrivateKey(key);
    const bool valid = cert != NULL && private_key != NULL && X509_check_private_key(cert, private_key) == 1;
    if (cert != NULL) X509_free(cert);
    if (private_key != NULL) EVP_PKEY_free(private_key);
    return valid;
}

bool SignedByRoot(const SdkTlsCertificatePaths& paths) {
    X509* leaf = ReadCertificate(paths.certificate_chain);
    X509* root = ReadCertificate(paths.root_certificate);
    EVP_PKEY* public_key = root == NULL ? NULL : X509_get_pubkey(root);
    const bool valid = leaf != NULL && root != NULL && public_key != NULL &&
        X509_check_ca(root) > 0 &&
        X509_NAME_cmp(X509_get_issuer_name(leaf), X509_get_subject_name(root)) == 0 &&
        X509_verify(leaf, public_key) == 1;
    if (public_key != NULL) EVP_PKEY_free(public_key);
    if (root != NULL) X509_free(root);
    if (leaf != NULL) X509_free(leaf);
    return valid;
}

bool ValidateTlsPair(const SdkTlsCertificatePaths& paths, bool managed, std::string* error,
                     bool allow_expired = false) {
    if (!CertificateKeyMatches(paths.certificate_chain, paths.private_key)) {
        SetError(error, "TLS certificate/private key unreadable or mismatched: " + paths.private_key);
        return false;
    }
    X509* leaf = ReadCertificate(paths.certificate_chain);
    const bool valid = leaf != NULL && X509_check_host(leaf, kHostName, 0, 0, NULL) == 1 &&
        X509_cmp_current_time(X509_get0_notBefore(leaf)) < 0 &&
        (allow_expired || X509_cmp_current_time(X509_get0_notAfter(leaf)) > 0) &&
        X509_check_purpose(leaf, X509_PURPOSE_SSL_SERVER, 0) == 1;
    if (leaf != NULL) X509_free(leaf);
    if (!valid) {
        SetError(error, "TLS certificate hostname, validity or SSL purpose is invalid: " + paths.certificate_chain);
        return false;
    }
    if (managed && (!CertificateKeyMatches(paths.root_certificate, paths.root_private_key) || !SignedByRoot(paths))) {
        SetError(error, "managed TLS CA/key or leaf signature is invalid: " + paths.tls_dir);
        return false;
    }
    return true;
}

std::string TextSha256(const std::string& path) {
    std::string text;
    if (!ReadText(path, &text)) return std::string();
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(text.data()), text.size(), digest);
    std::ostringstream result;
    result << std::hex;
    for (size_t i = 0; i < sizeof(digest); ++i) {
        result.width(2); result.fill('0'); result << static_cast<unsigned int>(digest[i]);
    }
    return result.str();
}

bool KnownLegacyLeaf(const std::string& path) {
    const std::string hash = TextSha256(path);
    return hash == "6519c52926b4cc3399b242dd44077f38b39457e080c56b6fe2f09a8af92ad32e" ||
           hash == "3b70d132fef144f6da10e732c8265029794757f1ebb258c9cb5541e4485b18b0";
}

bool KnownLegacyKey(const std::string& path) {
    return TextSha256(path) == "ceae324c3be1d43a19ea983c8bbd2f757001486265c7a6596ece42134f01cf11";
}

bool ReadTlsBundle(const SdkTlsCertificatePaths& source, const SdkTlsCertificatePaths& target,
                   std::vector<TlsTransactionFile>* bundle, std::string* error) {
    std::vector<std::string> sources = ManagedFiles(source);
    std::vector<std::string> targets = ManagedFiles(target);
    if (StateString(ReadTlsState(source.state), "mode") == "custom") {
        sources.erase(sources.begin(), sources.begin() + 2);
        targets.erase(targets.begin(), targets.begin() + 2);
    }
    bundle->clear();
    for (size_t i = 0; i < sources.size(); ++i) {
        TlsTransactionFile file;
        file.path = targets[i];
        file.mode = sources[i] == source.certificate_chain ? 0644 : 0600;
        if (!ReadText(sources[i], &file.contents)) {
            SetError(error, "cannot read TLS source before migration: " + sources[i] + ": " + std::strerror(errno));
            return false;
        }
        bundle->push_back(file);
    }
    return true;
}
std::string CertificatePem(X509* certificate) {
    BIO* memory = BIO_new(BIO_s_mem());
    if (memory == NULL) return std::string();
    std::string result;
    if (certificate != NULL && PEM_write_bio_X509(memory, certificate) == 1) {
        BUF_MEM* buffer = NULL;
        BIO_get_mem_ptr(memory, &buffer);
        result.assign(buffer->data, buffer->length);
    }
    BIO_free(memory);
    return result;
}

std::string PrivateKeyPem(EVP_PKEY* key) {
    BIO* memory = BIO_new(BIO_s_mem());
    if (memory == NULL) return std::string();
    std::string result;
    if (key != NULL && PEM_write_bio_PrivateKey(memory, key, NULL, NULL, 0, NULL, NULL) == 1) {
        BUF_MEM* buffer = NULL;
        BIO_get_mem_ptr(memory, &buffer);
        result.assign(buffer->data, buffer->length);
    }
    BIO_free(memory);
    return result;
}

bool InstallMacLeaf(const SdkTlsCertificatePaths& paths, const SdkTlsRotationPolicy& policy,
                    X509* root, EVP_PKEY* root_key, std::string* fingerprint, std::string* error) {
    if (root == NULL || root_key == NULL || X509_check_private_key(root, root_key) != 1) {
        SetError(error, "managed TLS CA and signing key do not match: " + paths.root_private_key);
        return false;
    }
    EVP_PKEY* key = GenerateKey();
    X509* leaf = key == NULL ? NULL : CreateLeafCertificate(key, root, root_key, policy.leaf_validity_days);
    const std::string root_hash = CertificateFingerprint(root);
    const std::string leaf_hash = CertificateFingerprint(leaf);
    const std::string state = "{\"mode\":\"managed\",\"leafValidityDays\":397,\"rotateBeforeDays\":90,\"leafSha256\":\"" +
        leaf_hash + "\",\"rootSha256\":\"" + root_hash + "\"}\n";
    const std::vector<std::string> contents = {CertificatePem(root), PrivateKeyPem(root_key),
        CertificatePem(leaf), PrivateKeyPem(key), state};
    const std::vector<std::string> paths_list = ManagedFiles(paths);
    bool valid = leaf != NULL && !root_hash.empty() && !leaf_hash.empty();
    std::vector<TlsTransactionFile> bundle;
    for (size_t i = 0; i < contents.size(); ++i) {
        valid = valid && !contents[i].empty();
        TlsTransactionFile file;
        file.path = paths_list[i]; file.contents = contents[i];
        file.mode = (i == 1 || i == 3 || i == 4) ? 0600 : 0644;
        bundle.push_back(file);
    }
    if (leaf != NULL) X509_free(leaf);
    if (key != NULL) EVP_PKEY_free(key);
    if (!valid) {
        SetError(error, "cannot generate complete managed TLS material: " + paths.tls_dir);
        return false;
    }
    if (!CommitTlsTransaction(paths.tls_dir, bundle, error)) return false;
    if (fingerprint != NULL) *fingerprint = leaf_hash;
    return true;
}

bool ValidateTlsBundle(const std::vector<TlsTransactionFile>& bundle, std::string* error) {
    const bool managed = bundle.size() == 5;
    const size_t leaf_index = managed ? 2 : 0;
    if (!managed && bundle.size() != 3) return false;
    BIO* leaf_input = BIO_new_mem_buf(bundle[leaf_index].contents.data(),
        static_cast<int>(bundle[leaf_index].contents.size()));
    BIO* key_input = BIO_new_mem_buf(bundle[leaf_index + 1].contents.data(),
        static_cast<int>(bundle[leaf_index + 1].contents.size()));
    X509* leaf = leaf_input == NULL ? NULL : PEM_read_bio_X509(leaf_input, NULL, NULL, NULL);
    EVP_PKEY* key = key_input == NULL ? NULL : PEM_read_bio_PrivateKey(key_input, NULL, NULL, NULL);
    if (leaf_input != NULL) BIO_free(leaf_input);
    if (key_input != NULL) BIO_free(key_input);
    bool valid = leaf != NULL && key != NULL && X509_check_private_key(leaf, key) == 1;
    if (managed) {
        BIO* root_input = BIO_new_mem_buf(bundle[0].contents.data(), static_cast<int>(bundle[0].contents.size()));
        BIO* root_key_input = BIO_new_mem_buf(bundle[1].contents.data(), static_cast<int>(bundle[1].contents.size()));
        X509* root = root_input == NULL ? NULL : PEM_read_bio_X509(root_input, NULL, NULL, NULL);
        EVP_PKEY* root_key = root_key_input == NULL ? NULL : PEM_read_bio_PrivateKey(root_key_input, NULL, NULL, NULL);
        if (root_input != NULL) BIO_free(root_input);
        if (root_key_input != NULL) BIO_free(root_key_input);
        const nlohmann::json state = nlohmann::json::parse(bundle[4].contents, NULL, false);
        valid = valid && root != NULL && root_key != NULL && state.is_object() &&
            X509_check_private_key(root, root_key) == 1 && X509_check_ca(root) > 0 &&
            X509_verify(leaf, root_key) == 1 &&
            StateString(state, "mode") == "managed" &&
            StateString(state, "leafSha256") == CertificateFingerprint(leaf) &&
            StateString(state, "rootSha256") == CertificateFingerprint(root);
        if (root != NULL) X509_free(root);
        if (root_key != NULL) EVP_PKEY_free(root_key);
    }
    if (leaf != NULL) X509_free(leaf);
    if (key != NULL) EVP_PKEY_free(key);
    if (!valid) SetError(error, "TLS source changed or is invalid; staged certificate/key set does not match");
    return valid;
}

#endif

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
#if defined(__APPLE__)
    const bool result = InstallMacLeaf(paths, policy, root, root_key, leaf_fingerprint, error_message);
    X509_free(root);
    EVP_PKEY_free(root_key);
    return result;
#else
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
#endif
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

#if defined(__APPLE__)
bool SdkTlsCertificateManager::PrepareMacCredentials(const SdkTlsCertificatePaths& system,
                                                      const SdkTlsCertificatePaths& user,
                                                      std::string* error_message) {
#if SDK_OPEN_ENABLE_TLS
    if (!EnsureDirectoryRecursive(user.tls_dir) || !CheckTlsFiles(user, true, error_message)) return false;
    TlsFileLock lock;
    bool busy = false;
    if (!lock.TryAcquire(user.state + ".lock", &busy, error_message)) return false;
    if (busy) {
        SetError(error_message, "another SDK Open process is preparing TLS credentials");
        return false;
    }
    if (!RecoverTlsTransaction(user.tls_dir, ManagedFiles(user), error_message) ||
        !CheckTlsFiles(user, true, error_message)) return false;

    const nlohmann::json user_state = ReadTlsState(user.state);
    const std::string mode = StateString(user_state, "mode");
    const bool has_leaf = FileExists(user.certificate_chain);
    const bool has_key = FileExists(user.private_key);
    const std::string user_leaf = RootFingerprint(user.certificate_chain);
    const std::string user_root = RootFingerprint(user.root_certificate);
    const bool tracked_leaf = !user_leaf.empty() &&
        StateString(user_state, "leafSha256") == user_leaf;
    const bool tracked_root = !user_root.empty() &&
        StateString(user_state, "rootSha256") == user_root &&
        CertificateKeyMatches(user.root_certificate, user.root_private_key);
    const bool legacy_leaf = KnownLegacyLeaf(user.certificate_chain);
    const bool legacy_key = KnownLegacyKey(user.private_key);
    const bool legacy = legacy_leaf && (!has_key || legacy_key);
    const bool empty = !has_leaf && !has_key && !FileExists(user.state) &&
        !FileExists(user.root_certificate) && !FileExists(user.root_private_key);

    // Explicit custom mode always wins. Metadata alone never grants permission
    // to replace an unknown customer certificate.
    if (mode == "custom") return ValidateTlsPair(user, false, error_message);

    if (!CheckTlsFiles(system, false, error_message)) return false;
    const std::string system_leaf = RootFingerprint(system.certificate_chain);
    const bool partial_system_leaf = !system_leaf.empty() && user_leaf == system_leaf &&
        (legacy_key || (mode == "managed" && tracked_root));
    const bool managed = mode == "managed" &&
        (((tracked_leaf || tracked_root) && SignedByRoot(user)) ||
         (!has_leaf && tracked_root) || partial_system_leaf);

    if (!empty && !legacy && !managed && !partial_system_leaf) {
        // A valid customer replacement with stale managed metadata becomes
        // explicitly custom, so a later managed check cannot rotate it.
        if (!ValidateTlsPair(user, false, error_message)) return false;
        if (mode == "managed") {
            TlsTransactionFile state;
            state.path = user.state; state.contents = "{\"mode\":\"custom\"}\n"; state.mode = 0600;
            return CommitTlsTransaction(user.tls_dir, {state}, error_message);
        }
        return true;
    }

    const nlohmann::json system_state = ReadTlsState(system.state);
    const bool system_managed = StateString(system_state, "mode") == "managed";
    if (!ValidateTlsPair(system, system_managed, error_message)) return false;
    const std::string system_root = RootFingerprint(system.root_certificate);
    if (system_managed && (StateString(system_state, "leafSha256") != system_leaf ||
                          StateString(system_state, "rootSha256") != system_root)) {
        SetError(error_message, "system managed TLS fingerprints do not match installed files: " + system.state);
        return false;
    }

    if (managed && tracked_root && user_root == system_root &&
        CertificateKeyMatches(user.certificate_chain, user.private_key) && SignedByRoot(user) &&
        ValidateTlsPair(user, true, error_message, true)) {
        if (!tracked_leaf) {
            TlsTransactionFile state;
            state.path = user.state;
            state.contents = ManagedStateJson(user, user_leaf);
            state.mode = 0600;
            if (!CommitTlsTransaction(user.tls_dir, {state}, error_message)) return false;
        }
        // Preserve a newer per-user leaf. Startup's RotateIfNeeded performs
        // the expiry check under this same lock before transport initialization.
        std::cerr << "[tls.prepare.preserved] root=" << user_root << " leaf=" << user_leaf << std::endl;
        return true;
    }

    std::vector<TlsTransactionFile> bundle;
    if (!ReadTlsBundle(system, user, &bundle, error_message) ||
        !ValidateTlsBundle(bundle, error_message)) return false;
    const char* reason = empty ? "first-install" : legacy ? "legacy-default" : "managed-repair";
    std::cerr << "[tls.prepare.started] reason=" << reason << " root=" << system_root
              << " leaf=" << system_leaf << std::endl;
    if (!CommitTlsTransaction(user.tls_dir, bundle, error_message)) return false;
    std::cerr << "[tls.prepare.succeeded] reason=" << reason << std::endl;
    return true;
#else
    (void)system; (void)user;
    SetError(error_message, "sdk_open was built without TLS support");
    return false;
#endif
}
#endif

bool SdkTlsCertificateManager::Provision(const SdkTlsCertificatePaths& paths,
                                         const SdkTlsRotationPolicy& policy,
                                         bool force,
                                         std::string* error_message) {
#if SDK_OPEN_ENABLE_TLS
    if (!EnsureDirectoryRecursive(paths.tls_dir)) {
        SetError(error_message, "cannot create TLS directory: " + paths.tls_dir);
        return false;
    }
#if defined(__APPLE__)
    if (!CheckTlsFiles(paths, false, error_message)) return false;
#endif
    TlsFileLock lock;
    bool busy = false;
    if (!lock.TryAcquire(paths.state + ".lock", &busy, error_message)) return false;
    if (busy) {
        SetError(error_message, "another SDK Open process is provisioning TLS credentials");
        return false;
    }
#if defined(__APPLE__)
    if (!CheckTlsFiles(paths, true, error_message) ||
        !RecoverTlsTransaction(paths.tls_dir, ManagedFiles(paths), error_message)) return false;
#endif
    const bool has_certificate = FileExists(paths.certificate_chain);
    const bool has_private_key = FileExists(paths.private_key);
    if (has_certificate != has_private_key) {
        SetError(error_message, "TLS certificate and private key must be replaced as a matching pair");
        return false;
    }
    if (!force && FileExists(paths.root_certificate) && FileExists(paths.root_private_key) &&
        has_certificate && has_private_key && IsManagedState(paths.state)) {
#if defined(__APPLE__)
        if (!ValidateTlsPair(paths, true, error_message)) return false;
        if (!IsManagedLeaf(paths)) {
            SetError(error_message, "managed TLS metadata does not match installed credentials: " + paths.state);
            return false;
        }
        return true;
#else
        return true;
#endif
    }
#if defined(__APPLE__)
    if (has_certificate && has_private_key && !IsManagedState(paths.state)) {
        if (!ValidateTlsPair(paths, false, error_message)) return false;
        std::vector<TlsTransactionFile> custom;
        const std::vector<std::string> names = {paths.certificate_chain, paths.private_key, paths.state};
        for (size_t i = 0; i < names.size(); ++i) {
            TlsTransactionFile file;
            file.path = names[i]; file.mode = i == 0 ? 0644 : 0600;
            if (i == 2) file.contents = "{\"mode\":\"custom\"}\n";
            else if (!ReadText(names[i], &file.contents)) return false;
            custom.push_back(file);
        }
        return CommitTlsTransaction(paths.tls_dir, custom, error_message);
    }
#endif
    const bool has_root_certificate = FileExists(paths.root_certificate);
    const bool has_root_private_key = FileExists(paths.root_private_key);
    if (has_root_certificate != has_root_private_key) {
        SetError(error_message, "managed TLS root certificate and private key are incomplete");
        return false;
    }
#if defined(__APPLE__)
    if (!has_root_certificate) {
        EVP_PKEY* root_key = GenerateKey();
        X509* root = root_key == NULL ? NULL : CreateRootCertificate(root_key, 3650);
        const bool result = InstallMacLeaf(paths, policy, root, root_key, NULL, error_message);
        if (root != NULL) X509_free(root);
        if (root_key != NULL) EVP_PKEY_free(root_key);
        return result;
    }
#else
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
#endif
    std::string leaf_fingerprint;
    if (!BuildLeaf(paths, policy, &leaf_fingerprint, error_message)) return false;
#if defined(__APPLE__)
    return true;
#else
    const std::string state = ManagedStateJson(paths, leaf_fingerprint);
    return WriteTextAtomically(paths.state, state, 0600, error_message);
#endif
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
#if !defined(__APPLE__)
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
#endif
#if defined(__APPLE__)
    if (!CheckTlsFiles(paths, false, error_message)) return false;
#endif
    TlsFileLock lock;
    bool busy = false;
    if (!lock.TryAcquire(paths.state + ".lock", &busy, error_message)) return false;
    if (busy) {
        SetError(error_message, "another SDK Open process is renewing TLS credentials");
        return false;
    }
#if defined(__APPLE__)
    if (!CheckTlsFiles(paths, true, error_message) ||
        !RecoverTlsTransaction(paths.tls_dir, ManagedFiles(paths), error_message)) return false;
#endif
#if defined(__APPLE__)
    if (!IsManagedState(paths.state)) return ValidateTlsPair(paths, false, error_message);
    if (!FileExists(paths.root_certificate) || !FileExists(paths.root_private_key)) {
        SetError(error_message, "managed TLS root certificate or private key is unavailable");
        return false;
    }
    if (!IsManagedLeaf(paths)) {
        if (IsManagedState(paths.state)) {
            SetError(error_message, "managed TLS metadata/signature mismatch; credentials preserved: " + paths.state);
            return false;
        }
        return ValidateTlsPair(paths, false, error_message);
    }
#endif
    std::string leaf_fingerprint;
    if (!BuildLeaf(paths, policy, &leaf_fingerprint, error_message)) return false;
#if defined(__APPLE__)
    return true;
#else
    const std::string state = ManagedStateJson(paths, leaf_fingerprint);
    return WriteTextAtomically(paths.state, state, 0600, error_message);
#endif
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
#if !defined(__APPLE__)
    if (!IsManagedLeaf(paths)) return true;
#endif
#if defined(__APPLE__)
    if (!CheckTlsFiles(paths, false, error_message)) return false;
#endif
#if defined(__APPLE__)
    struct stat transaction_stat;
    if (!IsManagedState(paths.state) &&
        ::lstat(JoinPath(paths.tls_dir, ".tls-update").c_str(), &transaction_stat) != 0 && errno == ENOENT) {
        return ValidateTlsPair(paths, false, error_message);
    }
#endif
    TlsFileLock lock;
    bool busy = false;
    if (!lock.TryAcquire(paths.state + ".lock", &busy, error_message)) return false;
    if (busy) {
#if defined(__APPLE__)
        SetError(error_message, "another SDK Open process is updating TLS credentials");
        return false;
#else
        return true;
#endif
    }
#if defined(__APPLE__)
    if (!CheckTlsFiles(paths, true, error_message) ||
        !RecoverTlsTransaction(paths.tls_dir, ManagedFiles(paths), error_message)) return false;
#endif
#if defined(__APPLE__)
    if (!IsManagedLeaf(paths)) {
        if (IsManagedState(paths.state)) {
            SetError(error_message, "managed TLS metadata/signature mismatch; credentials preserved: " + paths.state);
            return false;
        }
        return ValidateTlsPair(paths, false, error_message);
    }
#endif
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
#if !defined(__APPLE__)
    const std::string state = ManagedStateJson(paths, leaf_fingerprint);
    if (!WriteTextAtomically(paths.state, state, 0600, error_message)) {
        SDK_OPEN_LOG_ERROR("[tls.rotation.failed] state update failed, certificate={}", paths.certificate_chain);
        return false;
    }
#endif
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
