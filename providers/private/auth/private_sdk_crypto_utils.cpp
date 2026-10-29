// Copyright (c) 2026 CZUR Tech. All rights reserved.

#include "private_sdk_crypto_utils.h"

#include <algorithm>
#include <cstring>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

namespace editor {
namespace sdk {
namespace private_impl {

namespace {

void SetErr(std::string* err, const std::string& message) {
    if (err != NULL) {
        *err = message;
    }
}

} // namespace

bool RandomBytes(std::size_t size, std::vector<unsigned char>* out, std::string* err) {
    if (out == NULL) {
        SetErr(err, "missing output buffer");
        return false;
    }
    out->assign(size, 0);
    if (size == 0) {
        return true;
    }
    if (RAND_bytes(out->data(), static_cast<int>(size)) != 1) {
        out->clear();
        SetErr(err, "RAND_bytes failed");
        return false;
    }
    return true;
}

bool Aes256CbcEncrypt(const std::vector<unsigned char>& key,
                      const std::vector<unsigned char>& iv,
                      const std::string& plaintext,
                      std::vector<unsigned char>* ciphertext,
                      std::string* err) {
    if (ciphertext == NULL) {
        SetErr(err, "missing ciphertext output");
        return false;
    }
    if (key.size() != 32 || iv.size() != 16) {
        SetErr(err, "invalid aes key or iv size");
        return false;
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) {
        SetErr(err, "EVP_CIPHER_CTX_new failed");
        return false;
    }

    bool ok = false;
    ciphertext->assign(plaintext.size() + EVP_MAX_BLOCK_LENGTH, 0);
    int update_len = 0;
    int final_len = 0;
    do {
        if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key.data(), iv.data()) != 1) {
            SetErr(err, "EVP_EncryptInit_ex failed");
            break;
        }
        if (EVP_EncryptUpdate(ctx,
                              ciphertext->data(),
                              &update_len,
                              reinterpret_cast<const unsigned char*>(plaintext.data()),
                              static_cast<int>(plaintext.size())) != 1) {
            SetErr(err, "EVP_EncryptUpdate failed");
            break;
        }
        if (EVP_EncryptFinal_ex(ctx, ciphertext->data() + update_len, &final_len) != 1) {
            SetErr(err, "EVP_EncryptFinal_ex failed");
            break;
        }
        ciphertext->resize(static_cast<std::size_t>(update_len + final_len));
        ok = true;
    } while (false);

    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        ciphertext->clear();
    }
    return ok;
}

bool Aes256CbcDecrypt(const std::vector<unsigned char>& key,
                      const std::vector<unsigned char>& iv,
                      const std::vector<unsigned char>& ciphertext,
                      std::string* plaintext,
                      std::string* err) {
    if (plaintext == NULL) {
        SetErr(err, "missing plaintext output");
        return false;
    }
    if (key.size() != 32 || iv.size() != 16) {
        SetErr(err, "invalid aes key or iv size");
        return false;
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) {
        SetErr(err, "EVP_CIPHER_CTX_new failed");
        return false;
    }

    bool ok = false;
    std::vector<unsigned char> output(ciphertext.size() + EVP_MAX_BLOCK_LENGTH, 0);
    int update_len = 0;
    int final_len = 0;
    do {
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key.data(), iv.data()) != 1) {
            SetErr(err, "EVP_DecryptInit_ex failed");
            break;
        }
        if (EVP_DecryptUpdate(ctx,
                              output.data(),
                              &update_len,
                              ciphertext.data(),
                              static_cast<int>(ciphertext.size())) != 1) {
            SetErr(err, "EVP_DecryptUpdate failed");
            break;
        }
        if (EVP_DecryptFinal_ex(ctx, output.data() + update_len, &final_len) != 1) {
            SetErr(err, "EVP_DecryptFinal_ex failed");
            break;
        }
        output.resize(static_cast<std::size_t>(update_len + final_len));
        plaintext->assign(output.begin(), output.end());
        ok = true;
    } while (false);

    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        plaintext->clear();
    }
    return ok;
}

bool HmacSha256(const std::vector<unsigned char>& key,
                const std::string& data,
                std::vector<unsigned char>* mac,
                std::string* err) {
    if (mac == NULL) {
        SetErr(err, "missing mac output");
        return false;
    }
    unsigned int mac_len = 0;
    mac->assign(EVP_MAX_MD_SIZE, 0);
    unsigned char* result = HMAC(EVP_sha256(),
                                 key.empty() ? NULL : key.data(),
                                 static_cast<int>(key.size()),
                                 reinterpret_cast<const unsigned char*>(data.data()),
                                 data.size(),
                                 mac->data(),
                                 &mac_len);
    if (result == NULL) {
        mac->clear();
        SetErr(err, "HMAC failed");
        return false;
    }
    mac->resize(mac_len);
    return true;
}

std::vector<unsigned char> HkdfSha256(const std::string& input_key_material,
                                      const std::string& info,
                                      std::size_t output_size) {
    const std::size_t hash_len = SHA256_DIGEST_LENGTH;
    std::vector<unsigned char> salt(hash_len, 0);
    std::vector<unsigned char> prk(EVP_MAX_MD_SIZE, 0);
    unsigned int prk_len = 0;
    HMAC(EVP_sha256(),
         salt.data(),
         static_cast<int>(salt.size()),
         reinterpret_cast<const unsigned char*>(input_key_material.data()),
         input_key_material.size(),
         prk.data(),
         &prk_len);
    prk.resize(prk_len);

    std::vector<unsigned char> okm;
    okm.reserve(output_size);
    std::vector<unsigned char> previous;
    unsigned char counter = 1;

    while (okm.size() < output_size) {
        std::string buffer;
        buffer.reserve(previous.size() + info.size() + 1);
        if (!previous.empty()) {
            buffer.append(reinterpret_cast<const char*>(previous.data()), previous.size());
        }
        buffer.append(info);
        buffer.push_back(static_cast<char>(counter));

        std::vector<unsigned char> block(EVP_MAX_MD_SIZE, 0);
        unsigned int block_len = 0;
        HMAC(EVP_sha256(),
             prk.data(),
             static_cast<int>(prk.size()),
             reinterpret_cast<const unsigned char*>(buffer.data()),
             buffer.size(),
             block.data(),
             &block_len);
        block.resize(block_len);

        const std::size_t remaining = output_size - okm.size();
        okm.insert(okm.end(), block.begin(), block.begin() + std::min(remaining, block.size()));
        previous = block;
        ++counter;
    }
    return okm;
}

std::string Base64UrlEncode(const std::vector<unsigned char>& input) {
    if (input.empty()) {
        return "";
    }
    std::string encoded(4 * ((input.size() + 2) / 3), '\0');
    const int output_len = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(&encoded[0]),
                                           input.data(),
                                           static_cast<int>(input.size()));
    encoded.resize(static_cast<std::size_t>(output_len));
    std::replace(encoded.begin(), encoded.end(), '+', '-');
    std::replace(encoded.begin(), encoded.end(), '/', '_');
    while (!encoded.empty() && encoded[encoded.size() - 1] == '=') {
        encoded.erase(encoded.size() - 1);
    }
    return encoded;
}

bool Base64UrlDecode(const std::string& input, std::vector<unsigned char>* output, std::string* err) {
    if (output == NULL) {
        SetErr(err, "missing output buffer");
        return false;
    }
    std::string normalized = input;
    std::replace(normalized.begin(), normalized.end(), '-', '+');
    std::replace(normalized.begin(), normalized.end(), '_', '/');
    while ((normalized.size() % 4) != 0) {
        normalized.push_back('=');
    }

    std::vector<unsigned char> decoded((normalized.size() / 4) * 3, 0);
    const int decoded_len = EVP_DecodeBlock(decoded.data(),
                                            reinterpret_cast<const unsigned char*>(normalized.data()),
                                            static_cast<int>(normalized.size()));
    if (decoded_len < 0) {
        output->clear();
        SetErr(err, "base64 decode failed");
        return false;
    }

    std::size_t padding = 0;
    if (!normalized.empty() && normalized[normalized.size() - 1] == '=') {
        ++padding;
    }
    if (normalized.size() > 1 && normalized[normalized.size() - 2] == '=') {
        ++padding;
    }

    if (static_cast<std::size_t>(decoded_len) < padding) {
        output->clear();
        SetErr(err, "base64 decoded length invalid");
        return false;
    }

    decoded.resize(static_cast<std::size_t>(decoded_len) - padding);
    *output = decoded;
    return true;
}

std::string HexLower(const unsigned char* data, std::size_t size) {
    static const char kHex[] = "0123456789abcdef";
    std::string result;
    result.resize(size * 2);
    for (std::size_t i = 0; i < size; ++i) {
        const unsigned char value = data[i];
        result[i * 2] = kHex[(value >> 4) & 0x0f];
        result[i * 2 + 1] = kHex[value & 0x0f];
    }
    return result;
}

bool ConstantTimeEquals(const std::string& lhs, const std::string& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    unsigned char diff = 0;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        diff |= static_cast<unsigned char>(lhs[i] ^ rhs[i]);
    }
    return diff == 0;
}

} // namespace private_impl
} // namespace sdk
} // namespace editor
