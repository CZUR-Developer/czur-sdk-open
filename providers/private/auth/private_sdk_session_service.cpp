// Copyright (c) 2026 CZUR Tech. All rights reserved.

#include "private_sdk_session_service.h"

#include <cstdlib>
#include <ctime>
#include <vector>

#include <nlohmann/json.hpp>

#include "private_sdk_crypto_utils.h"
#include "private_sdk_master_secret.h"

namespace editor {
namespace sdk {
namespace private_impl {

namespace {

const char kSessionPrefix[] = "ss-v1-";
const char kPluginScene[] = "plugin";
const char kOfflineApiKeyMode[] = "offline_api_key";
const char kOnlineApiKeyMode[] = "online_api_key";
const char kSessionEncInfo[] = "czur-sdk-session-v1-enc";
const char kSessionSigInfo[] = "czur-sdk-session-v1-sig";
const std::size_t kSignatureHexLength = 32;
const std::size_t kSignatureBinaryLength = 16;
const std::size_t kAesKeyLength = 32;
const std::size_t kAesIvLength = 16;
const int kDefaultSessionTtlSec = 7200;

std::int64_t NormalizeNowTs(std::int64_t now_ts) {
    return now_ts > 0 ? now_ts : static_cast<std::int64_t>(std::time(NULL));
}

std::string BuildSignatureInput(const std::string& payload) {
    return std::string(kSessionPrefix) + payload;
}

nlohmann::json BuildQuotaBucketsJson(const std::vector<AuthQuotaBucket>& quota_buckets) {
    nlohmann::json buckets = nlohmann::json::array();
    std::size_t count = 0;
    try {
        count = quota_buckets.size();
    } catch (const std::exception&) {
        return buckets;
    }
    if (count > 256) {
        return buckets;
    }
    for (std::size_t bucket_index = 0; bucket_index < count; ++bucket_index) {
        const AuthQuotaBucket& bucket = quota_buckets[bucket_index];
        nlohmann::json methods = nlohmann::json::array();
        std::size_t method_count = 0;
        try {
            method_count = bucket.methods.size();
        } catch (const std::exception&) {
            method_count = 0;
        }
        if (method_count > 1024) {
            method_count = 0;
        }
        for (std::size_t method_index = 0; method_index < method_count; ++method_index) {
            methods.push_back(bucket.methods[method_index]);
        }
        buckets.push_back(nlohmann::json{
            {"bucket", bucket.bucket},
            {"methods", methods},
            {"limit", bucket.limit},
            {"remaining", bucket.remaining},
            {"enforcement", bucket.enforcement},
        });
    }
    return buckets;
}

nlohmann::json BuildDeviceScopeJson(const std::vector<SdkDeviceGrant>& device_scope) {
    nlohmann::json output = nlohmann::json::array();
    try {
        const std::size_t count = device_scope.size();
        if (count > 256) {
            return output;
        }
        for (std::size_t i = 0; i < count; ++i) {
            output.push_back(nlohmann::json{
                {"vid", device_scope[i].vid},
                {"pid", device_scope[i].pid},
            });
        }
    } catch (const std::exception&) {
        return nlohmann::json::array();
    }
    return output;
}

bool ParseQuotaBucketsJson(const nlohmann::json& buckets_json, std::vector<AuthQuotaBucket>* quota_buckets) {
    if (quota_buckets == NULL) {
        return false;
    }
    quota_buckets->clear();
    if (!buckets_json.is_array()) {
        return false;
    }
    for (nlohmann::json::const_iterator bucket_it = buckets_json.begin(); bucket_it != buckets_json.end(); ++bucket_it) {
        if (!bucket_it->is_object()) {
            return false;
        }
        AuthQuotaBucket bucket;
        bucket.bucket = bucket_it->value("bucket", std::string());
        bucket.limit = bucket_it->value("limit", 0);
        bucket.remaining = bucket_it->value("remaining", 0);
        bucket.enforcement = bucket_it->value("enforcement", std::string());
        const nlohmann::json& methods_json = bucket_it->value("methods", nlohmann::json::array());
        if (!methods_json.is_array()) {
            return false;
        }
        for (nlohmann::json::const_iterator method_it = methods_json.begin(); method_it != methods_json.end();
             ++method_it) {
            if (!method_it->is_string()) {
                return false;
            }
            bucket.methods.push_back(method_it->get<std::string>());
        }
        quota_buckets->push_back(bucket);
    }
    return true;
}

} // namespace

PrivateSdkSessionService::PrivateSdkSessionService() {
    const std::string master_secret = GetPrivateSdkMasterSecret();
    enc_key_ = HkdfSha256(master_secret, kSessionEncInfo, kAesKeyLength);
    sig_key_ = HkdfSha256(master_secret, kSessionSigInfo, kAesKeyLength);
}

SessionIssueResult PrivateSdkSessionService::IssueSession(const AuthContext& auth_context, std::int64_t now_ts) const {
    SessionIssueResult result;
    IssueSession(auth_context, now_ts, &result);
    return result;
}

void PrivateSdkSessionService::IssueSession(const AuthContext& auth_context,
                                            std::int64_t now_ts,
                                            SessionIssueResult* result_output) const {
    if (result_output == NULL) {
        return;
    }
    SessionIssueResult result;
    if (!auth_context.is_valid) {
        result.code = 1101;
        result.message = "api key invalid";
        *result_output = result;
        return;
    }

    const std::int64_t issued_at = NormalizeNowTs(now_ts);
    const std::int64_t expires_at = issued_at + kDefaultSessionTtlSec;

    const nlohmann::json device_scope = BuildDeviceScopeJson(auth_context.device_scope);

    std::vector<unsigned char> nonce;
    if (!RandomBytes(8, &nonce, NULL)) {
        result.code = 1900;
        result.message = "internal error";
        *result_output = result;
        return;
    }

    const nlohmann::json payload_json = nlohmann::json{
        {"ver", 1},
        {"tier", ToAccountTypeString(auth_context.account_type)},
        {"tier_code", auth_context.account_type_code},
        {"licensed_account_type", ToAccountTypeString(auth_context.licensed_account_type)},
        {"licensed_account_type_code", auth_context.licensed_account_type_code},
        {"auth_scene", kPluginScene},
        {"license_mode", auth_context.license_mode},
        {"host_auth_mode", auth_context.host_auth_mode},
        {"entitlement_state", auth_context.entitlement_state},
        {"commercial_authorized", auth_context.commercial_authorized},
        {"machine_code", auth_context.machine_code},
        {"device_scope", device_scope},
        {"quota_buckets", BuildQuotaBucketsJson(auth_context.quota_buckets)},
        {"issued_at", issued_at},
        {"expires_at", expires_at},
        {"nonce", HexLower(nonce.data(), nonce.size())},
    };

    std::vector<unsigned char> iv;
    if (!RandomBytes(kAesIvLength, &iv, NULL)) {
        result.code = 1900;
        result.message = "internal error";
        *result_output = result;
        return;
    }

    std::vector<unsigned char> ciphertext;
    if (!Aes256CbcEncrypt(enc_key_, iv, payload_json.dump(), &ciphertext, NULL)) {
        result.code = 1900;
        result.message = "internal error";
        *result_output = result;
        return;
    }

    std::vector<unsigned char> payload_binary = iv;
    payload_binary.insert(payload_binary.end(), ciphertext.begin(), ciphertext.end());
    const std::string payload = Base64UrlEncode(payload_binary);

    std::vector<unsigned char> mac;
    if (!HmacSha256(sig_key_, BuildSignatureInput(payload), &mac, NULL)) {
        result.code = 1900;
        result.message = "internal error";
        *result_output = result;
        return;
    }

    result.code = 0;
    result.message = "ok";
    result.expires_in = kDefaultSessionTtlSec;
    result.auth_context = auth_context;
    result.session_token = BuildSignatureInput(payload) + HexLower(mac.data(), kSignatureBinaryLength);
    *result_output = result;
    return;
}

SessionParseResult PrivateSdkSessionService::ValidateSession(const std::string& session_token,
                                                             std::int64_t now_ts) const {
    SessionParseResult result;
    result.code = 1103;
    result.message = "session token invalid";

    const std::string prefix = kSessionPrefix;
    if (session_token.compare(0, prefix.size(), prefix) != 0) {
        return result;
    }
    if (session_token.size() <= prefix.size() + kSignatureHexLength) {
        return result;
    }

    const std::size_t payload_end = session_token.size() - kSignatureHexLength;
    const std::string payload = session_token.substr(prefix.size(), payload_end - prefix.size());
    const std::string signature = session_token.substr(payload_end);
    if (payload.empty()) {
        return result;
    }

    std::vector<unsigned char> mac;
    if (!HmacSha256(sig_key_, BuildSignatureInput(payload), &mac, NULL)) {
        result.code = 1900;
        result.message = "internal error";
        return result;
    }
    if (!ConstantTimeEquals(signature, HexLower(mac.data(), kSignatureBinaryLength))) {
        return result;
    }

    std::vector<unsigned char> payload_binary;
    if (!Base64UrlDecode(payload, &payload_binary, NULL) || payload_binary.size() <= kAesIvLength) {
        return result;
    }

    const std::vector<unsigned char> iv(payload_binary.begin(), payload_binary.begin() + kAesIvLength);
    const std::vector<unsigned char> ciphertext(payload_binary.begin() + kAesIvLength, payload_binary.end());
    std::string plaintext;
    if (!Aes256CbcDecrypt(enc_key_, iv, ciphertext, &plaintext, NULL)) {
        return result;
    }

    nlohmann::json payload_json = nlohmann::json::parse(plaintext, NULL, false);
    if (payload_json.is_discarded() || !payload_json.is_object()) {
        return result;
    }

    if (!payload_json.contains("tier") || !payload_json["tier"].is_string() || !payload_json.contains("tier_code") ||
        !payload_json["tier_code"].is_number_integer() || !payload_json.contains("auth_scene") ||
        !payload_json["auth_scene"].is_string() || !payload_json.contains("license_mode") ||
        !payload_json["license_mode"].is_string() || !payload_json.contains("device_scope") ||
        !payload_json["device_scope"].is_array() || !payload_json.contains("expires_at") ||
        !payload_json["expires_at"].is_number_integer() || !payload_json.contains("quota_buckets")) {
        return result;
    }

    const std::string license_mode = payload_json["license_mode"].get<std::string>();
    if (payload_json["auth_scene"].get<std::string>() != kPluginScene ||
        (license_mode != kOfflineApiKeyMode && license_mode != kOnlineApiKeyMode)) {
        return result;
    }

    const SdkAccountType account_type = AccountTypeFromString(payload_json["tier"].get<std::string>());
    if (account_type == SdkAccountType::Unknown ||
        payload_json["tier_code"].get<int>() != AccountTypeCode(account_type)) {
        return result;
    }
    const SdkAccountType licensed_account_type =
        AccountTypeFromString(payload_json.value("licensed_account_type", payload_json["tier"].get<std::string>()));
    const int licensed_account_type_code =
        payload_json.value("licensed_account_type_code", AccountTypeCode(licensed_account_type));
    if (licensed_account_type != SdkAccountType::Unknown &&
        licensed_account_type_code != AccountTypeCode(licensed_account_type)) {
        return result;
    }

    const std::int64_t expires_at = payload_json["expires_at"].get<std::int64_t>();
    if (expires_at <= 0 || NormalizeNowTs(now_ts) > expires_at) {
        result.code = 1103;
        result.message = "session token invalid";
        return result;
    }

    std::vector<SdkDeviceGrant> device_scope;
    for (nlohmann::json::const_iterator it = payload_json["device_scope"].begin();
         it != payload_json["device_scope"].end();
         ++it) {
        if (!it->is_object() || !it->contains("vid") || !(*it)["vid"].is_number_integer() || !it->contains("pid") ||
            !(*it)["pid"].is_number_integer()) {
            return result;
        }
        SdkDeviceGrant grant;
        grant.vid = (*it)["vid"].get<int>();
        grant.pid = (*it)["pid"].get<int>();
        device_scope.push_back(grant);
    }

    std::vector<AuthQuotaBucket> quota_buckets;
    if (!ParseQuotaBucketsJson(payload_json["quota_buckets"], &quota_buckets)) {
        return result;
    }

    result.code = 0;
    result.message = "ok";
    result.auth_context.is_valid = true;
    result.auth_context.account_type = account_type;
    result.auth_context.account_type_code = AccountTypeCode(account_type);
    result.auth_context.licensed_account_type = licensed_account_type;
    result.auth_context.licensed_account_type_code = AccountTypeCode(licensed_account_type);
    result.auth_context.auth_scene = kPluginScene;
    result.auth_context.license_mode = license_mode;
    result.auth_context.host_auth_mode = payload_json.value("host_auth_mode", std::string());
    result.auth_context.entitlement_state = payload_json.value("entitlement_state", std::string());
    result.auth_context.commercial_authorized = payload_json.value("commercial_authorized", false);
    result.auth_context.machine_code = payload_json.value("machine_code", std::string());
    result.auth_context.device_scope = device_scope;
    result.auth_context.expires_at = expires_at;
    result.auth_context.quota_buckets = quota_buckets;
    result.key_id = payload_json.value("key_id", std::string());
    return result;
}

} // namespace private_impl
} // namespace sdk
} // namespace editor
