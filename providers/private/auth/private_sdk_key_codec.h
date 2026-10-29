// Copyright (c) 2026 CZUR Tech. All rights reserved.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "sdk_auth_types.h"

namespace editor {
namespace sdk {
namespace private_impl {

struct ApiKeySpec {
    SdkAccountType account_type;
    std::string license_mode;
    std::string host_auth_mode;
    bool commercial_authorized = false;
    std::vector<SdkDeviceGrant> device_scope;
    std::int64_t issued_at;
    std::int64_t expires_at;
    std::string key_id;
    std::string nonce;
};

struct ParsedApiKey {
    int code;
    std::string message;
    std::string api_key;
    std::string tier_short;
    std::string key_id;
    std::string nonce;
    std::string license_mode;
    std::string token_digest;
    std::int64_t issued_at;
    AuthContext auth_context;

    ParsedApiKey() : code(0), issued_at(0) {}
};

class PrivateSdkKeyCodec {
public:
    PrivateSdkKeyCodec();

    bool GenerateApiKey(const ApiKeySpec& spec, std::string* api_key, std::string* err = NULL) const;
    ParsedApiKey ParseAndValidateApiKey(const std::string& api_key, std::int64_t now_ts) const;
    void ParseAndValidateApiKey(const std::string& api_key, std::int64_t now_ts, ParsedApiKey* parsed) const;

private:
    bool BuildPayload(const ApiKeySpec& spec, nlohmann::json* payload_json, std::string* err) const;
    std::vector<SdkDeviceGrant> NormalizeDeviceScope(const std::vector<SdkDeviceGrant>& device_scope) const;

    std::vector<unsigned char> enc_key_;
    std::vector<unsigned char> sig_key_;
};

} // namespace private_impl
} // namespace sdk
} // namespace editor
