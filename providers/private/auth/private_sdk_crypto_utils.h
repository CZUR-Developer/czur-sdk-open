// Copyright (c) 2026 CZUR Tech. All rights reserved.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace editor {
namespace sdk {
namespace private_impl {

bool RandomBytes(std::size_t size, std::vector<unsigned char>* out, std::string* err = NULL);
bool Aes256CbcEncrypt(const std::vector<unsigned char>& key,
                      const std::vector<unsigned char>& iv,
                      const std::string& plaintext,
                      std::vector<unsigned char>* ciphertext,
                      std::string* err = NULL);
bool Aes256CbcDecrypt(const std::vector<unsigned char>& key,
                      const std::vector<unsigned char>& iv,
                      const std::vector<unsigned char>& ciphertext,
                      std::string* plaintext,
                      std::string* err = NULL);
bool HmacSha256(const std::vector<unsigned char>& key,
                const std::string& data,
                std::vector<unsigned char>* mac,
                std::string* err = NULL);
std::vector<unsigned char> HkdfSha256(const std::string& input_key_material,
                                      const std::string& info,
                                      std::size_t output_size);
std::string Base64UrlEncode(const std::vector<unsigned char>& input);
bool Base64UrlDecode(const std::string& input, std::vector<unsigned char>* output, std::string* err = NULL);
std::string HexLower(const unsigned char* data, std::size_t size);
bool ConstantTimeEquals(const std::string& lhs, const std::string& rhs);

} // namespace private_impl
} // namespace sdk
} // namespace editor
