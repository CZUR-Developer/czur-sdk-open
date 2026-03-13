// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_app.h"

#include <iostream>
#include <sstream>
#include <utility>
#include <vector>

namespace editor {
namespace sdk {

namespace {

std::string EscapeJson(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input) {
        switch (c) {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

std::string JsonString(const std::string& value) {
    return "\"" + EscapeJson(value) + "\"";
}

std::string JsonStringArray(const std::vector<std::string>& values) {
    std::ostringstream os;
    os << "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            os << ",";
        }
        os << JsonString(values[i]);
    }
    os << "]";
    return os.str();
}

} // namespace

SdkApp::SdkApp(const SdkConfig& config, ProviderBundle providers)
    : config_(config),
      providers_(std::move(providers)),
      admin_http_server_(
          "admin-site", config_.bind_host, config_.admin_http_port, config_.web_root + "/admin", config_.auth_token),
      demo_http_server_(
          "demo-site", config_.bind_host, config_.demo_http_port, config_.web_root + "/demo", config_.auth_token),
      command_ws_server_(config_.bind_host, config_.command_ws_port, config_.auth_token),
      video_ws_server_(config_.bind_host, config_.video_ws_port, config_.auth_token),
      running_(false),
      start_time_(std::chrono::steady_clock::now()) {
    admin_http_server_.EnableStatusApi([this]() { return BuildStatusJson(); });
    command_ws_server_.SetStatusJsonSupplier([this]() { return BuildStatusJson(); });
    command_ws_server_.SetCapabilitiesJsonSupplier([this]() { return BuildCapabilitiesJson(); });
}

bool SdkApp::Start() {
    if (running_.load()) {
        return true;
    }

    std::cout << "[sdk_app] starting..." << std::endl;
    if (!admin_http_server_.Start()) {
        return false;
    }
    if (!demo_http_server_.Start()) {
        admin_http_server_.Stop();
        return false;
    }
    if (!command_ws_server_.Start()) {
        demo_http_server_.Stop();
        admin_http_server_.Stop();
        return false;
    }
    if (!video_ws_server_.Start()) {
        command_ws_server_.Stop();
        demo_http_server_.Stop();
        admin_http_server_.Stop();
        return false;
    }

    if (providers_.device_provider) {
        std::cout << "[sdk_app] device provider: " << providers_.device_provider->ProviderName() << std::endl;
    }
    if (providers_.graphic_provider) {
        std::cout << "[sdk_app] graphic provider: " << providers_.graphic_provider->ProviderName() << std::endl;
    }
    if (providers_.ocr_provider) {
        std::cout << "[sdk_app] ocr provider: " << providers_.ocr_provider->ProviderName() << std::endl;
    }
    if (providers_.ofd_provider) {
        std::cout << "[sdk_app] ofd provider: " << providers_.ofd_provider->ProviderName() << std::endl;
    }

    start_time_ = std::chrono::steady_clock::now();
    running_.store(true);
    std::cout << "[sdk_app] started" << std::endl;
    return true;
}

void SdkApp::Stop() {
    if (!running_.load()) {
        return;
    }

    std::cout << "[sdk_app] stopping..." << std::endl;
    video_ws_server_.Stop();
    command_ws_server_.Stop();
    demo_http_server_.Stop();
    admin_http_server_.Stop();
    running_.store(false);
    std::cout << "[sdk_app] stopped" << std::endl;
}

bool SdkApp::IsRunning() const {
    return running_.load();
}

uint64_t SdkApp::UptimeSeconds() const {
    if (!running_.load()) {
        return 0;
    }
    const auto elapsed = std::chrono::steady_clock::now() - start_time_;
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count());
}

std::string SdkApp::BuildStatusJson() const {
    const SdkWsCommandServer::Stats command_stats = command_ws_server_.GetStats();
    const SdkWsVideoServer::Stats video_stats = video_ws_server_.GetStats();

    std::ostringstream os;
    os << "{";
    os << "\"running\":" << (running_.load() ? "true" : "false") << ",";
    os << "\"uptimeSec\":" << UptimeSeconds() << ",";
    os << "\"bindHost\":" << JsonString(config_.bind_host) << ",";
    os << "\"ports\":{";
    os << "\"adminHttp\":" << config_.admin_http_port << ",";
    os << "\"demoHttp\":" << config_.demo_http_port << ",";
    os << "\"commandWs\":" << config_.command_ws_port << ",";
    os << "\"videoWs\":" << config_.video_ws_port << "},";
    os << "\"providers\":{";
    os << "\"device\":" << JsonString(providers_.device_provider ? providers_.device_provider->ProviderName() : "") << ",";
    os << "\"graphic\":" << JsonString(providers_.graphic_provider ? providers_.graphic_provider->ProviderName() : "") << ",";
    os << "\"ocr\":" << JsonString(providers_.ocr_provider ? providers_.ocr_provider->ProviderName() : "") << ",";
    os << "\"ofd\":" << JsonString(providers_.ofd_provider ? providers_.ofd_provider->ProviderName() : "") << "},";
    os << "\"ws\":{";
    os << "\"command\":{";
    os << "\"activeConnections\":" << command_stats.active_connections << ",";
    os << "\"authFailed\":" << command_stats.auth_failed << ",";
    os << "\"requestCount\":" << command_stats.request_count << "},";
    os << "\"video\":{";
    os << "\"activeConnections\":" << video_stats.active_connections << ",";
    os << "\"authFailed\":" << video_stats.auth_failed << ",";
    os << "\"binaryFrames\":" << video_stats.binary_frames << ",";
    os << "\"binaryBytes\":" << video_stats.binary_bytes << "}}";
    os << "}";
    return os.str();
}

std::string SdkApp::BuildCapabilitiesJson() const {
    std::vector<std::string> modules;
    if (providers_.device_provider) {
        modules.push_back("device");
    }
    if (providers_.graphic_provider) {
        modules.push_back("graphic");
    }
    if (providers_.ocr_provider) {
        modules.push_back("ocr");
    }
    if (providers_.ofd_provider) {
        modules.push_back("ofd");
    }

    std::vector<std::string> command_methods;
    command_methods.push_back("ping");
    command_methods.push_back("getStatus");
    command_methods.push_back("listCapabilities");

    std::ostringstream os;
    os << "{";
    os << "\"modules\":" << JsonStringArray(modules) << ",";
    os << "\"commandMethods\":" << JsonStringArray(command_methods) << ",";
    os << "\"videoControlTypes\":[\"start\",\"stop\",\"setFormat\"]";
    os << "}";
    return os.str();
}

} // namespace sdk
} // namespace editor
