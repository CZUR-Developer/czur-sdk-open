// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

#include "sdk_json_utils.h"

namespace editor {
namespace sdk {

class RuntimeRecordService {
public:
    explicit RuntimeRecordService(std::size_t capacity = 256);

    void RecordCommandRequest(const std::string& connection_id,
                              const Json& request,
                              const Json& response,
                              uint64_t duration_ms);
    void RecordRuntimeEvent(const std::string& source,
                            const std::string& connection_id,
                            const std::string& name,
                            const std::string& message,
                            int code = 0,
                            const Json& payload = Json::object());
    Json BuildRecordsJson(const std::string& type_filter, std::size_t limit) const;

private:
    struct Entry {
        uint64_t id = 0;
        std::int64_t ts = 0;
        std::string type;
        std::string source;
        std::string connection_id;
        std::string method;
        std::string request_id;
        std::string status;
        int code = 0;
        std::string message;
        uint64_t duration_ms = 0;
        std::string payload_preview;
    };

    void PushLocked(const Entry& entry);
    static std::string PreviewJson(const Json& payload);
    static std::string GetStringField(const Json& obj, const char* key);
    static int GetIntField(const Json& obj, const char* key, int default_value);

    std::size_t capacity_;
    mutable std::mutex mu_;
    std::deque<Entry> entries_;
    uint64_t next_id_;
};

} // namespace sdk
} // namespace editor
