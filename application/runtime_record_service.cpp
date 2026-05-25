// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "runtime_record_service.h"

#include <algorithm>
#include <cctype>
#include <ctime>

namespace editor {
namespace sdk {

namespace {

const std::size_t kDefaultLimit = 100;
const std::size_t kMaxLimit = 500;
const std::size_t kPreviewLimit = 1600;

std::string ToLowerAscii(std::string value) {
    for (std::string::iterator it = value.begin(); it != value.end(); ++it) {
        *it = static_cast<char>(std::tolower(static_cast<unsigned char>(*it)));
    }
    return value;
}

bool IsSensitiveKey(const std::string& key) {
    const std::string value = ToLowerAscii(key);
    return value == "token" ||
           value == "apikey" ||
           value == "api_key" ||
           value == "session_token" ||
           value == "sessiontoken" ||
           value == "auth_code" ||
           value == "authcode" ||
           value == "authorization" ||
           value == "x-api-key";
}

std::string MaskValue(const std::string& value) {
    if (value.empty()) {
        return "";
    }
    if (value.size() <= 10) {
        return "***";
    }
    return value.substr(0, 6) + "..." + value.substr(value.size() - 4);
}

Json RedactJson(const Json& value) {
    if (value.is_object()) {
        Json redacted = Json::object();
        for (Json::const_iterator it = value.begin(); it != value.end(); ++it) {
            if (IsSensitiveKey(it.key())) {
                redacted[it.key()] = it.value().is_string() ? MaskValue(it.value().get<std::string>()) : "***";
            } else {
                redacted[it.key()] = RedactJson(it.value());
            }
        }
        return redacted;
    }
    if (value.is_array()) {
        Json redacted = Json::array();
        for (Json::const_iterator it = value.begin(); it != value.end(); ++it) {
            redacted.push_back(RedactJson(*it));
        }
        return redacted;
    }
    return value;
}

} // namespace

RuntimeRecordService::RuntimeRecordService(std::size_t capacity)
    : capacity_(capacity == 0 ? 256 : capacity),
      next_id_(1) {}

void RuntimeRecordService::RecordCommandRequest(const std::string& connection_id,
                                                const Json& request,
                                                const Json& response,
                                                uint64_t duration_ms) {
    Entry entry;
    entry.ts = static_cast<std::int64_t>(std::time(NULL));
    entry.type = "request";
    entry.source = "command_ws";
    entry.connection_id = connection_id;
    entry.method = GetStringField(request, "method");
    entry.request_id = GetStringField(request, "request_id");
    if (entry.request_id.empty()) {
        entry.request_id = GetStringField(request, "id");
    }
    entry.code = GetIntField(response, "code", 0);
    entry.message = GetStringField(response, "message");
    entry.status = IsOkStatusCode(entry.code) ? "success" : "error";
    entry.duration_ms = duration_ms;
    entry.payload_preview = PreviewJson(Json{{"request", request}, {"response", response}});

    std::lock_guard<std::mutex> lock(mu_);
    PushLocked(entry);
}

void RuntimeRecordService::RecordRuntimeEvent(const std::string& source,
                                              const std::string& connection_id,
                                              const std::string& name,
                                              const std::string& message,
                                              int code,
                                              const Json& payload) {
    Entry entry;
    entry.ts = static_cast<std::int64_t>(std::time(NULL));
    entry.type = IsOkStatusCode(code) ? "event" : "error";
    entry.source = source;
    entry.connection_id = connection_id;
    entry.method = name;
    entry.request_id = GetStringField(payload, "request_id");
    entry.code = code;
    entry.message = message;
    entry.status = IsOkStatusCode(code) ? "success" : "error";
    entry.payload_preview = PreviewJson(payload);

    std::lock_guard<std::mutex> lock(mu_);
    PushLocked(entry);
}

Json RuntimeRecordService::BuildRecordsJson(const std::string& type_filter, std::size_t limit) const {
    if (limit == 0) {
        limit = kDefaultLimit;
    }
    if (limit > kMaxLimit) {
        limit = kMaxLimit;
    }

    Json records = Json::array();
    std::lock_guard<std::mutex> lock(mu_);
    for (std::deque<Entry>::const_reverse_iterator it = entries_.rbegin();
         it != entries_.rend() && records.size() < limit;
         ++it) {
        if (!type_filter.empty() && it->type != type_filter) {
            continue;
        }
        records.push_back(Json{
            {"id", it->id},
            {"ts", it->ts},
            {"type", it->type},
            {"source", it->source},
            {"connectionId", it->connection_id},
            {"method", it->method},
            {"requestId", it->request_id},
            {"status", it->status},
            {"code", it->code},
            {"message", it->message},
            {"durationMs", it->duration_ms},
            {"payloadPreview", it->payload_preview},
        });
    }

    return Json{{"records", records}, {"totalRetained", entries_.size()}, {"capacity", capacity_}};
}

void RuntimeRecordService::PushLocked(const Entry& source) {
    Entry entry = source;
    entry.id = next_id_++;
    entries_.push_back(entry);
    while (entries_.size() > capacity_) {
        entries_.pop_front();
    }
}

std::string RuntimeRecordService::PreviewJson(const Json& payload) {
    std::string preview = DumpJson(RedactJson(payload));
    if (preview.size() > kPreviewLimit) {
        preview = preview.substr(0, kPreviewLimit) + "...";
    }
    return preview;
}

std::string RuntimeRecordService::GetStringField(const Json& obj, const char* key) {
    if (!obj.is_object()) {
        return "";
    }
    const Json::const_iterator it = obj.find(key);
    if (it != obj.end() && it->is_string()) {
        return it->get<std::string>();
    }
    return "";
}

int RuntimeRecordService::GetIntField(const Json& obj, const char* key, int default_value) {
    if (!obj.is_object()) {
        return default_value;
    }
    const Json::const_iterator it = obj.find(key);
    if (it != obj.end() && it->is_number_integer()) {
        return it->get<int>();
    }
    return default_value;
}

} // namespace sdk
} // namespace editor
