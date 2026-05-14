// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_app.h"

#include <utility>

#include "sdk_logger.h"

namespace editor {
namespace sdk {

namespace {
#if SDK_OPEN_ENABLE_HTTP_SERVER
constexpr bool kSdkOpenHttpServerEnabled = true;
#else
constexpr bool kSdkOpenHttpServerEnabled = false;
#endif

std::string BuildAssetBaseUrl(const SdkConfig& config) {
    if (!config.asset_base_url.empty()) {
        return config.asset_base_url;
    }
    const std::string host = config.bind_host == "0.0.0.0" ? "127.0.0.1" : config.bind_host;
    return "http://" + host + ":" + std::to_string(config.asset_http_port);
}

} // namespace

SdkApp::SdkApp(const SdkConfig& config, ProviderBundle providers)
    : config_(config),
      providers_(std::move(providers)),
      command_application_service_(new CommandApplicationService(config_, providers_)),
      admin_http_server_(
          "admin-site", config_.bind_host, config_.admin_http_port, config_.web_root + "/admin", config_.auth_token),
      demo_http_server_(
          "demo-site", config_.bind_host, config_.demo_http_port, config_.web_root + "/demo", config_.auth_token),
      asset_http_server_(
          "asset-api", config_.bind_host, config_.asset_http_port, "", config_.auth_token, false),
      command_ws_server_(config_.bind_host, config_.command_ws_port),
      video_ws_server_(config_.bind_host, config_.video_ws_port),
      running_(false),
      start_time_(std::chrono::steady_clock::now()) {
    admin_application_service_.SetStatusSupplier([this]() { return BuildStatusJson(); });
    if (kSdkOpenHttpServerEnabled) {
        admin_http_server_.SetHealthSupplier([this]() { return admin_application_service_.BuildHealthJson(); });
        admin_http_server_.SetStatusSupplier([this]() { return admin_application_service_.BuildStatusJson(); });
        demo_http_server_.SetHealthSupplier([this]() { return admin_application_service_.BuildHealthJson(); });
        demo_http_server_.SetStatusSupplier([this]() { return admin_application_service_.BuildStatusJson(); });
    }
    asset_http_server_.SetHealthSupplier([this]() { return admin_application_service_.BuildHealthJson(); });
    SdkHttpServer::AssetResolver asset_resolver =
            [this](const std::string& session_token, const std::string& task_id, const std::string& asset_id) {
                SdkHttpServer::AssetResult result;
                if (!command_application_service_) {
                    result.code = ToCode(SdkStatusCode::InternalError);
                    result.message = "command service not ready";
                    return result;
                }
                const CommandApplicationService::AssetAccessResult asset_result =
                        command_application_service_->ResolveAsset(session_token, task_id, asset_id);
                result.code = asset_result.code;
                result.message = asset_result.message;
                result.asset = asset_result.asset;
                return result;
            };
    asset_http_server_.SetAssetResolver(asset_resolver);
    asset_http_server_.SetImageUploadHandler(
        [this](const std::string& session_token,
               const std::string& filename,
               const std::string& content_type,
               const std::string& content) {
            SdkHttpServer::UploadResult result;
            if (!command_application_service_) {
                result.code = ToCode(SdkStatusCode::InternalError);
                result.message = "command service not ready";
                return result;
            }
            const CommandApplicationService::ImageUploadResult upload_result =
                command_application_service_->UploadImage(session_token, filename, content_type, content);
            result.code = upload_result.code;
            result.message = upload_result.message;
            result.body = Json{{"upload_id", upload_result.upload_id},
                               {"asset",
                                Json{{"asset_id", upload_result.asset.asset_id},
                                     {"kind", upload_result.asset.kind},
                                     {"path", upload_result.asset.path},
                                     {"url", upload_result.asset.url},
                                     {"download_url", upload_result.asset.download_url},
                                     {"content_type", upload_result.asset.content_type},
                                     {"width", upload_result.asset.width},
                                     {"height", upload_result.asset.height},
                                     {"size", upload_result.asset.size}}}};
            return result;
        });
    command_application_service_->SetStatusSupplier([this]() { return BuildStatusJson(); });
    command_application_service_->SetVideoFrameSink([this](const SdkVideoFrame& frame) {
        video_ws_server_.PublishFrame(frame);
    });
    command_application_service_->SetVideoStreamClosedSink([this](const std::string& stream_id) {
        video_ws_server_.CloseStream(stream_id);
    });
    command_application_service_->SetCommandEventSink([this](const std::string& connection_id, const Json& event) {
        command_ws_server_.SendEvent(connection_id, event);
    });
    command_ws_server_.SetRequestHandler(
        [this](const std::string& connection_id, const Json& request) { return command_application_service_->HandleRequest(connection_id, request); });
    command_ws_server_.SetCloseHandler([this](const std::string& connection_id) {
        if (command_application_service_) {
            command_application_service_->OnConnectionClosed(connection_id);
        }
    });
    video_ws_server_.SetConnectionAuthHandler([this](const std::string& session_token, const std::string& stream_id) {
        SdkWsVideoServer::ConnectionAuthResult result;
        const VideoSessionService::ValidationResult validation =
            command_application_service_->ValidateVideoStream(session_token, stream_id);
        result.authorized = validation.authorized;
        result.code = validation.code;
        result.message = validation.message;
        result.connection_id = validation.binding.connection_id;
        result.stream_id = validation.binding.stream_id;
        return result;
    });
}

bool SdkApp::Start() {
    if (running_.load()) {
        return true;
    }

    SDK_OPEN_LOG_INFO("[sdk_app] starting...");
    if (kSdkOpenHttpServerEnabled) {
        if (!admin_http_server_.Start()) {
            return false;
        }
        if (!demo_http_server_.Start()) {
            admin_http_server_.Stop();
            return false;
        }
    } else {
        SDK_OPEN_LOG_INFO("[sdk_app] embedded http server disabled by SDK_OPEN_ENABLE_HTTP_SERVER=0");
    }
    if (!asset_http_server_.Start()) {
        demo_http_server_.Stop();
        admin_http_server_.Stop();
        return false;
    }
    if (!command_ws_server_.Start()) {
        asset_http_server_.Stop();
        if (kSdkOpenHttpServerEnabled) {
            demo_http_server_.Stop();
            admin_http_server_.Stop();
        }
        return false;
    }
    if (!video_ws_server_.Start()) {
        command_ws_server_.Stop();
        asset_http_server_.Stop();
        if (kSdkOpenHttpServerEnabled) {
            demo_http_server_.Stop();
            admin_http_server_.Stop();
        }
        return false;
    }

    if (providers_.device_provider) {
        SDK_OPEN_LOG_INFO("[sdk_app] device provider: {}", providers_.device_provider->ProviderName());
    }
    if (providers_.graphic_provider) {
        SDK_OPEN_LOG_INFO("[sdk_app] graphic provider: {}", providers_.graphic_provider->ProviderName());
    }
    if (providers_.ocr_provider) {
        SDK_OPEN_LOG_INFO("[sdk_app] ocr provider: {}", providers_.ocr_provider->ProviderName());
    }
    if (providers_.ofd_provider) {
        SDK_OPEN_LOG_INFO("[sdk_app] ofd provider: {}", providers_.ofd_provider->ProviderName());
    }
    if (providers_.auth_provider) {
        SDK_OPEN_LOG_INFO("[sdk_app] auth provider: {}", providers_.auth_provider->ProviderName());
    }

    start_time_ = std::chrono::steady_clock::now();
    running_.store(true);
    SDK_OPEN_LOG_INFO("[sdk_app] started");
    return true;
}

void SdkApp::Stop() {
    if (!running_.load()) {
        return;
    }

    SDK_OPEN_LOG_INFO("[sdk_app] stopping...");
    video_ws_server_.Stop();
    command_ws_server_.Stop();
    if (kSdkOpenHttpServerEnabled) {
        asset_http_server_.Stop();
        demo_http_server_.Stop();
        admin_http_server_.Stop();
    }
    running_.store(false);
    SDK_OPEN_LOG_INFO("[sdk_app] stopped");
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
                  {"asset", config_.asset_http_port},
              }},
             {"assetBaseUrl", BuildAssetBaseUrl(config_)},
         }},
        {"ports",
         {
             {"adminHttp", config_.admin_http_port},
             {"demoHttp", config_.demo_http_port},
             {"assetHttp", config_.asset_http_port},
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
        {"application",
         {
             {"boundSessions", command_application_service_ ? command_application_service_->ActiveSessionCount() : 0},
             {"activeStreams", command_application_service_ ? command_application_service_->ActiveStreamCount() : 0},
         }},
    };
}

} // namespace sdk
} // namespace editor
