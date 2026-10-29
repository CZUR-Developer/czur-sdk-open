// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "online_image_enhance_client.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <sys/stat.h>

#include <httplib.h>
#include "nlohmann/json.hpp"

#include "auth/private_sdk_support.h"

namespace editor {
namespace sdk {
namespace private_impl {

namespace {

const char kBaseUrlEnv[] = "CZUR_SDK_IMAGE_ENHANCE_BASE_URL";
const char kTimeoutMsEnv[] = "CZUR_SDK_IMAGE_ENHANCE_TIMEOUT_MS";
const char kDefaultBaseUrl[] = "https://gateway.czur.com";
const char kProcessImagePath[] = "/api/v1/capeditor/image-enhance/process-image";
const long kMaxImageBytes = 3L * 1024L * 1024L;

struct ParsedUrl {
    bool valid = false;
    bool https = false;
    std::string scheme_host_port;
    std::string path = "/";
};

std::string GetEnvString(const char* name) {
    const char* value = std::getenv(name);
    return value == NULL ? std::string() : std::string(value);
}

int GetTimeoutMs() {
    const std::string value = GetEnvString(kTimeoutMsEnv);
    if (value.empty()) {
        return 90000;
    }
    const int parsed = std::atoi(value.c_str());
    return parsed > 0 ? parsed : 90000;
}

std::string BaseUrl(const std::string& override_base_url) {
    const std::string env_base_url = GetEnvString(kBaseUrlEnv);
    if (!env_base_url.empty()) {
        return env_base_url;
    }
    return override_base_url.empty() ? std::string(kDefaultBaseUrl) : override_base_url;
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string ExtensionFromPath(const std::string& path) {
    const std::string::size_type slash_pos = path.find_last_of("/\\");
    const std::string leaf = slash_pos == std::string::npos ? path : path.substr(slash_pos + 1);
    const std::string::size_type dot_pos = leaf.find_last_of('.');
    if (dot_pos == std::string::npos || dot_pos + 1 >= leaf.size()) {
        return "";
    }
    return Lower(leaf.substr(dot_pos + 1));
}

std::string BaseName(const std::string& path) {
    const std::string::size_type slash_pos = path.find_last_of("/\\");
    const std::string leaf = slash_pos == std::string::npos ? path : path.substr(slash_pos + 1);
    return leaf.empty() ? "image.jpg" : leaf;
}

std::string ContentTypeForPath(const std::string& path) {
    const std::string extension = ExtensionFromPath(path);
    if (extension == "png") {
        return "image/png";
    }
    if (extension == "webp") {
        return "image/webp";
    }
    if (extension == "jpg" || extension == "jpeg") {
        return "image/jpeg";
    }
    return "";
}

bool FileExistsAndSize(const std::string& path, long* size) {
    struct stat st;
    if (path.empty() || ::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
        return false;
    }
    if (size != NULL) {
        *size = static_cast<long>(st.st_size);
    }
    return true;
}

bool ReadFileAll(const std::string& path, std::string* output) {
    if (output == NULL) {
        return false;
    }
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream stream;
    stream << in.rdbuf();
    *output = stream.str();
    return static_cast<bool>(in) || in.eof();
}

bool WriteFileAll(const std::string& path, const std::string& bytes) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) {
        return false;
    }
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

ParsedUrl ParseUrl(const std::string& raw_url) {
    ParsedUrl parsed;
    const std::string::size_type scheme_pos = raw_url.find("://");
    if (scheme_pos == std::string::npos) {
        return parsed;
    }
    const std::string scheme = Lower(raw_url.substr(0, scheme_pos));
    if (scheme != "http" && scheme != "https") {
        return parsed;
    }
    const std::string::size_type authority_begin = scheme_pos + 3;
    const std::string::size_type path_begin = raw_url.find('/', authority_begin);
    const std::string authority = path_begin == std::string::npos
                                      ? raw_url.substr(authority_begin)
                                      : raw_url.substr(authority_begin, path_begin - authority_begin);
    if (authority.empty()) {
        return parsed;
    }
    parsed.valid = true;
    parsed.https = scheme == "https";
    parsed.scheme_host_port = scheme + "://" + authority;
    parsed.path = path_begin == std::string::npos ? "/" : raw_url.substr(path_begin);
    if (parsed.path.empty()) {
        parsed.path = "/";
    }
    return parsed;
}

std::string JoinUrlPath(const std::string& base_path, const std::string& endpoint) {
    if (base_path.empty() || base_path == "/") {
        return endpoint;
    }
    if (base_path[base_path.size() - 1] == '/') {
        return base_path.substr(0, base_path.size() - 1) + endpoint;
    }
    return base_path + endpoint;
}

int MapHttpStatusToCode(int status) {
    if (status == 400) {
        return ToCode(SdkStatusCode::InvalidParams);
    }
    if (status == 401) {
        return ToCode(SdkStatusCode::AuthRequired);
    }
    if (status == 403) {
        return ToCode(SdkStatusCode::CapabilityNotAllowed);
    }
    if (status == 429) {
        return ToCode(SdkStatusCode::UsageLimitExceeded);
    }
    return ToCode(SdkStatusCode::ProviderCallFailed);
}

nlohmann::json ParseJsonObject(const std::string& body) {
    nlohmann::json parsed = nlohmann::json::parse(body, NULL, false);
    return parsed.is_object() ? parsed : nlohmann::json::object();
}

std::unique_ptr<httplib::Client> BuildClient(const std::string& scheme_host_port, int timeout_ms) {
    std::unique_ptr<httplib::Client> client(new httplib::Client(scheme_host_port.c_str()));
    if (scheme_host_port.compare(0, 8, "https://") == 0) {
        ConfigureSdkHttpsClientTrustStore(client.get());
    }
    client->set_connection_timeout(timeout_ms / 1000, (timeout_ms % 1000) * 1000);
    client->set_read_timeout(timeout_ms / 1000, (timeout_ms % 1000) * 1000);
    client->set_write_timeout(timeout_ms / 1000, (timeout_ms % 1000) * 1000);
    client->set_follow_location(true);
    return client;
}

OnlineImageEnhanceResult ErrorResult(int code, const std::string& message) {
    OnlineImageEnhanceResult result;
    result.code = code;
    result.message = message;
    return result;
}

OnlineImageEnhanceResult DownloadResult(const std::string& result_url,
                                        const std::string& output_path,
                                        int timeout_ms) {
    const ParsedUrl parsed = ParseUrl(result_url);
    if (!parsed.valid) {
        return ErrorResult(ToCode(SdkStatusCode::ProviderCallFailed), "online image enhance result url invalid");
    }
    std::unique_ptr<httplib::Client> client = BuildClient(parsed.scheme_host_port, timeout_ms);
    httplib::Result response = client->Get(parsed.path.c_str());
    if (!response) {
        return ErrorResult(ToCode(SdkStatusCode::OnlineAuthUnavailable),
                           "online image enhance download failed: " + httplib::to_string(response.error()));
    }
    if (response->status != 200) {
        return ErrorResult(response->status == 403 ? ToCode(SdkStatusCode::TokenExpired)
                                                   : MapHttpStatusToCode(response->status),
                           "online image enhance download failed, http=" + std::to_string(response->status));
    }
    if (!WriteFileAll(output_path, response->body)) {
        return ErrorResult(ToCode(SdkStatusCode::ProviderCallFailed), "failed to write online image enhance output");
    }
    OnlineImageEnhanceResult result;
    result.processed = true;
    return result;
}

} // namespace

bool OnlineImageEnhanceClient::IsConfigured() {
    return false;
}

OnlineImageEnhanceResult OnlineImageEnhanceClient::EnhanceImage(const OnlineImageEnhanceOptions& options) {
    const std::string api_key = options.api_key;
    if (api_key.empty()) {
        return ErrorResult(ToCode(SdkStatusCode::ProviderNotReady), "online image enhance api key is not configured");
    }
    if (options.capability.empty()) {
        return ErrorResult(ToCode(SdkStatusCode::InvalidParams), "online image enhance capability is empty");
    }
    const std::string content_type = ContentTypeForPath(options.input_path);
    if (content_type.empty()) {
        return ErrorResult(ToCode(SdkStatusCode::InvalidParams), "online image enhance input must be jpg, png, or webp");
    }
    long file_size = 0;
    if (!FileExistsAndSize(options.input_path, &file_size)) {
        return ErrorResult(ToCode(SdkStatusCode::InvalidParams), "online image enhance input file does not exist");
    }
    if (file_size <= 0 || file_size > kMaxImageBytes) {
        return ErrorResult(ToCode(SdkStatusCode::InvalidParams), "online image enhance input size must be between 1 byte and 3MB");
    }
    std::string image_bytes;
    if (!ReadFileAll(options.input_path, &image_bytes)) {
        return ErrorResult(ToCode(SdkStatusCode::InvalidParams), "failed to read online image enhance input file");
    }

    const ParsedUrl parsed_base = ParseUrl(BaseUrl(options.base_url));
    if (!parsed_base.valid) {
        return ErrorResult(ToCode(SdkStatusCode::InvalidParams), "online image enhance base url invalid");
    }

    const int timeout_ms = GetTimeoutMs();
    std::unique_ptr<httplib::Client> client = BuildClient(parsed_base.scheme_host_port, timeout_ms);
    httplib::Headers headers;
    headers.insert(std::make_pair("X-Api-Key", api_key));
    httplib::MultipartFormDataItems items;
    items.push_back(httplib::MultipartFormData{"capability", options.capability, "", ""});
    items.push_back(httplib::MultipartFormData{"image", image_bytes, BaseName(options.input_path), content_type});

    const std::string path = JoinUrlPath(parsed_base.path, kProcessImagePath);
    httplib::Result response = client->Post(path.c_str(), headers, items);
    if (!response) {
        return ErrorResult(ToCode(SdkStatusCode::OnlineAuthUnavailable),
                           "online image enhance request failed: " + httplib::to_string(response.error()));
    }

    const nlohmann::json body = ParseJsonObject(response->body);
    if (response->status < 200 || response->status >= 300) {
        const std::string message = body.value("message", std::string("online image enhance http error"));
        return ErrorResult(MapHttpStatusToCode(response->status), message);
    }

    const int business_code = body.value("code", -1);
    if (business_code != 200) {
        const std::string message = body.value("message", std::string("online image enhance business error"));
        return ErrorResult(MapHttpStatusToCode(business_code), message);
    }
    const nlohmann::json data = body.value("data", nlohmann::json::object());
    const std::string result_url = data.value("resultAccessUrl", std::string());
    if (result_url.empty()) {
        return ErrorResult(ToCode(SdkStatusCode::ProviderCallFailed), "online image enhance result url is empty");
    }

    OnlineImageEnhanceResult download = DownloadResult(result_url, options.output_path, timeout_ms);
    if (!IsOkStatusCode(download.code)) {
        return download;
    }

    nlohmann::json metadata;
    metadata["requestNo"] = data.value("requestNo", std::string());
    metadata["capability"] = data.value("capability", options.capability);
    metadata["provider"] = data.value("provider", std::string());
    metadata["resultAccessUrlExpireAt"] = data.value("resultAccessUrlExpireAt", 0LL);
    metadata["quotaLimit"] = data.value("quotaLimit", 0);
    metadata["quotaUsed"] = data.value("quotaUsed", 0);
    metadata["quotaRemaining"] = data.value("quotaRemaining", 0);
    metadata["inputObjectId"] = data.value("inputObjectId", 0LL);
    metadata["resultObjectId"] = data.value("resultObjectId", 0LL);

    OnlineImageEnhanceResult result;
    result.processed = true;
    result.metadata_json = metadata.dump();
    return result;
}

} // namespace private_impl
} // namespace sdk
} // namespace editor
