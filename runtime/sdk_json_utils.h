// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "sdk_status_code.h"

namespace editor {
namespace sdk {

using Json = nlohmann::json;

std::string DumpJson(const Json& value);
bool TryParseJson(const std::string& payload, Json* out, std::string* err = nullptr);
Json BuildErrorBody(SdkStatusCode code, const std::string& message, const Json& data = Json::object());
Json BuildErrorBody(int code, const std::string& message, const Json& data = Json::object());
Json BuildWsResponse(const std::string& request_id,
                     SdkStatusCode code,
                     const std::string& message,
                     const Json& data = Json::object());
Json BuildWsResponse(const std::string& request_id,
                     int code,
                     const std::string& message,
                     const Json& data = Json::object());
Json BuildWsEvent(const std::string& event,
                  const Json& payload,
                  SdkStatusCode code = SdkStatusCode::Ok,
                  const std::string& message = "ok");
Json BuildWsEvent(const std::string& event,
                  const Json& payload,
                  int code,
                  const std::string& message);

} // namespace sdk
} // namespace editor
