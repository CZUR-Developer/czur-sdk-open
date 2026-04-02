// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_json_utils.h"

#include <ctime>
#include <utility>

namespace editor {
namespace sdk {

std::string DumpJson(const Json& value) {
    return value.dump();
}

bool TryParseJson(std::string_view payload, Json* out, std::string* err) {
    if (out == nullptr) {
        if (err != nullptr) {
            *err = "output json is null";
        }
        return false;
    }

    Json parsed = Json::parse(payload.begin(), payload.end(), nullptr, false);
    if (parsed.is_discarded()) {
        if (err != nullptr) {
            *err = "invalid json";
        }
        return false;
    }

    *out = std::move(parsed);
    return true;
}

Json BuildErrorBody(SdkStatusCode code, const std::string& message, const Json& data) {
    return BuildErrorBody(ToCode(code), message, data);
}

Json BuildErrorBody(int code, const std::string& message, const Json& data) {
    return Json{
        {"code", code},
        {"message", message},
        {"data", data},
    };
}

Json BuildWsResponse(const std::string& request_id, SdkStatusCode code, const std::string& message, const Json& data) {
    return BuildWsResponse(request_id, ToCode(code), message, data);
}

Json BuildWsResponse(const std::string& request_id, int code, const std::string& message, const Json& data) {
    Json response = BuildErrorBody(code, message, data);
    response["request_id"] = request_id;
    response["id"] = request_id;
    response["ts"] = static_cast<std::int64_t>(std::time(nullptr));
    return response;
}

Json BuildWsEvent(const std::string& event, const Json& payload, SdkStatusCode code, const std::string& message) {
    return BuildWsEvent(event, payload, ToCode(code), message);
}

Json BuildWsEvent(const std::string& event, const Json& payload, int code, const std::string& message) {
    return Json{
        {"event", event},
        {"code", code},
        {"message", message},
        {"payload", payload},
        {"ts", static_cast<std::int64_t>(std::time(nullptr))},
    };
}

} // namespace sdk
} // namespace editor
