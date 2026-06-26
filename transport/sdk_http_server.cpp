// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_http_server.h"

#include <cstdlib>
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

std::string JoinPath(const std::string& base, const std::string& child) {
    if (base.empty()) {
        return child;
    }
    if (base[base.size() - 1] == '/') {
        return base + child;
    }
    return base + "/" + child;
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
    if (code == ToCode(SdkStatusCode::InvalidRequest)) {
        return 400;
    }
    if (code == ToCode(SdkStatusCode::InvalidMethod) ||
        code == ToCode(SdkStatusCode::UnsupportedMethod)) {
        return ToHttpStatus(SdkHttpStatus::NotFound);
    }
    if (code == ToCode(SdkStatusCode::AuthRequired) ||
        code == ToCode(SdkStatusCode::TokenInvalid) ||
        code == ToCode(SdkStatusCode::SessionTokenInvalid)) {
        return ToHttpStatus(SdkHttpStatus::Unauthorized);
    }
    if (code == ToCode(SdkStatusCode::CapabilityNotAllowed)) {
        return 403;
    }
    if (code == ToCode(SdkStatusCode::UploadFileEmpty) ||
        code == ToCode(SdkStatusCode::UploadFileTooLarge)) {
        return 400;
    }
    if (code == ToCode(SdkStatusCode::InvalidParams)) {
        return 400;
    }
    return 500;
}

std::size_t ParseSizeParam(const std::string& value, std::size_t default_value, std::size_t max_value) {
    if (value.empty()) {
        return default_value;
    }
    const long parsed = std::strtol(value.c_str(), NULL, 10);
    if (parsed <= 0) {
        return default_value;
    }
    const std::size_t result = static_cast<std::size_t>(parsed);
    return result > max_value ? max_value : result;
}

void SetCorsHeaders(httplib::Response* res) {
    if (res == NULL) {
        return;
    }
    res->set_header("Access-Control-Allow-Origin", "*");
    res->set_header("Access-Control-Allow-Headers", "Authorization, Content-Type");
    res->set_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
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

void SdkHttpServer::SetSystemSupplier(JsonSupplier supplier) {
    system_supplier_ = supplier;
}

void SdkHttpServer::SetAuthSupplier(JsonSupplier supplier) {
    auth_supplier_ = supplier;
}

void SdkHttpServer::SetLogsSupplier(JsonSupplier supplier) {
    logs_supplier_ = supplier;
}

void SdkHttpServer::SetLogReadHandler(LogReadHandler handler) {
    log_read_handler_ = handler;
}

void SdkHttpServer::SetRecordsSupplier(JsonSupplier supplier) {
    records_supplier_ = supplier;
}

void SdkHttpServer::SetConfigSupplier(JsonSupplier supplier) {
    config_supplier_ = supplier;
}

void SdkHttpServer::SetConfigUpdateHandler(JsonUpdateHandler handler) {
    config_update_handler_ = handler;
}

void SdkHttpServer::SetOfflineActivationHandler(OfflineActivationHandler handler) {
    offline_activation_handler_ = handler;
}

void SdkHttpServer::SetAssetResolver(AssetResolver resolver) {
    asset_resolver_ = resolver;
}

void SdkHttpServer::SetImageUploadHandler(ImageUploadHandler handler) {
    image_upload_handler_ = handler;
}

bool SdkHttpServer::ShouldServeSpaFallback(const std::string& path, const std::string& accept_header) {
    if (path.empty() || path[0] != '/') {
        return false;
    }
    if (path == "/") {
        return true;
    }
    if (path.find("/api/") == 0 || path == "/api") {
        return false;
    }
    const std::string::size_type last_slash = path.find_last_of('/');
    const std::string file_name = last_slash == std::string::npos ? path : path.substr(last_slash + 1);
    if (file_name.find('.') != std::string::npos) {
        return false;
    }
    return accept_header.empty() ||
           accept_header.find("text/html") != std::string::npos ||
           accept_header.find("*/*") != std::string::npos;
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
        SetCorsHeaders(&res);
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

    server_->Get("/api/system", [this](const httplib::Request& req, httplib::Response& res) {
        SetCorsHeaders(&res);
        if (!IsAuthorized(req.get_header_value("Authorization"))) {
            res.status = ToHttpStatus(SdkHttpStatus::Unauthorized);
            res.set_content(DumpJson(BuildErrorBody(SdkStatusCode::AuthRequired, "unauthorized")), kJsonContentType);
            return;
        }
        res.status = ToHttpStatus(SdkHttpStatus::Ok);
        res.set_header("Cache-Control", "no-store");
        res.set_content(DumpJson(system_supplier_ ? system_supplier_() : Json::object()), kJsonContentType);
    });

    server_->Get("/api/auth", [this](const httplib::Request& req, httplib::Response& res) {
        SetCorsHeaders(&res);
        if (!IsAuthorized(req.get_header_value("Authorization"))) {
            res.status = ToHttpStatus(SdkHttpStatus::Unauthorized);
            res.set_content(DumpJson(BuildErrorBody(SdkStatusCode::AuthRequired, "unauthorized")), kJsonContentType);
            return;
        }
        res.status = ToHttpStatus(SdkHttpStatus::Ok);
        res.set_header("Cache-Control", "no-store");
        res.set_content(DumpJson(auth_supplier_ ? auth_supplier_() : Json::object()), kJsonContentType);
    });

    server_->Post(R"(/api/auth/sessions/([^/]+)/offline-activation)", [this](const httplib::Request& req, httplib::Response& res) {
        SetCorsHeaders(&res);
        if (!IsAuthorized(req.get_header_value("Authorization"))) {
            res.status = ToHttpStatus(SdkHttpStatus::Unauthorized);
            res.set_content(DumpJson(BuildErrorBody(SdkStatusCode::AuthRequired, "unauthorized")), kJsonContentType);
            return;
        }
        if (!offline_activation_handler_) {
            res.status = 404;
            res.set_content(DumpJson(BuildErrorBody(SdkStatusCode::InvalidMethod, "offline activation api unavailable")), kJsonContentType);
            return;
        }
        Json request_json;
        std::string parse_error;
        if (!TryParseJson(req.body, &request_json, &parse_error) || !request_json.is_object()) {
            res.status = 400;
            res.set_content(DumpJson(BuildErrorBody(SdkStatusCode::InvalidParams, "invalid json")), kJsonContentType);
            return;
        }
        const Json body = offline_activation_handler_(req.matches[1], request_json);
        const int code = body.value("code", 0);
        res.status = code == ToCode(SdkStatusCode::InvalidParams) ? 400 : HttpStatusForSdkCode(code);
        res.set_header("Cache-Control", "no-store");
        res.set_content(DumpJson(body), kJsonContentType);
    });

    server_->Get("/api/logs", [this](const httplib::Request& req, httplib::Response& res) {
        SetCorsHeaders(&res);
        if (!IsAuthorized(req.get_header_value("Authorization"))) {
            res.status = ToHttpStatus(SdkHttpStatus::Unauthorized);
            res.set_content(DumpJson(BuildErrorBody(SdkStatusCode::AuthRequired, "unauthorized")), kJsonContentType);
            return;
        }
        res.status = ToHttpStatus(SdkHttpStatus::Ok);
        res.set_header("Cache-Control", "no-store");
        res.set_content(DumpJson(logs_supplier_ ? logs_supplier_() : Json::object()), kJsonContentType);
    });

    server_->Get(R"(/api/logs/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        SetCorsHeaders(&res);
        if (!IsAuthorized(req.get_header_value("Authorization"))) {
            res.status = ToHttpStatus(SdkHttpStatus::Unauthorized);
            res.set_content(DumpJson(BuildErrorBody(SdkStatusCode::AuthRequired, "unauthorized")), kJsonContentType);
            return;
        }
        if (!log_read_handler_) {
            res.status = 404;
            res.set_content(DumpJson(BuildErrorBody(SdkStatusCode::InvalidMethod, "log api unavailable")), kJsonContentType);
            return;
        }
        const std::size_t tail_bytes = ParseSizeParam(req.get_param_value("tailBytes"), 262144, 1048576);
        const Json body = log_read_handler_(req.matches[1], tail_bytes);
        const int code = body.value("code", 0);
        res.status = HttpStatusForSdkCode(code);
        res.set_header("Cache-Control", "no-store");
        res.set_content(DumpJson(body), kJsonContentType);
    });

    server_->Get("/api/records", [this](const httplib::Request& req, httplib::Response& res) {
        SetCorsHeaders(&res);
        if (!IsAuthorized(req.get_header_value("Authorization"))) {
            res.status = ToHttpStatus(SdkHttpStatus::Unauthorized);
            res.set_content(DumpJson(BuildErrorBody(SdkStatusCode::AuthRequired, "unauthorized")), kJsonContentType);
            return;
        }
        res.status = ToHttpStatus(SdkHttpStatus::Ok);
        res.set_header("Cache-Control", "no-store");
        res.set_content(DumpJson(records_supplier_ ? records_supplier_() : Json::object()), kJsonContentType);
    });

    server_->Get("/api/config", [this](const httplib::Request& req, httplib::Response& res) {
        SetCorsHeaders(&res);
        if (!IsAuthorized(req.get_header_value("Authorization"))) {
            res.status = ToHttpStatus(SdkHttpStatus::Unauthorized);
            res.set_content(DumpJson(BuildErrorBody(SdkStatusCode::AuthRequired, "unauthorized")), kJsonContentType);
            return;
        }
        if (!config_supplier_) {
            res.status = 404;
            res.set_content(DumpJson(BuildErrorBody(SdkStatusCode::InvalidMethod, "config api unavailable")), kJsonContentType);
            return;
        }
        res.status = ToHttpStatus(SdkHttpStatus::Ok);
        res.set_header("Cache-Control", "no-store");
        res.set_content(DumpJson(config_supplier_()), kJsonContentType);
    });

    server_->Post("/api/config", [this](const httplib::Request& req, httplib::Response& res) {
        SetCorsHeaders(&res);
        if (!IsAuthorized(req.get_header_value("Authorization"))) {
            res.status = ToHttpStatus(SdkHttpStatus::Unauthorized);
            res.set_content(DumpJson(BuildErrorBody(SdkStatusCode::AuthRequired, "unauthorized")), kJsonContentType);
            return;
        }
        if (!config_update_handler_) {
            res.status = 404;
            res.set_content(DumpJson(BuildErrorBody(SdkStatusCode::InvalidMethod, "config api unavailable")), kJsonContentType);
            return;
        }
        Json request_json;
        std::string parse_error;
        if (!TryParseJson(req.body, &request_json, &parse_error) || !request_json.is_object()) {
            res.status = 400;
            res.set_content(DumpJson(BuildErrorBody(SdkStatusCode::InvalidParams, "invalid json")), kJsonContentType);
            return;
        }
        const Json body = config_update_handler_(request_json);
        const int code = body.value("code", 0);
        res.status = HttpStatusForSdkCode(code);
        res.set_header("Cache-Control", "no-store");
        res.set_content(DumpJson(body), kJsonContentType);
    });

    server_->Post(R"(/api/uploads/(images|files))", [this](const httplib::Request& req, httplib::Response& res) {
        SetCorsHeaders(&res);
        if (!image_upload_handler_) {
            res.status = 404;
            res.set_content(DumpJson(BuildErrorBody(SdkStatusCode::InvalidMethod, "image upload api unavailable")), kJsonContentType);
            return;
        }
        if (!req.has_file("file")) {
            res.status = 400;
            res.set_content(DumpJson(BuildErrorBody(SdkStatusCode::InvalidParams, "multipart field 'file' required")), kJsonContentType);
            return;
        }
        const httplib::MultipartFormData file = req.get_file_value("file");
        const std::string session_token = ExtractBearerToken(req.get_header_value("Authorization"));
        const UploadResult result = image_upload_handler_(session_token, file.filename, file.content_type, file.content);
        SDK_OPEN_LOG_INFO("[sdk_http_server] upload request handled route=/api/uploads/images filename={} content_type={} code={} message={}",
                          file.filename,
                          file.content_type,
                          result.code,
                          result.message);
        if (!IsOkStatusCode(result.code)) {
            res.status = HttpStatusForSdkCode(result.code);
            res.set_content(DumpJson(BuildErrorBody(result.code, result.message)), kJsonContentType);
            return;
        }
        res.status = ToHttpStatus(SdkHttpStatus::Ok);
        res.set_header("Cache-Control", "no-store");
        res.set_content(DumpJson(result.body), kJsonContentType);
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

    server_->Options(R"(/api/uploads/.*)", [](const httplib::Request&, httplib::Response& res) {
        SetCorsHeaders(&res);
        res.status = 204;
    });

    server_->Options("/api/config", [](const httplib::Request&, httplib::Response& res) {
        SetCorsHeaders(&res);
        res.status = 204;
    });

    server_->Options(R"(/api/(system|auth|logs|records).*)", [](const httplib::Request&, httplib::Response& res) {
        SetCorsHeaders(&res);
        res.status = 204;
    });

    if (mount_static_site_) {
        server_->Get(R"(/.*)", [this](const httplib::Request& req, httplib::Response& res) {
            if (!ShouldServeSpaFallback(req.path, req.get_header_value("Accept"))) {
                res.status = ToHttpStatus(SdkHttpStatus::NotFound);
                res.set_content("Not Found", "text/plain; charset=utf-8");
                return;
            }
            const std::string index_path = JoinPath(document_root_, "index.html");
            if (!FileExists(index_path)) {
                res.status = ToHttpStatus(SdkHttpStatus::NotFound);
                res.set_content("Not Found", "text/plain; charset=utf-8");
                return;
            }
            res.status = ToHttpStatus(SdkHttpStatus::Ok);
            res.set_header("Cache-Control", "no-store");
            res.set_file_content(index_path, "text/html");
        });

        if (!server_->set_mount_point("/", document_root_)) {
            SDK_OPEN_LOG_ERROR("[sdk_http_server] {} set_mount_point failed, root={}", site_name_, document_root_);
            return false;
        }
    }

    server_->set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.status == ToHttpStatus(SdkHttpStatus::NotFound) && res.body.empty()) {
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
