// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_http_server.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace editor {
namespace sdk {

namespace {

std::string BuildHttpResponse(const std::string& status,
                              const std::string& content_type,
                              const std::string& body) {
    std::ostringstream os;
    os << "HTTP/1.1 " << status << "\r\n";
    os << "Content-Type: " << content_type << "\r\n";
    os << "Content-Length: " << body.size() << "\r\n";
    os << "Connection: close\r\n";
    os << "\r\n";
    os << body;
    return os.str();
}

} // namespace

SdkHttpServer::SdkHttpServer(const std::string& site_name,
                             const std::string& host,
                             int port,
                             const std::string& document_root)
    : site_name_(site_name),
      host_(host),
      port_(port),
      document_root_(document_root),
      running_(false),
      server_fd_(-1) {}

bool SdkHttpServer::Start() {
    if (running_.load()) {
        return true;
    }

    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        std::cerr << "[sdk_http_server] " << site_name_ << " socket() failed: " << std::strerror(errno) << std::endl;
        return false;
    }

    int opt = 1;
    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));
    if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
        std::cerr << "[sdk_http_server] " << site_name_ << " invalid host: " << host_ << std::endl;
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "[sdk_http_server] " << site_name_ << " bind() failed: " << std::strerror(errno) << std::endl;
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    if (::listen(server_fd_, 32) != 0) {
        std::cerr << "[sdk_http_server] " << site_name_ << " listen() failed: " << std::strerror(errno) << std::endl;
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    running_.store(true);
    server_thread_ = std::thread(&SdkHttpServer::ServeLoop, this);
    std::cout << "[sdk_http_server] " << site_name_
              << " listening on http://" << host_ << ":" << port_
              << ", root=" << document_root_ << std::endl;
    return true;
}

void SdkHttpServer::Stop() {
    if (!running_.load()) {
        return;
    }
    running_.store(false);
    if (server_fd_ >= 0) {
        ::shutdown(server_fd_, SHUT_RDWR);
        ::close(server_fd_);
        server_fd_ = -1;
    }
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    std::cout << "[sdk_http_server] " << site_name_ << " stopped" << std::endl;
}

void SdkHttpServer::ServeLoop() {
    while (running_.load()) {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        const int client_fd = ::accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            if (running_.load()) {
                std::cerr << "[sdk_http_server] " << site_name_ << " accept() failed: " << std::strerror(errno) << std::endl;
            }
            continue;
        }
        HandleClient(client_fd);
        ::close(client_fd);
    }
}

void SdkHttpServer::HandleClient(int client_fd) {
    char buffer[8192];
    const ssize_t n = ::recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        return;
    }
    buffer[n] = '\0';
    std::string request(buffer);

    const size_t line_end = request.find("\r\n");
    if (line_end == std::string::npos) {
        const std::string resp = BuildHttpResponse("400 Bad Request", "text/plain", "Bad Request");
        SendAll(client_fd, resp.data(), resp.size());
        return;
    }

    std::istringstream first_line(request.substr(0, line_end));
    std::string method;
    std::string path;
    std::string version;
    first_line >> method >> path >> version;

    if (method != "GET") {
        const std::string resp = BuildHttpResponse("405 Method Not Allowed", "text/plain", "Method Not Allowed");
        SendAll(client_fd, resp.data(), resp.size());
        return;
    }

    const std::string resolved_path = ResolveRequestPath(path);
    if (resolved_path.empty()) {
        const std::string resp = BuildHttpResponse("403 Forbidden", "text/plain", "Forbidden");
        SendAll(client_fd, resp.data(), resp.size());
        return;
    }

    std::ifstream file(resolved_path.c_str(), std::ios::binary);
    if (!file.is_open()) {
        const std::string resp = BuildHttpResponse("404 Not Found", "text/plain", "Not Found");
        SendAll(client_fd, resp.data(), resp.size());
        return;
    }

    std::ostringstream content;
    content << file.rdbuf();
    const std::string body = content.str();
    const std::string header = BuildHttpResponse("200 OK", GuessContentType(resolved_path), body);
    SendAll(client_fd, header.data(), header.size());
}

std::string SdkHttpServer::ResolveRequestPath(const std::string& request_path) const {
    std::string path = request_path;
    const size_t query_pos = path.find('?');
    if (query_pos != std::string::npos) {
        path = path.substr(0, query_pos);
    }
    if (path.empty() || path == "/") {
        path = "/index.html";
    }
    if (path.find("..") != std::string::npos) {
        return "";
    }

    std::string resolved = document_root_;
    if (!resolved.empty() && resolved[resolved.size() - 1] != '/') {
        resolved.push_back('/');
    }
    if (!path.empty() && path[0] == '/') {
        resolved += path.substr(1);
    } else {
        resolved += path;
    }

    struct stat st;
    if (::stat(resolved.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        if (!resolved.empty() && resolved[resolved.size() - 1] != '/') {
            resolved.push_back('/');
        }
        resolved += "index.html";
    }
    return resolved;
}

bool SdkHttpServer::SendAll(int fd, const char* data, size_t size) {
    size_t sent = 0;
    while (sent < size) {
        const ssize_t n = ::send(fd, data + sent, size - sent, 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

std::string SdkHttpServer::GuessContentType(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return "application/octet-stream";
    }
    const std::string ext = path.substr(dot + 1);
    if (ext == "html") return "text/html; charset=utf-8";
    if (ext == "css") return "text/css; charset=utf-8";
    if (ext == "js") return "application/javascript; charset=utf-8";
    if (ext == "json") return "application/json; charset=utf-8";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "ico") return "image/x-icon";
    return "application/octet-stream";
}

} // namespace sdk
} // namespace editor
