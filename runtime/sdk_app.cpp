// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_app.h"

#include <ctime>
#include <iostream>
#include <utility>

namespace editor {
namespace sdk {

namespace {

#if SDK_OPEN_ENABLE_HTTP_SERVER
constexpr bool kSdkOpenHttpServerEnabled = true;
#else
constexpr bool kSdkOpenHttpServerEnabled = false;
#endif

std::string MaskApiKey(const std::string& api_key) {
    if (api_key.empty()) {
        return "<empty>";
    }
    if (api_key.size() <= 16) {
        return api_key.substr(0, api_key.size() / 2) + "...(len=" + std::to_string(api_key.size()) + ")";
    }
    return api_key.substr(0, 8) + "..." + api_key.substr(api_key.size() - 6) +
           " (len=" + std::to_string(api_key.size()) + ")";
}

} // namespace

SdkApp::SdkApp(const SdkConfig& config, ProviderBundle providers)
    : config_(config),
      providers_(std::move(providers)),
      command_dispatcher_(new SdkCommandDispatcher(config_, providers_)),
      admin_http_server_(
          "admin-site", config_.bind_host, config_.admin_http_port, config_.web_root + "/admin", config_.auth_token),
      demo_http_server_(
          "demo-site", config_.bind_host, config_.demo_http_port, config_.web_root + "/demo", config_.auth_token),
      command_ws_server_(config_.bind_host, config_.command_ws_port),
      video_ws_server_(config_.bind_host, config_.video_ws_port, config_.auth_token),
      running_(false),
      start_time_(std::chrono::steady_clock::now()) {
    if (kSdkOpenHttpServerEnabled) {
        admin_http_server_.EnableStatusApi([this]() { return BuildStatusJson(); });
    }
    command_dispatcher_->SetStatusSupplier([this]() { return BuildStatusJson(); });
    command_ws_server_.SetConnectionAuthHandler([this](const std::string& api_key) {
        SdkWsCommandServer::ConnectionAuthResult result;
        const std::string provider_name = providers_.auth_provider ? providers_.auth_provider->ProviderName() : "<none>";
        std::cout << "[sdk_app] command ws auth validate begin"
                  << ", provider=" << provider_name
                  << ", api_key=" << MaskApiKey(api_key)
                  << ", now_ts=" << static_cast<std::int64_t>(std::time(nullptr))
                  << std::endl;
        if (!providers_.auth_provider) {
            result.code = ToCode(SdkStatusCode::ProviderNotReady);
            result.message = "provider not ready";
            std::cerr << "[sdk_app] command ws auth validate failed"
                      << ", provider=" << provider_name
                      << ", code=" << result.code
                      << ", message=" << result.message
                      << std::endl;
            return result;
        }
        if (api_key.empty()) {
            result.code = ToCode(SdkStatusCode::AuthRequired);
            result.message = "api key required";
            std::cerr << "[sdk_app] command ws auth validate failed"
                      << ", provider=" << provider_name
                      << ", code=" << result.code
                      << ", message=" << result.message
                      << std::endl;
            return result;
        }

        const std::int64_t now_ts = static_cast<std::int64_t>(std::time(nullptr));
        AuthValidateRequest validate_request;
        validate_request.api_key = api_key;
        validate_request.now_ts = now_ts;
        const AuthValidateResult validate = providers_.auth_provider->ValidateApiKey(validate_request);
        result.authorized = IsOkStatusCode(validate.code);
        result.code = validate.code;
        result.message = validate.message;
        std::cout << "[sdk_app] command ws auth validate result"
                  << ", provider=" << provider_name
                  << ", authorized=" << (result.authorized ? "true" : "false")
                  << ", code=" << result.code
                  << ", message=" << result.message
                  << std::endl;
        return result;
    });
    command_ws_server_.SetRequestHandler([this](const Json& request) { return command_dispatcher_->Dispatch(request); });
    command_ws_server_.SetStatusJsonSupplier([this]() { return BuildStatusJson(); });
    command_ws_server_.SetCapabilitiesJsonSupplier([this]() { return BuildCapabilitiesJson(); });
}

bool SdkApp::Start() {
    if (running_.load()) {
        return true;
    }

    std::cout << "[sdk_app] starting..." << std::endl;
    if (kSdkOpenHttpServerEnabled) {
        if (!admin_http_server_.Start()) {
            return false;
        }
        if (!demo_http_server_.Start()) {
            admin_http_server_.Stop();
            return false;
        }
    } else {
        std::cout << "[sdk_app] embedded http server disabled by SDK_OPEN_ENABLE_HTTP_SERVER=0" << std::endl;
    }
    if (!command_ws_server_.Start()) {
        if (kSdkOpenHttpServerEnabled) {
            demo_http_server_.Stop();
            admin_http_server_.Stop();
        }
        return false;
    }
    if (!video_ws_server_.Start()) {
        command_ws_server_.Stop();
        if (kSdkOpenHttpServerEnabled) {
            demo_http_server_.Stop();
            admin_http_server_.Stop();
        }
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
    if (providers_.auth_provider) {
        std::cout << "[sdk_app] auth provider: " << providers_.auth_provider->ProviderName() << std::endl;
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
    if (kSdkOpenHttpServerEnabled) {
        demo_http_server_.Stop();
        admin_http_server_.Stop();
    }
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

Json SdkApp::BuildStatusJson() const {
    const SdkWsCommandServer::Stats command_stats = command_ws_server_.GetStats();
    const SdkWsVideoServer::Stats video_stats = video_ws_server_.GetStats();

    return Json{
        {"running", running_.load()},
        {"uptimeSec", UptimeSeconds()},
        {"bindHost", config_.bind_host},
        {"http",
         {
             {"enabled", kSdkOpenHttpServerEnabled},
             {"sites",
              {
                  {"admin", config_.admin_http_port},
                  {"demo", config_.demo_http_port},
              }},
         }},
        {"ports",
         {
             {"adminHttp", config_.admin_http_port},
             {"demoHttp", config_.demo_http_port},
             {"commandWs", config_.command_ws_port},
             {"videoWs", config_.video_ws_port},
         }},
        {"providers",
         {
             {"device", providers_.device_provider ? providers_.device_provider->ProviderName() : ""},
             {"graphic", providers_.graphic_provider ? providers_.graphic_provider->ProviderName() : ""},
             {"ocr", providers_.ocr_provider ? providers_.ocr_provider->ProviderName() : ""},
             {"ofd", providers_.ofd_provider ? providers_.ofd_provider->ProviderName() : ""},
             {"auth", providers_.auth_provider ? providers_.auth_provider->ProviderName() : ""},
         }},
        {"ws",
         {
             {"command",
              {
                  {"activeConnections", command_stats.active_connections},
                  {"authFailed", command_stats.auth_failed},
                  {"requestCount", command_stats.request_count},
              }},
             {"video",
              {
                  {"activeConnections", video_stats.active_connections},
                  {"authFailed", video_stats.auth_failed},
                  {"binaryFrames", video_stats.binary_frames},
                  {"binaryBytes", video_stats.binary_bytes},
              }},
         }},
    };
}

Json SdkApp::BuildCapabilitiesJson() const {
    return command_dispatcher_ ? command_dispatcher_->BuildCapabilitiesJson() : Json::object();
}

} // namespace sdk
} // namespace editor
