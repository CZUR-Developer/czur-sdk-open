// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_http_server.h"

#include <httplib.h>
#include <sys/stat.h>

#include "sdk_logger.h"

namespace editor {
namespace sdk {

namespace {

const char* kJsonContentType = "application/json; charset=utf-8";

bool FileExists(const std::string& path) {
    struct stat st;
    return !path.empty() && ::stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::string ExtractBearerToken(const std::string& authorization) {
    const std::string prefix = "Bearer ";
    if (authorization.size() <= prefix.size() || authorization.substr(0, prefix.size()) != prefix) {
        return "";
    }
    return authorization.substr(prefix.size());
}

int HttpStatusForSdkCode(int code) {
    if (IsOkStatusCode(code)) {
        return ToHttpStatus(SdkHttpStatus::Ok);
    }
    if (code == ToCode(SdkStatusCode::AuthRequired) ||
        code == ToCode(SdkStatusCode::TokenInvalid) ||
        code == ToCode(SdkStatusCode::SessionTokenInvalid)) {
        return ToHttpStatus(SdkHttpStatus::Unauthorized);
    }
    if (code == ToCode(SdkStatusCode::CapabilityNotAllowed)) {
        return 403;
    }
    if (code == ToCode(SdkStatusCode::InvalidParams)) {
        return ToHttpStatus(SdkHttpStatus::NotFound);
    }
    return 500;
}

void SetCorsHeaders(httplib::Response* res) {
    if (res == NULL) {
        return;
    }
    res->set_header("Access-Control-Allow-Origin", "*");
    res->set_header("Access-Control-Allow-Headers", "Authorization, Content-Type");
    res->set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
}

} // namespace

SdkHttpServer::SdkHttpServer(const std::string& site_name,
                             const std::string& host,
                             int port,
                             const std::string& document_root,
                             const std::string& auth_token,
                             bool mount_static_site)
    : site_name_(site_name),
      host_(host),
      port_(port),
      document_root_(document_root),
      auth_token_(auth_token),
      mount_static_site_(mount_static_site),
      running_(false) {}

SdkHttpServer::~SdkHttpServer() {
    Stop();
}

void SdkHttpServer::SetHealthSupplier(JsonSupplier supplier) {
    health_supplier_ = supplier;
}

void SdkHttpServer::SetStatusSupplier(JsonSupplier supplier) {
    status_supplier_ = supplier;
}

void SdkHttpServer::SetAssetResolver(AssetResolver resolver) {
    asset_resolver_ = resolver;
}

bool SdkHttpServer::IsAuthorized(const std::string& authorization) const {
    if (auth_token_.empty()) {
        return true;
    }
    return authorization == "Bearer " + auth_token_;
}

bool SdkHttpServer::ConfigureRoutes() {
    if (!server_) {
        return false;
    }

    server_->Get("/healthz", [this](const httplib::Request&, httplib::Response& res) {
        const Json body = health_supplier_ ? health_supplier_() : Json{{"ok", true}};
        res.status = ToHttpStatus(SdkHttpStatus::Ok);
        res.set_header("Cache-Control", "no-store");
        res.set_content(DumpJson(body), kJsonContentType);
    });

    server_->Get("/api/status", [this](const httplib::Request& req, httplib::Response& res) {
        if (!IsAuthorized(req.get_header_value("Authorization"))) {
            res.status = ToHttpStatus(SdkHttpStatus::Unauthorized);
            res.set_content(DumpJson(BuildErrorBody(SdkStatusCode::AuthRequired, "unauthorized")), kJsonContentType);
            return;
        }
        const Json body = status_supplier_ ? status_supplier_() : Json::object();
        res.status = ToHttpStatus(SdkHttpStatus::Ok);
        res.set_header("Cache-Control", "no-store");
        res.set_content(DumpJson(body), kJsonContentType);
    });

    server_->Get(R"(/api/assets/([^/]+)/([^/]+)/download)", [this](const httplib::Request& req, httplib::Response& res) {
        SetCorsHeaders(&res);
        if (!asset_resolver_) {
            res.status = 404;
            res.set_content(DumpJson(BuildErrorBody(SdkStatusCode::InvalidMethod, "asset api unavailable")), kJsonContentType);
            return;
        }
        const std::string session_token = ExtractBearerToken(req.get_header_value("Authorization"));
        const AssetResult result = asset_resolver_(session_token, req.matches[1], req.matches[2]);
        if (!IsOkStatusCode(result.code)) {
            res.status = HttpStatusForSdkCode(result.code);
            res.set_content(DumpJson(BuildErrorBody(result.code, result.message)), kJsonContentType);
            return;
        }
        if (!FileExists(result.asset.path)) {
            res.status = ToHttpStatus(SdkHttpStatus::NotFound);
            res.set_content(DumpJson(BuildErrorBody(SdkStatusCode::InvalidParams, "asset file not found")), kJsonContentType);
            return;
        }
        res.status = ToHttpStatus(SdkHttpStatus::Ok);
        res.set_header("Cache-Control", "no-store");
        res.set_header("Content-Disposition", "attachment; filename=\"" + result.asset.asset_id + "\"");
        res.set_file_content(result.asset.path, result.asset.content_type.empty() ? "application/octet-stream" : result.asset.content_type);
    });

    server_->Get(R"(/api/assets/([^/]+)/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        SetCorsHeaders(&res);
        if (!asset_resolver_) {
            res.status = 404;
            res.set_content(DumpJson(BuildErrorBody(SdkStatusCode::InvalidMethod, "asset api unavailable")), kJsonContentType);
            return;
        }
        const std::string session_token = ExtractBearerToken(req.get_header_value("Authorization"));
        const AssetResult result = asset_resolver_(session_token, req.matches[1], req.matches[2]);
        if (!IsOkStatusCode(result.code)) {
            res.status = HttpStatusForSdkCode(result.code);
            res.set_content(DumpJson(BuildErrorBody(result.code, result.message)), kJsonContentType);
            return;
        }
        if (!FileExists(result.asset.path)) {
            res.status = ToHttpStatus(SdkHttpStatus::NotFound);
            res.set_content(DumpJson(BuildErrorBody(SdkStatusCode::InvalidParams, "asset file not found")), kJsonContentType);
            return;
        }
        res.status = ToHttpStatus(SdkHttpStatus::Ok);
        res.set_header("Cache-Control", "no-store");
        res.set_header("Content-Disposition", "inline");
        res.set_file_content(result.asset.path, result.asset.content_type.empty() ? "application/octet-stream" : result.asset.content_type);
    });

    server_->Options(R"(/api/assets/.*)", [](const httplib::Request&, httplib::Response& res) {
        SetCorsHeaders(&res);
        res.status = 204;
    });

    if (mount_static_site_) {
        if (!server_->set_mount_point("/", document_root_)) {
            SDK_OPEN_LOG_ERROR("[sdk_http_server] {} set_mount_point failed, root={}", site_name_, document_root_);
            return false;
        }
    }

    server_->set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.status == ToHttpStatus(SdkHttpStatus::NotFound)) {
            res.set_content("Not Found", "text/plain; charset=utf-8");
        }
    });
    return true;
}

bool SdkHttpServer::Start() {
    if (running_.load()) {
        return true;
    }
    server_.reset(new httplib::Server());
    if (!ConfigureRoutes()) {
        server_.reset();
        return false;
    }

    const int bound_port = server_->bind_to_port(host_, port_);
    if (bound_port < 0) {
        SDK_OPEN_LOG_ERROR("[sdk_http_server] {} bind failed: {}:{}", site_name_, host_, port_);
        server_.reset();
        return false;
    }

    running_.store(true);
    server_thread_ = std::thread([this]() {
        if (!server_->listen_after_bind() && running_.load()) {
            SDK_OPEN_LOG_ERROR("[sdk_http_server] {} listen failed", site_name_);
        }
        running_.store(false);
    });

    SDK_OPEN_LOG_INFO("[sdk_http_server] {} listening on http://{}:{}, root={}", site_name_, host_, port_, document_root_);
    return true;
}

void SdkHttpServer::Stop() {
    if (!server_) {
        return;
    }
    running_.store(false);
    server_->stop();
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    server_.reset();
    SDK_OPEN_LOG_INFO("[sdk_http_server] {} stopped", site_name_);
}

} // namespace sdk
} // namespace editor
