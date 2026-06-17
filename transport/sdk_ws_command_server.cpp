// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_ws_command_server.h"

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <asio/ip/address.hpp>

#include <exception>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#include "sdk_logger.h"

namespace editor {
namespace sdk {

namespace {

using WsServer = websocketpp::server<websocketpp::config::asio>;
using ConnectionHdl = websocketpp::connection_hdl;
using MessagePtr = WsServer::message_ptr;
using ErrorCode = websocketpp::lib::error_code;

std::string SafeRemoteEndpoint(WsServer& server, ConnectionHdl hdl) {
    try {
        return server.get_con_from_hdl(hdl)->get_remote_endpoint();
    } catch (...) {
        return "<unknown-remote>";
    }
}

} // namespace

class SdkWsCommandServer::Impl {
public:
    WsServer server;
    std::thread io_thread;
    std::set<ConnectionHdl, std::owner_less<ConnectionHdl>> connections;
    std::map<ConnectionHdl, std::string, std::owner_less<ConnectionHdl>> connection_ids;
    std::mutex connections_mu;
    std::atomic<uint64_t> active_connections{0};
    std::atomic<uint64_t> auth_failed{0};
    std::atomic<uint64_t> request_count{0};
    std::atomic<uint64_t> next_connection_id{1};
};

SdkWsCommandServer::SdkWsCommandServer(const std::string& host, int port)
    : host_(host),
      port_(port),
      running_(false) {}

SdkWsCommandServer::~SdkWsCommandServer() {
    Stop();
}

void SdkWsCommandServer::SetRequestHandler(RequestHandler handler) {
    request_handler_ = handler;
}

void SdkWsCommandServer::SetOpenHandler(ConnectionHandler handler) {
    open_handler_ = handler;
}

void SdkWsCommandServer::SetCloseHandler(ConnectionHandler handler) {
    close_handler_ = handler;
}

bool SdkWsCommandServer::SendEvent(const std::string& connection_id, const Json& event) {
    if (!impl_ || connection_id.empty()) {
        return false;
    }
    ConnectionHdl target;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(impl_->connections_mu);
        for (std::map<ConnectionHdl, std::string, std::owner_less<ConnectionHdl>>::const_iterator it =
                 impl_->connection_ids.begin();
             it != impl_->connection_ids.end();
             ++it) {
            if (it->second == connection_id) {
                target = it->first;
                found = true;
                break;
            }
        }
    }
    if (!found) {
        return false;
    }
    impl_->server.get_io_service().post([this, target, event]() {
        ErrorCode ec;
        impl_->server.send(target, DumpJson(event), websocketpp::frame::opcode::text, ec);
    });
    return true;
}

bool SdkWsCommandServer::Start() {
    if (running_.load()) {
        return true;
    }

    impl_.reset(new Impl());
    WsServer& server = impl_->server;
    server.clear_access_channels(websocketpp::log::alevel::all);
    server.clear_error_channels(websocketpp::log::elevel::all);
    server.init_asio();
    server.set_reuse_addr(true);

    server.set_open_handler([this](ConnectionHdl hdl) {
        const std::string connection_id = "conn-" + std::to_string(impl_->next_connection_id.fetch_add(1));
        {
            std::lock_guard<std::mutex> lock(impl_->connections_mu);
            impl_->connections.insert(hdl);
            impl_->connection_ids[hdl] = connection_id;
            impl_->active_connections.store(static_cast<uint64_t>(impl_->connections.size()));
        }
        SDK_OPEN_LOG_INFO("[sdk_ws_command_server] connection opened, id={}, remote={}, active_connections={}",
                          connection_id,
                          SafeRemoteEndpoint(impl_->server, hdl),
                          impl_->active_connections.load());
        if (open_handler_) {
            open_handler_(connection_id);
        }
    });

    server.set_close_handler([this](ConnectionHdl hdl) {
        std::string connection_id;
        {
            std::lock_guard<std::mutex> lock(impl_->connections_mu);
            const std::map<ConnectionHdl, std::string, std::owner_less<ConnectionHdl>>::iterator it =
                impl_->connection_ids.find(hdl);
            if (it != impl_->connection_ids.end()) {
                connection_id = it->second;
                impl_->connection_ids.erase(it);
            }
            impl_->connections.erase(hdl);
            impl_->active_connections.store(static_cast<uint64_t>(impl_->connections.size()));
        }
        SDK_OPEN_LOG_INFO("[sdk_ws_command_server] connection closed, id={}, remote={}, active_connections={}",
                          connection_id,
                          SafeRemoteEndpoint(impl_->server, hdl),
                          impl_->active_connections.load());
        if (!connection_id.empty() && close_handler_) {
            try {
                close_handler_(connection_id);
            } catch (const std::exception& e) {
                SDK_OPEN_LOG_ERROR("[sdk_ws_command_server] close handler failed, id={}, err={}",
                                   connection_id,
                                   e.what());
            } catch (...) {
                SDK_OPEN_LOG_ERROR("[sdk_ws_command_server] close handler failed, id={}, err=<unknown>",
                                   connection_id);
            }
        }
    });

    server.set_message_handler([this](ConnectionHdl hdl, MessagePtr msg) {
        impl_->request_count.fetch_add(1);
        if (msg->get_opcode() != websocketpp::frame::opcode::text) {
            ErrorCode ec;
            impl_->server.send(hdl,
                               DumpJson(BuildWsResponse("", SdkStatusCode::InvalidRequest, "text message required")),
                               websocketpp::frame::opcode::text,
                               ec);
            return;
        }

        std::string connection_id;
        {
            std::lock_guard<std::mutex> lock(impl_->connections_mu);
            const std::map<ConnectionHdl, std::string, std::owner_less<ConnectionHdl>>::const_iterator it =
                impl_->connection_ids.find(hdl);
            if (it != impl_->connection_ids.end()) {
                connection_id = it->second;
            }
        }

        Json request;
        const std::string payload = msg->get_payload();
        if (!TryParseJson(payload, &request) || !request.is_object()) {
            ErrorCode ec;
            impl_->server.send(hdl,
                               DumpJson(BuildWsResponse("", SdkStatusCode::InvalidRequest, "invalid json")),
                               websocketpp::frame::opcode::text,
                               ec);
            return;
        }

        Json response = request_handler_ ? request_handler_(connection_id, request)
                                         : BuildWsResponse("", SdkStatusCode::UnsupportedMethod, "request handler not ready");
        int response_code = ToCode(SdkStatusCode::InternalError);
        const auto code_it = response.find("code");
        if (code_it != response.end() && code_it->is_number_integer()) {
            response_code = code_it->get<int>();
        }
        if (IsAuthStatusCode(response_code)) {
            impl_->auth_failed.fetch_add(1);
        }

        ErrorCode ec;
        impl_->server.send(hdl, DumpJson(response), websocketpp::frame::opcode::text, ec);
    });

    ErrorCode ec;
    const auto addr = asio::ip::make_address(host_, ec);
    if (ec) {
        SDK_OPEN_LOG_ERROR("[sdk_ws_command_server] invalid host: {}, err={}", host_, ec.message());
        impl_.reset();
        return false;
    }

    asio::ip::tcp::endpoint endpoint(addr, static_cast<uint16_t>(port_));
    server.listen(endpoint, ec);
    if (ec) {
        SDK_OPEN_LOG_ERROR("[sdk_ws_command_server] listen failed: {}", ec.message());
        impl_.reset();
        return false;
    }
    server.start_accept(ec);
    if (ec) {
        SDK_OPEN_LOG_ERROR("[sdk_ws_command_server] start_accept failed: {}", ec.message());
        impl_.reset();
        return false;
    }

    running_.store(true);
    impl_->io_thread = std::thread([this]() {
        impl_->server.run();
        running_.store(false);
    });
    SDK_OPEN_LOG_INFO("[sdk_ws_command_server] listening on ws://{}:{}", host_, port_);
    return true;
}

void SdkWsCommandServer::Stop() {
    if (!impl_) {
        return;
    }
    running_.store(false);
    ErrorCode ec;
    impl_->server.stop_listening(ec);
    {
        std::lock_guard<std::mutex> lock(impl_->connections_mu);
        for (std::set<ConnectionHdl, std::owner_less<ConnectionHdl>>::const_iterator it = impl_->connections.begin();
             it != impl_->connections.end();
             ++it) {
            impl_->server.close(*it, websocketpp::close::status::going_away, "server stopping", ec);
        }
        impl_->connections.clear();
        impl_->connection_ids.clear();
        impl_->active_connections.store(0);
    }
    impl_->server.stop();
    if (impl_->io_thread.joinable()) {
        impl_->io_thread.join();
    }
    impl_.reset();
    SDK_OPEN_LOG_INFO("[sdk_ws_command_server] stopped");
}

SdkWsCommandServer::Stats SdkWsCommandServer::GetStats() const {
    Stats stats;
    if (!impl_) {
        return stats;
    }
    stats.active_connections = impl_->active_connections.load();
    stats.auth_failed = impl_->auth_failed.load();
    stats.request_count = impl_->request_count.load();
    return stats;
}

} // namespace sdk
} // namespace editor
