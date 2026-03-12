#include "sdk_ws_command_server.h"

#include <iostream>

namespace editor {
namespace sdk {

SdkWsCommandServer::SdkWsCommandServer(const std::string& host, int port)
    : host_(host), port_(port), running_(false) {}

bool SdkWsCommandServer::Start() {
    running_ = true;
    std::cout << "[sdk_ws_command_server] listening on ws://" << host_ << ":" << port_ << std::endl;
    return true;
}

void SdkWsCommandServer::Stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    std::cout << "[sdk_ws_command_server] stopped" << std::endl;
}

} // namespace sdk
} // namespace editor

