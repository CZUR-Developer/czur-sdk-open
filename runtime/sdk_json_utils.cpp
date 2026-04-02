// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_json_utils.h"

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

Json BuildErrorBody(int code, const std::string& message, const Json& data) {
    return Json{
        {"code", code},
        {"message", message},
        {"data", data},
    };
}

Json BuildWsResponse(const std::string& request_id, int code, const std::string& message, const Json& data) {
    Json response = BuildErrorBody(code, message, data);
    response["id"] = request_id;
    return response;
}

Json BuildWsEvent(const std::string& event, const Json& payload, int code, const std::string& message) {
    return Json{
        {"event", event},
        {"code", code},
        {"message", message},
        {"payload", payload},
    };
}

} // namespace sdk
} // namespace editor
