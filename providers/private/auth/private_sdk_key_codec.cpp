// Copyright (c) 2026 CZUR Tech. All rights reserved.

#include "private_sdk_key_codec.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <utility>

#include "private_sdk_crypto_utils.h"
#include "private_sdk_master_secret.h"
#include "private_sdk_support.h"
#include "support/private_sdk_logger.h"

namespace editor {
namespace sdk {
namespace private_impl {

namespace {

const char kApiKeyPrefix[] = "sk-";
const char kApiKeyVersion[] = "v1";
const char kPluginScene[] = "plugin";
const char kOfflineApiKeyMode[] = "offline_api_key";
const char kOnlineApiKeyMode[] = "online_api_key";
const char kHostAuthModeNone[] = "none";
const char kHostAuthModeActivationRequired[] = "activation_required";
const char kApiKeyEncInfo[] = "czur-sdk-apikey-v1-enc";
const char kApiKeySigInfo[] = "czur-sdk-apikey-v1-sig";
const std::size_t kSignatureHexLength = 32;
const std::size_t kSignatureBinaryLength = 16;
const std::size_t kAesKeyLength = 32;
const std::size_t kAesIvLength = 16;

std::string ShortDigestString(const std::string& value) {
    if (value.empty()) {
        return "empty";
    }
    const std::string digest = Sha256HexString(value);
    return digest.size() > 12 ? digest.substr(0, 12) : digest;
}

std::string TokenDigestForLog(const std::string& api_key) {
    return ShortDigestString(api_key);
}

std::string MasterSecretDigestForLog() {
    return ShortDigestString(GetPrivateSdkMasterSecret());
}

void LogApiKeyParseFail(const std::string& reason,
                        const std::string& api_key,
                        const std::string& tier_short,
                        std::size_t payload_length,
                        const std::string& detail = std::string()) {
    SDK_PRIVATE_LOG_WARN("auth.api_key parse_fail reason={} token_digest={} token_len={} tier={} payload_len={} master_secret_source={} master_secret_digest={}{}{}",
                         reason,
                         TokenDigestForLog(api_key),
                         api_key.size(),
                         tier_short.empty() ? "unknown" : tier_short,
                         payload_length,
                         GetPrivateSdkMasterSecretSource(),
                         MasterSecretDigestForLog(),
                         detail.empty() ? "" : " detail=",
                         detail.empty() ? "" : detail);
}

bool IsHexLowerString(const std::string& value) {
    if (value.empty()) {
        return false;
    }
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char ch = value[i];
        const bool is_digit = (ch >= '0' && ch <= '9');
        const bool is_hex_alpha = (ch >= 'a' && ch <= 'f');
        if (!is_digit && !is_hex_alpha) {
            return false;
        }
    }
    return true;
}

bool IsSupportedLicenseMode(const std::string& license_mode) {
    return license_mode == kOfflineApiKeyMode || license_mode == kOnlineApiKeyMode;
}

bool IsSupportedHostAuthMode(const std::string& host_auth_mode) {
    return host_auth_mode == kHostAuthModeNone || host_auth_mode == kHostAuthModeActivationRequired;
}

bool TryShortTierToAccountType(const std::string& short_tier, SdkAccountType* account_type) {
    if (account_type == NULL) {
        return false;
    }
    if (short_tier == "sr") {
        *account_type = SdkAccountType::Vip;
        return true;
    }
    if (short_tier == "sq") {
        *account_type = SdkAccountType::Svip;
        return true;
    }
    if (short_tier == "sp") {
        *account_type = SdkAccountType::SvipPlus;
        return true;
    }
    return false;
}

bool TryAccountTypeToShortTier(SdkAccountType account_type, std::string* short_tier) {
    if (short_tier == NULL) {
        return false;
    }
    switch (account_type) {
        case SdkAccountType::Vip:
            *short_tier = "sr";
            return true;
        case SdkAccountType::Svip:
            *short_tier = "sq";
            return true;
        case SdkAccountType::SvipPlus:
            *short_tier = "sp";
            return true;
        default:
            return false;
    }
}

std::int64_t NormalizeNowTs(std::int64_t now_ts) {
    return now_ts > 0 ? now_ts : static_cast<std::int64_t>(std::time(NULL));
}

std::string BuildSignatureInput(const std::string& short_tier, const std::string& payload) {
    return std::string(kApiKeyPrefix) + short_tier + "-" + kApiKeyVersion + "-" + payload;
}

} // namespace

PrivateSdkKeyCodec::PrivateSdkKeyCodec() {
    const std::string master_secret = GetPrivateSdkMasterSecret();
    SDK_PRIVATE_LOG_INFO("auth.api_key codec_init master_secret_source={} master_secret_configured={} master_secret_digest={}",
                         GetPrivateSdkMasterSecretSource(),
                         IsPrivateSdkMasterSecretEnvConfigured(),
                         ShortDigestString(master_secret));
    enc_key_ = HkdfSha256(master_secret, kApiKeyEncInfo, kAesKeyLength);
    sig_key_ = HkdfSha256(master_secret, kApiKeySigInfo, kAesKeyLength);
}

bool PrivateSdkKeyCodec::GenerateApiKey(const ApiKeySpec& spec, std::string* api_key, std::string* err) const {
    if (api_key == NULL) {
        if (err != NULL) {
            *err = "missing api_key output";
        }
        return false;
    }

    nlohmann::json payload_json;
    if (!BuildPayload(spec, &payload_json, err)) {
        return false;
    }

    std::string short_tier;
    if (!TryAccountTypeToShortTier(spec.account_type, &short_tier)) {
        if (err != NULL) {
            *err = "unsupported account type";
        }
        return false;
    }

    std::vector<unsigned char> iv;
    if (!RandomBytes(kAesIvLength, &iv, err)) {
        return false;
    }

    std::vector<unsigned char> ciphertext;
    if (!Aes256CbcEncrypt(enc_key_, iv, payload_json.dump(), &ciphertext, err)) {
        return false;
    }

    std::vector<unsigned char> payload_binary = iv;
    payload_binary.insert(payload_binary.end(), ciphertext.begin(), ciphertext.end());
    const std::string payload = Base64UrlEncode(payload_binary);
    const std::string sign_input = BuildSignatureInput(short_tier, payload);

    std::vector<unsigned char> mac;
    if (!HmacSha256(sig_key_, sign_input, &mac, err)) {
        return false;
    }

    *api_key = sign_input + HexLower(mac.data(), kSignatureBinaryLength);
    return true;
}

ParsedApiKey PrivateSdkKeyCodec::ParseAndValidateApiKey(const std::string& api_key, std::int64_t now_ts) const {
    ParsedApiKey parsed;
    ParseAndValidateApiKey(api_key, now_ts, &parsed);
    return parsed;
}

void PrivateSdkKeyCodec::ParseAndValidateApiKey(const std::string& api_key,
                                                std::int64_t now_ts,
                                                ParsedApiKey* parsed_output) const {
    if (parsed_output == NULL) {
        return;
    }
    ParsedApiKey& parsed = *parsed_output;
    parsed = ParsedApiKey();
    parsed.api_key = api_key;
    parsed.code = 1101;
    parsed.message = "api key invalid";

    const std::string prefix = kApiKeyPrefix;
    if (api_key.compare(0, prefix.size(), prefix) != 0) {
        LogApiKeyParseFail("prefix_mismatch", api_key, std::string(), 0);
        return;
    }
    if (api_key.size() <= prefix.size() + 2 + 1 + 2 + 1 + kSignatureHexLength) {
        LogApiKeyParseFail("too_short", api_key, std::string(), 0);
        return;
    }

    const std::string short_tier = api_key.substr(prefix.size(), 2);
    if (api_key.compare(prefix.size() + 2, 4, "-v1-") != 0) {
        LogApiKeyParseFail("version_mismatch", api_key, short_tier, 0);
        return;
    }
    parsed.tier_short = short_tier;

    const std::size_t payload_begin = prefix.size() + 2 + 4;
    const std::size_t payload_end = api_key.size() - kSignatureHexLength;
    const std::string payload = api_key.substr(payload_begin, payload_end - payload_begin);
    const std::string signature = api_key.substr(payload_end);
    if (payload.empty() || signature.size() != kSignatureHexLength || !IsHexLowerString(signature)) {
        LogApiKeyParseFail("signature_format_invalid", api_key, short_tier, payload.size());
        return;
    }

    SdkAccountType short_tier_account_type = SdkAccountType::Unknown;
    if (!TryShortTierToAccountType(short_tier, &short_tier_account_type)) {
        LogApiKeyParseFail("unsupported_tier", api_key, short_tier, payload.size());
        return;
    }

    std::vector<unsigned char> mac;
    if (!HmacSha256(sig_key_, BuildSignatureInput(short_tier, payload), &mac, NULL)) {
        parsed.code = 1900;
        parsed.message = "internal error";
        LogApiKeyParseFail("hmac_failed", api_key, short_tier, payload.size());
        return;
    }
    const std::string expected_signature = HexLower(mac.data(), kSignatureBinaryLength);
    if (!ConstantTimeEquals(signature, expected_signature)) {
        LogApiKeyParseFail("signature_mismatch", api_key, short_tier, payload.size());
        return;
    }

    std::vector<unsigned char> payload_binary;
    if (!Base64UrlDecode(payload, &payload_binary, NULL) || payload_binary.size() <= kAesIvLength) {
        LogApiKeyParseFail("payload_base64_invalid", api_key, short_tier, payload.size());
        return;
    }

    std::vector<unsigned char> iv(payload_binary.begin(), payload_binary.begin() + kAesIvLength);
    std::vector<unsigned char> ciphertext(payload_binary.begin() + kAesIvLength, payload_binary.end());
    std::string plaintext;
    if (!Aes256CbcDecrypt(enc_key_, iv, ciphertext, &plaintext, NULL)) {
        LogApiKeyParseFail("decrypt_failed", api_key, short_tier, payload.size());
        return;
    }

    nlohmann::json payload_json = nlohmann::json::parse(plaintext, NULL, false);
    if (payload_json.is_discarded() || !payload_json.is_object()) {
        LogApiKeyParseFail("payload_json_invalid", api_key, short_tier, payload.size());
        return;
    }

    if (!payload_json.contains("ver") || !payload_json["ver"].is_number_integer() || payload_json["ver"].get<int>() != 1) {
        LogApiKeyParseFail("missing_field:ver", api_key, short_tier, payload.size());
        return;
    }
    if (!payload_json.contains("tier") || !payload_json["tier"].is_string()) {
        LogApiKeyParseFail("missing_field:tier", api_key, short_tier, payload.size());
        return;
    }
    if (!payload_json.contains("tier_code") || !payload_json["tier_code"].is_number_integer()) {
        LogApiKeyParseFail("missing_field:tier_code", api_key, short_tier, payload.size());
        return;
    }
    if (!payload_json.contains("license_mode") || !payload_json["license_mode"].is_string()) {
        LogApiKeyParseFail("missing_field:license_mode", api_key, short_tier, payload.size());
        return;
    }
    if (!payload_json.contains("auth_scene") || !payload_json["auth_scene"].is_string()) {
        LogApiKeyParseFail("missing_field:auth_scene", api_key, short_tier, payload.size());
        return;
    }
    if (!payload_json.contains("issued_at") || !payload_json["issued_at"].is_number_integer()) {
        LogApiKeyParseFail("missing_field:issued_at", api_key, short_tier, payload.size());
        return;
    }
    if (!payload_json.contains("expires_at") || !payload_json["expires_at"].is_number_integer()) {
        LogApiKeyParseFail("missing_field:expires_at", api_key, short_tier, payload.size());
        return;
    }
    if (!payload_json.contains("device_scope") || !payload_json["device_scope"].is_array()) {
        LogApiKeyParseFail("missing_field:device_scope", api_key, short_tier, payload.size());
        return;
    }

    const std::string long_tier = payload_json["tier"].get<std::string>();
    const SdkAccountType payload_account_type = AccountTypeFromString(long_tier);
    if (payload_account_type == SdkAccountType::Unknown) {
        LogApiKeyParseFail("payload_tier_unknown", api_key, short_tier, payload.size(), long_tier);
        return;
    }
    if (payload_account_type != short_tier_account_type) {
        LogApiKeyParseFail("tier_mismatch", api_key, short_tier, payload.size(), long_tier);
        return;
    }
    if (payload_json["tier_code"].get<int>() != AccountTypeCode(payload_account_type)) {
        LogApiKeyParseFail("tier_code_mismatch", api_key, short_tier, payload.size());
        return;
    }
    const std::string license_mode = payload_json["license_mode"].get<std::string>();
    if (!IsSupportedLicenseMode(license_mode)) {
        LogApiKeyParseFail("license_mode_unsupported", api_key, short_tier, payload.size(), license_mode);
        return;
    }
    const std::string host_auth_mode = payload_json.value("host_auth_mode", std::string(kHostAuthModeNone));
    if (!IsSupportedHostAuthMode(host_auth_mode)) {
        LogApiKeyParseFail("host_auth_mode_unsupported", api_key, short_tier, payload.size(), host_auth_mode);
        return;
    }
    const bool commercial_authorized = payload_json.value("commercial_authorized",
                                                         payload_json.value("commercialAuthorized", false));
    if (payload_json["auth_scene"].get<std::string>() != kPluginScene) {
        parsed.code = 1106;
        parsed.message = "auth scene mismatch";
        LogApiKeyParseFail("auth_scene_mismatch", api_key, short_tier, payload.size(), payload_json["auth_scene"].get<std::string>());
        return;
    }

    std::vector<SdkDeviceGrant> device_scope;
    const nlohmann::json& device_scope_json = payload_json["device_scope"];
    for (nlohmann::json::const_iterator it = device_scope_json.begin(); it != device_scope_json.end(); ++it) {
        if (!it->is_object() || !it->contains("vid") || !(*it)["vid"].is_number_integer() || !it->contains("pid") ||
            !(*it)["pid"].is_number_integer()) {
            LogApiKeyParseFail("device_scope_invalid", api_key, short_tier, payload.size());
            return;
        }
        const int vid = (*it)["vid"].get<int>();
        const int pid = (*it)["pid"].get<int>();
        if (vid <= 0 || pid <= 0) {
            LogApiKeyParseFail("device_scope_invalid", api_key, short_tier, payload.size());
            return;
        }
        SdkDeviceGrant grant;
        grant.vid = vid;
        grant.pid = pid;
        device_scope.push_back(grant);
    }

    device_scope = NormalizeDeviceScope(device_scope);
    const std::int64_t expires_at = payload_json["expires_at"].get<std::int64_t>();
    if (expires_at > 0 && NormalizeNowTs(now_ts) > expires_at) {
        parsed.code = 1102;
        parsed.message = "api key expired";
        LogApiKeyParseFail("expired", api_key, short_tier, payload.size());
        return;
    }

    parsed.code = 0;
    parsed.message = "ok";
    parsed.key_id = payload_json.value("key_id", std::string());
    parsed.nonce = payload_json.value("nonce", std::string());
    parsed.license_mode = license_mode;
    parsed.token_digest = Sha256HexString(api_key);
    parsed.issued_at = payload_json["issued_at"].get<std::int64_t>();
    parsed.auth_context.is_valid = true;
    parsed.auth_context.account_type = payload_account_type;
    parsed.auth_context.account_type_code = AccountTypeCode(payload_account_type);
    parsed.auth_context.licensed_account_type = payload_account_type;
    parsed.auth_context.licensed_account_type_code = AccountTypeCode(payload_account_type);
    parsed.auth_context.auth_scene = kPluginScene;
    parsed.auth_context.license_mode = license_mode;
    parsed.auth_context.host_auth_mode = host_auth_mode;
    parsed.auth_context.commercial_authorized = commercial_authorized;
    parsed.auth_context.device_scope = device_scope;
    parsed.auth_context.expires_at = expires_at;
    SDK_PRIVATE_LOG_INFO("auth.api_key parse_success token_digest={} key_id={} license_mode={} tier={} expires_at={} device_scope_count={}",
                         TokenDigestForLog(api_key),
                         parsed.key_id,
                         parsed.license_mode,
                         ToAccountTypeString(parsed.auth_context.account_type),
                         parsed.auth_context.expires_at,
                         parsed.auth_context.device_scope.size());
    return;
}

bool PrivateSdkKeyCodec::BuildPayload(const ApiKeySpec& spec,
                                      nlohmann::json* payload_json,
                                      std::string* err) const {
    if (payload_json == NULL) {
        if (err != NULL) {
            *err = "missing payload json output";
        }
        return false;
    }

    std::string short_tier;
    if (!TryAccountTypeToShortTier(spec.account_type, &short_tier)) {
        if (err != NULL) {
            *err = "unsupported account type";
        }
        return false;
    }

    std::int64_t issued_at = spec.issued_at > 0 ? spec.issued_at : static_cast<std::int64_t>(std::time(NULL));
    std::string license_mode = spec.license_mode.empty() ? kOfflineApiKeyMode : spec.license_mode;
    if (!IsSupportedLicenseMode(license_mode)) {
        if (err != NULL) {
            *err = "unsupported license mode";
        }
        return false;
    }
    std::string host_auth_mode = spec.host_auth_mode.empty() ? kHostAuthModeNone : spec.host_auth_mode;
    if (!IsSupportedHostAuthMode(host_auth_mode)) {
        if (err != NULL) {
            *err = "unsupported host auth mode";
        }
        return false;
    }
    std::string key_id = spec.key_id;
    if (key_id.empty()) {
        key_id = "key_" + std::to_string(issued_at);
    }

    std::string nonce = spec.nonce;
    if (nonce.empty()) {
        std::vector<unsigned char> nonce_bytes;
        if (!RandomBytes(8, &nonce_bytes, err)) {
            return false;
        }
        nonce = HexLower(nonce_bytes.data(), nonce_bytes.size());
    }

    nlohmann::json device_scope = nlohmann::json::array();
    const std::vector<SdkDeviceGrant> normalized = NormalizeDeviceScope(spec.device_scope);
    for (std::size_t i = 0; i < normalized.size(); ++i) {
        device_scope.push_back(nlohmann::json{
            {"vid", normalized[i].vid},
            {"pid", normalized[i].pid},
        });
    }

    *payload_json = nlohmann::json{
        {"ver", 1},
        {"tier", ToAccountTypeString(spec.account_type)},
        {"tier_code", AccountTypeCode(spec.account_type)},
        {"license_mode", license_mode},
        {"host_auth_mode", host_auth_mode},
        {"commercial_authorized", spec.commercial_authorized},
        {"auth_scene", kPluginScene},
        {"device_scope", device_scope},
        {"issued_at", issued_at},
        {"expires_at", spec.expires_at},
        {"key_id", key_id},
        {"nonce", nonce},
    };
    return true;
}

std::vector<SdkDeviceGrant> PrivateSdkKeyCodec::NormalizeDeviceScope(
    const std::vector<SdkDeviceGrant>& device_scope) const {
    std::vector<SdkDeviceGrant> normalized;
    normalized.reserve(device_scope.size());
    for (std::size_t i = 0; i < device_scope.size(); ++i) {
        if (device_scope[i].vid > 0 && device_scope[i].pid > 0) {
            normalized.push_back(device_scope[i]);
        }
    }

    std::sort(normalized.begin(),
              normalized.end(),
              [](const SdkDeviceGrant& lhs, const SdkDeviceGrant& rhs) {
                  if (lhs.vid != rhs.vid) {
                      return lhs.vid < rhs.vid;
                  }
                  return lhs.pid < rhs.pid;
              });
    normalized.erase(std::unique(normalized.begin(),
                                 normalized.end(),
                                 [](const SdkDeviceGrant& lhs, const SdkDeviceGrant& rhs) {
                                     return lhs.vid == rhs.vid && lhs.pid == rhs.pid;
                                 }),
                     normalized.end());
    return normalized;
}

} // namespace private_impl
} // namespace sdk
} // namespace editor
