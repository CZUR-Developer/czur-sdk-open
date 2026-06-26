// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "sdk_status_code.h"

namespace editor {
namespace sdk {

class VideoSessionService {
public:
    struct StreamBinding {
        std::string stream_id;
        std::string connection_id;
        std::string session_token;
        std::string device_id;
        std::string pixel_format = "mjpeg";
        int width = 1280;
        int height = 720;
        int fps = 15;
    };

    struct StreamResult {
        int code = ToCode(SdkStatusCode::Ok);
        std::string message = "ok";
        StreamBinding binding;
    };

    struct ValidationResult {
        bool authorized = false;
        int code = ToCode(SdkStatusCode::AuthRequired);
        std::string message = "auth required";
        StreamBinding binding;
    };

    StreamResult RegisterStream(const std::string& connection_id,
                                const std::string& session_token,
                                const std::string& device_id,
                                const std::string& pixel_format,
                                int width,
                                int height,
                                int fps);
    StreamResult StopStream(const std::string& connection_id, const std::string& device_id);
    StreamResult StopStreamById(const std::string& stream_id);
    std::vector<StreamBinding> ClearDevice(const std::string& device_id);
    StreamResult UpdateStreamFormat(const std::string& connection_id,
                                    const std::string& device_id,
                                    const std::string& pixel_format,
                                    int width,
                                    int height,
                                    int fps);
    ValidationResult Validate(const std::string& session_token, const std::string& stream_id) const;
    std::vector<StreamBinding> ClearConnection(const std::string& connection_id);
    std::size_t ActiveStreamCount() const;

private:
    std::string NextStreamId();

    mutable std::mutex streams_mu_;
    std::map<std::string, StreamBinding> streams_;
    std::size_t next_stream_id_ = 1;
};

} // namespace sdk
} // namespace editor
