// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "video_session_service.h"

namespace editor {
namespace sdk {

std::string VideoSessionService::NextStreamId() {
    return "stream-" + std::to_string(next_stream_id_++);
}

VideoSessionService::StreamResult VideoSessionService::RegisterStream(const std::string& connection_id,
                                                                      const std::string& session_token,
                                                                      const std::string& device_id,
                                                                      const std::string& pixel_format,
                                                                      int width,
                                                                      int height,
                                                                      int fps) {
    StreamResult result;
    std::lock_guard<std::mutex> lock(streams_mu_);
    result.binding.stream_id = NextStreamId();
    result.binding.connection_id = connection_id;
    result.binding.session_token = session_token;
    result.binding.device_id = device_id;
    result.binding.pixel_format = pixel_format;
    result.binding.width = width;
    result.binding.height = height;
    result.binding.fps = fps;
    streams_[result.binding.stream_id] = result.binding;
    return result;
}

VideoSessionService::StreamResult VideoSessionService::StopStream(const std::string& connection_id,
                                                                  const std::string& device_id) {
    StreamResult result;
    std::lock_guard<std::mutex> lock(streams_mu_);
    for (std::map<std::string, StreamBinding>::iterator it = streams_.begin(); it != streams_.end(); ++it) {
        if (it->second.connection_id == connection_id && it->second.device_id == device_id) {
            result.binding = it->second;
            streams_.erase(it);
            return result;
        }
    }
    result.code = ToCode(SdkStatusCode::StreamNotFound);
    result.message = "stream not found";
    return result;
}

VideoSessionService::StreamResult VideoSessionService::StopStreamById(const std::string& stream_id) {
    StreamResult result;
    std::lock_guard<std::mutex> lock(streams_mu_);
    const std::map<std::string, StreamBinding>::iterator it = streams_.find(stream_id);
    if (it == streams_.end()) {
        result.code = ToCode(SdkStatusCode::StreamNotFound);
        result.message = "stream not found";
        return result;
    }
    result.binding = it->second;
    streams_.erase(it);
    return result;
}

std::vector<VideoSessionService::StreamBinding> VideoSessionService::ClearDevice(const std::string& device_id) {
    std::vector<StreamBinding> removed;
    std::lock_guard<std::mutex> lock(streams_mu_);
    for (std::map<std::string, StreamBinding>::iterator it = streams_.begin(); it != streams_.end();) {
        if (it->second.device_id == device_id) {
            removed.push_back(it->second);
            it = streams_.erase(it);
        } else {
            ++it;
        }
    }
    return removed;
}

VideoSessionService::StreamResult VideoSessionService::UpdateStreamFormat(const std::string& connection_id,
                                                                          const std::string& device_id,
                                                                          const std::string& pixel_format,
                                                                          int width,
                                                                          int height,
                                                                          int fps) {
    StreamResult result;
    std::lock_guard<std::mutex> lock(streams_mu_);
    for (std::map<std::string, StreamBinding>::iterator it = streams_.begin(); it != streams_.end(); ++it) {
        if (it->second.connection_id == connection_id && it->second.device_id == device_id) {
            it->second.pixel_format = pixel_format;
            it->second.width = width;
            it->second.height = height;
            it->second.fps = fps;
            result.binding = it->second;
            return result;
        }
    }
    result.code = ToCode(SdkStatusCode::StreamNotFound);
    result.message = "stream not found";
    return result;
}

VideoSessionService::ValidationResult VideoSessionService::Validate(const std::string& session_token,
                                                                    const std::string& stream_id) const {
    ValidationResult result;
    if (session_token.empty() || stream_id.empty()) {
        result.code = ToCode(SdkStatusCode::AuthRequired);
        result.message = "session_token and stream_id required";
        return result;
    }

    std::lock_guard<std::mutex> lock(streams_mu_);
    const std::map<std::string, StreamBinding>::const_iterator it = streams_.find(stream_id);
    if (it == streams_.end()) {
        result.code = ToCode(SdkStatusCode::StreamNotFound);
        result.message = "stream not found";
        return result;
    }
    if (it->second.session_token != session_token) {
        result.code = ToCode(SdkStatusCode::SessionTokenInvalid);
        result.message = "session token invalid";
        return result;
    }

    result.authorized = true;
    result.code = ToCode(SdkStatusCode::Ok);
    result.message = "ok";
    result.binding = it->second;
    return result;
}

std::vector<VideoSessionService::StreamBinding> VideoSessionService::ClearConnection(const std::string& connection_id) {
    std::vector<StreamBinding> removed;
    std::lock_guard<std::mutex> lock(streams_mu_);
    for (std::map<std::string, StreamBinding>::iterator it = streams_.begin(); it != streams_.end();) {
        if (it->second.connection_id == connection_id) {
            removed.push_back(it->second);
            it = streams_.erase(it);
        } else {
            ++it;
        }
    }
    return removed;
}

std::vector<VideoSessionService::StreamBinding> VideoSessionService::ClearAll() {
    std::vector<StreamBinding> removed;
    std::lock_guard<std::mutex> lock(streams_mu_);
    for (std::map<std::string, StreamBinding>::const_iterator it = streams_.begin(); it != streams_.end(); ++it) {
        removed.push_back(it->second);
    }
    streams_.clear();
    return removed;
}

std::size_t VideoSessionService::ActiveStreamCount() const {
    std::lock_guard<std::mutex> lock(streams_mu_);
    return streams_.size();
}

} // namespace sdk
} // namespace editor
