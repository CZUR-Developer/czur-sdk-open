#include "sdk_ws_video_server.h"

#include <iostream>

namespace editor {
namespace sdk {

SdkWsVideoServer::SdkWsVideoServer(const std::string& host, int port)
    : host_(host), port_(port), running_(false) {}

bool SdkWsVideoServer::Start() {
    running_ = true;
    std::cout << "[sdk_ws_video_server] listening on ws://" << host_ << ":" << port_ << std::endl;
    return true;
}

void SdkWsVideoServer::Stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    std::cout << "[sdk_ws_video_server] stopped" << std::endl;
}

} // namespace sdk
} // namespace editor

