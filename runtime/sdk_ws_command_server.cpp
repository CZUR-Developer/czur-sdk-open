// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_ws_command_server.h"

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <asio/ip/address.hpp>

#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>

namespace editor {
namespace sdk {

namespace {

using WsServer = websocketpp::server<websocketpp::config::asio>;
using ConnectionHdl = websocketpp::connection_hdl;
using MessagePtr = WsServer::message_ptr;
using ErrorCode = websocketpp::lib::error_code;

std::string QueryValue(const std::string& query, const std::string& key) {
    size_t pos = 0;
    while (pos < query.size()) {
        const size_t amp_pos = query.find('&', pos);
        const std::string kv = query.substr(pos, amp_pos == std::string::npos ? std::string::npos : amp_pos - pos);
        const size_t eq_pos = kv.find('=');
        const std::string k = kv.substr(0, eq_pos);
        const std::string v = eq_pos == std::string::npos ? "" : kv.substr(eq_pos + 1);
        if (k == key) {
            return v;
        }
        if (amp_pos == std::string::npos) {
            break;
        }
        pos = amp_pos + 1;
    }
    return "";
}

std::string GetOptionalStringField(const Json& obj, const char* key) {
    const auto it = obj.find(key);
    if (it != obj.end() && it->is_string()) {
        return it->get<std::string>();
    }
    return "";
}

} // namespace

class SdkWsCommandServer::Impl {
public:
    WsServer server;
    std::thread io_thread;
    std::set<ConnectionHdl, std::owner_less<ConnectionHdl>> connections;
    std::mutex connections_mu;
    std::atomic<uint64_t> active_connections{0};
    std::atomic<uint64_t> auth_failed{0};
    std::atomic<uint64_t> request_count{0};
};

SdkWsCommandServer::SdkWsCommandServer(const std::string& host,
                                       int port,
                                       const std::string& auth_token)
    : host_(host),
      port_(port),
      auth_token_(auth_token),
      running_(false) {}

SdkWsCommandServer::~SdkWsCommandServer() {
    Stop();
}

void SdkWsCommandServer::SetStatusJsonSupplier(JsonSupplier supplier) {
    status_json_supplier_ = std::move(supplier);
}

void SdkWsCommandServer::SetCapabilitiesJsonSupplier(JsonSupplier supplier) {
    capabilities_json_supplier_ = std::move(supplier);
}

bool SdkWsCommandServer::Start() {
    if (running_.load()) {
        return true;
    }

    impl_ = std::make_unique<Impl>();
    WsServer& server = impl_->server;
    server.clear_access_channels(websocketpp::log::alevel::all);
    server.clear_error_channels(websocketpp::log::elevel::all);
    server.init_asio();
    server.set_reuse_addr(true);

    server.set_validate_handler([this](ConnectionHdl hdl) {
        auto con = impl_->server.get_con_from_hdl(hdl);
        const std::string query = con->get_uri()->get_query();
        const std::string token = QueryValue(query, "token");
        if (auth_token_.empty() || token == auth_token_) {
            return true;
        }
        impl_->auth_failed.fetch_add(1);
        con->set_status(websocketpp::http::status_code::unauthorized);
        return false;
    });

    server.set_open_handler([this](ConnectionHdl hdl) {
        std::lock_guard<std::mutex> lock(impl_->connections_mu);
        impl_->connections.insert(hdl);
        impl_->active_connections.store(static_cast<uint64_t>(impl_->connections.size()));
    });

    server.set_close_handler([this](ConnectionHdl hdl) {
        std::lock_guard<std::mutex> lock(impl_->connections_mu);
        impl_->connections.erase(hdl);
        impl_->active_connections.store(static_cast<uint64_t>(impl_->connections.size()));
    });

    server.set_message_handler([this](ConnectionHdl hdl, MessagePtr msg) {
        impl_->request_count.fetch_add(1);
        if (msg->get_opcode() != websocketpp::frame::opcode::text) {
            const std::string bad_resp = DumpJson(BuildWsResponse("", 400, "text message required"));
            ErrorCode ec;
            impl_->server.send(hdl, bad_resp, websocketpp::frame::opcode::text, ec);
            return;
        }

        const std::string payload = msg->get_payload();
        Json request;
        if (!TryParseJson(payload, &request) || !request.is_object()) {
            const std::string bad_resp = DumpJson(BuildWsResponse("", 400, "invalid json"));
            ErrorCode ec;
            impl_->server.send(hdl, bad_resp, websocketpp::frame::opcode::text, ec);
            return;
        }

        const std::string req_id = GetOptionalStringField(request, "id");
        const std::string method = GetOptionalStringField(request, "method");
        if (method.empty()) {
            const std::string bad_resp = DumpJson(BuildWsResponse(req_id, 400, "missing method"));
            ErrorCode ec;
            impl_->server.send(hdl, bad_resp, websocketpp::frame::opcode::text, ec);
            return;
        }

        Json data_json = Json::object();
        int code = 0;
        std::string message = "ok";
        if (method == "ping") {
            data_json = Json{{"pong", true}};
        } else if (method == "getStatus") {
            data_json = status_json_supplier_ ? status_json_supplier_() : Json::object();
        } else if (method == "listCapabilities") {
            data_json = capabilities_json_supplier_ ? capabilities_json_supplier_() : Json::object();
        } else {
            code = 404;
            message = "unknown method";
        }

        const std::string resp = DumpJson(BuildWsResponse(req_id, code, message, data_json));
        ErrorCode ec;
        impl_->server.send(hdl, resp, websocketpp::frame::opcode::text, ec);
    });

    ErrorCode ec;
    const auto addr = asio::ip::make_address(host_, ec);
    if (ec) {
        std::cerr << "[sdk_ws_command_server] invalid host: " << host_ << ", err=" << ec.message() << std::endl;
        impl_.reset();
        return false;
    }

    asio::ip::tcp::endpoint endpoint(addr, static_cast<uint16_t>(port_));
    server.listen(endpoint, ec);
    if (ec) {
        std::cerr << "[sdk_ws_command_server] listen failed: " << ec.message() << std::endl;
        impl_.reset();
        return false;
    }
    server.start_accept(ec);
    if (ec) {
        std::cerr << "[sdk_ws_command_server] start_accept failed: " << ec.message() << std::endl;
        impl_.reset();
        return false;
    }

    running_.store(true);
    impl_->io_thread = std::thread([this]() {
        impl_->server.run();
        running_.store(false);
    });

    std::cout << "[sdk_ws_command_server] listening on ws://" << host_ << ":" << port_ << std::endl;
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
        for (const ConnectionHdl& hdl : impl_->connections) {
            impl_->server.close(hdl, websocketpp::close::status::going_away, "server stopping", ec);
        }
        impl_->connections.clear();
        impl_->active_connections.store(0);
    }
    impl_->server.stop();

    if (impl_->io_thread.joinable()) {
        impl_->io_thread.join();
    }
    impl_.reset();
    std::cout << "[sdk_ws_command_server] stopped" << std::endl;
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
