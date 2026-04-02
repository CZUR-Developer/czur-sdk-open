// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_http_server.h"

#include <httplib.h>

#include <iostream>
#include <utility>

namespace editor {
namespace sdk {

namespace {

const char* kJsonContentType = "application/json; charset=utf-8";

Json BuildUnauthorizedJson() {
    return BuildErrorBody(401, "unauthorized");
}

} // namespace

SdkHttpServer::SdkHttpServer(const std::string& site_name,
                             const std::string& host,
                             int port,
                             const std::string& document_root,
                             const std::string& auth_token)
    : site_name_(site_name),
      host_(host),
      port_(port),
      document_root_(document_root),
      auth_token_(auth_token),
      status_api_enabled_(false),
      running_(false) {}

SdkHttpServer::~SdkHttpServer() {
    Stop();
}

void SdkHttpServer::EnableStatusApi(StatusJsonSupplier supplier) {
    status_api_enabled_ = true;
    status_supplier_ = std::move(supplier);
}

bool SdkHttpServer::IsAuthorized(const std::string& authorization) const {
    if (auth_token_.empty()) {
        return true;
    }
    const std::string expected = "Bearer " + auth_token_;
    return authorization == expected;
}

bool SdkHttpServer::ConfigureRoutes() {
    if (!server_) {
        return false;
    }

    if (status_api_enabled_) {
        server_->Get("/api/status", [this](const httplib::Request& req, httplib::Response& res) {
            if (!IsAuthorized(req.get_header_value("Authorization"))) {
                res.status = 401;
                res.set_content(DumpJson(BuildUnauthorizedJson()), kJsonContentType);
                return;
            }
            const Json body = status_supplier_ ? status_supplier_() : Json::object();
            res.status = 200;
            res.set_header("Cache-Control", "no-store");
            res.set_content(DumpJson(body), kJsonContentType);
        });
    }

    if (!server_->set_mount_point("/", document_root_)) {
        std::cerr << "[sdk_http_server] " << site_name_
                  << " set_mount_point failed, root=" << document_root_ << std::endl;
        return false;
    }

    server_->set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (res.status == 404) {
            res.set_content("Not Found", "text/plain; charset=utf-8");
        }
    });
    return true;
}

bool SdkHttpServer::Start() {
    if (running_.load()) {
        return true;
    }

    server_ = std::make_unique<httplib::Server>();
    if (!ConfigureRoutes()) {
        server_.reset();
        return false;
    }

    const int bound_port = server_->bind_to_port(host_, port_);
    if (bound_port < 0) {
        std::cerr << "[sdk_http_server] " << site_name_ << " bind failed: " << host_ << ":" << port_ << std::endl;
        server_.reset();
        return false;
    }

    running_.store(true);
    server_thread_ = std::thread([this]() {
        if (!server_->listen_after_bind()) {
            if (running_.load()) {
                std::cerr << "[sdk_http_server] " << site_name_ << " listen failed" << std::endl;
            }
        }
        running_.store(false);
    });

    std::cout << "[sdk_http_server] " << site_name_
              << " listening on http://" << host_ << ":" << port_
              << ", root=" << document_root_ << std::endl;
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
    std::cout << "[sdk_http_server] " << site_name_ << " stopped" << std::endl;
}

} // namespace sdk
} // namespace editor
