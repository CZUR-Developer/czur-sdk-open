// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace editor {
namespace sdk {

using Json = nlohmann::json;

std::string DumpJson(const Json& value);
bool TryParseJson(std::string_view payload, Json* out, std::string* err = nullptr);
Json BuildErrorBody(int code, const std::string& message, const Json& data = Json::object());
Json BuildWsResponse(const std::string& request_id,
                     int code,
                     const std::string& message,
                     const Json& data = Json::object());
Json BuildWsEvent(const std::string& event,
                  const Json& payload,
                  int code = 0,
                  const std::string& message = "ok");

} // namespace sdk
} // namespace editor
