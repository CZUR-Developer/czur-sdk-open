// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_ws_command_server.h"

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <asio/ip/address.hpp>

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>

#include "sdk_logger.h"

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
    struct PendingSessionIssue {
        std::string session_key;
        int expires_in = 0;
        Json auth_context = Json::object();
    };

    WsServer server;
    std::thread io_thread;
    std::set<ConnectionHdl, std::owner_less<ConnectionHdl>> connections;
    std::map<ConnectionHdl, PendingSessionIssue, std::owner_less<ConnectionHdl>> pending_session_issues;
    std::mutex connections_mu;
    std::atomic<uint64_t> active_connections{0};
    std::atomic<uint64_t> auth_failed{0};
    std::atomic<uint64_t> request_count{0};
};

SdkWsCommandServer::SdkWsCommandServer(const std::string& host,
                                       int port)
    : host_(host),
      port_(port),
      running_(false) {}

SdkWsCommandServer::~SdkWsCommandServer() {
    Stop();
}

void SdkWsCommandServer::SetConnectionAuthHandler(ConnectionAuthHandler handler) {
    connection_auth_handler_ = std::move(handler);
}

void SdkWsCommandServer::SetRequestHandler(RequestHandler handler) {
    request_handler_ = std::move(handler);
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

    impl_.reset(new Impl());
    WsServer& server = impl_->server;
    server.clear_access_channels(websocketpp::log::alevel::all);
    server.clear_error_channels(websocketpp::log::elevel::all);
    server.init_asio();
    server.set_reuse_addr(true);

    server.set_validate_handler([this](ConnectionHdl hdl) {
        auto con = impl_->server.get_con_from_hdl(hdl);
        const std::string remote = SafeRemoteEndpoint(impl_->server, hdl);
        const std::string resource = con->get_uri()->get_resource();
        const std::string query = con->get_uri()->get_query();
        const std::string api_key = QueryValue(query, "api_key");
        SDK_OPEN_LOG_INFO("[sdk_ws_command_server] handshake validate start, remote={}, resource={}, has_api_key={}, api_key={}",
                          remote,
                          resource,
                          !api_key.empty() ? "true" : "false",
                          MaskApiKey(api_key));
        if (!connection_auth_handler_) {
            impl_->auth_failed.fetch_add(1);
            SDK_OPEN_LOG_WARN("[sdk_ws_command_server] handshake validate failed, remote={}, reason=provider_not_ready",
                              remote);
            con->set_status(websocketpp::http::status_code::service_unavailable);
            con->set_body(DumpJson(BuildErrorBody(SdkStatusCode::ProviderNotReady, "provider not ready")));
            return false;
        }

        const ConnectionAuthResult auth_result = connection_auth_handler_(api_key);
        SDK_OPEN_LOG_INFO("[sdk_ws_command_server] handshake validate result, remote={}, authorized={}, code={}, message={}",
                          remote,
                          auth_result.authorized ? "true" : "false",
                          auth_result.code,
                          auth_result.message);
        if (auth_result.authorized) {
            std::lock_guard<std::mutex> lock(impl_->connections_mu);
            Impl::PendingSessionIssue pending;
            pending.session_key = auth_result.session_key;
            pending.expires_in = auth_result.expires_in;
            pending.auth_context = auth_result.auth_context;
            impl_->pending_session_issues[hdl] = pending;
            SDK_OPEN_LOG_INFO("[sdk_ws_command_server] handshake accepted, remote={}", remote);
            return true;
        }
        impl_->auth_failed.fetch_add(1);
        {
            std::lock_guard<std::mutex> lock(impl_->connections_mu);
            impl_->pending_session_issues.erase(hdl);
        }
        SDK_OPEN_LOG_WARN("[sdk_ws_command_server] handshake rejected, remote={}, http_status=401, code={}, message={}",
                          remote,
                          auth_result.code,
                          auth_result.message);
        con->set_status(websocketpp::http::status_code::unauthorized);
        con->set_body(DumpJson(BuildErrorBody(auth_result.code, auth_result.message)));
        return false;
    });

    server.set_open_handler([this](ConnectionHdl hdl) {
        Impl::PendingSessionIssue pending;
        bool has_pending = false;
        {
            std::lock_guard<std::mutex> lock(impl_->connections_mu);
            impl_->connections.insert(hdl);
            const std::map<ConnectionHdl, Impl::PendingSessionIssue, std::owner_less<ConnectionHdl>>::iterator pending_it =
                impl_->pending_session_issues.find(hdl);
            if (pending_it != impl_->pending_session_issues.end()) {
                pending = pending_it->second;
                impl_->pending_session_issues.erase(pending_it);
                has_pending = true;
            }
            impl_->active_connections.store(static_cast<uint64_t>(impl_->connections.size()));
        }
        SDK_OPEN_LOG_INFO("[sdk_ws_command_server] connection opened, remote={}, active_connections={}",
                          SafeRemoteEndpoint(impl_->server, hdl),
                          impl_->active_connections.load());

        if (has_pending && !pending.session_key.empty()) {
            const Json payload{
                {"session_key", pending.session_key},
                {"session_token", pending.session_key},
                {"expires_in", pending.expires_in},
                {"auth_context", pending.auth_context},
            };
            const std::string event_payload = DumpJson(BuildWsEvent("auth.session_issued", payload));
            ErrorCode ec;
            impl_->server.send(hdl, event_payload, websocketpp::frame::opcode::text, ec);
            if (ec) {
                SDK_OPEN_LOG_WARN("[sdk_ws_command_server] auth.session_issued send failed, remote={}, err={}",
                                  SafeRemoteEndpoint(impl_->server, hdl),
                                  ec.message());
            } else {
                SDK_OPEN_LOG_INFO("[sdk_ws_command_server] auth.session_issued sent, remote={}, expires_in={}",
                                  SafeRemoteEndpoint(impl_->server, hdl),
                                  pending.expires_in);
            }
        }
    });

    server.set_close_handler([this](ConnectionHdl hdl) {
        std::lock_guard<std::mutex> lock(impl_->connections_mu);
        impl_->connections.erase(hdl);
        impl_->pending_session_issues.erase(hdl);
        impl_->active_connections.store(static_cast<uint64_t>(impl_->connections.size()));
        SDK_OPEN_LOG_INFO("[sdk_ws_command_server] connection closed, remote={}, active_connections={}",
                          SafeRemoteEndpoint(impl_->server, hdl),
                          impl_->active_connections.load());
    });

    server.set_message_handler([this](ConnectionHdl hdl, MessagePtr msg) {
        impl_->request_count.fetch_add(1);
        if (msg->get_opcode() != websocketpp::frame::opcode::text) {
            const std::string bad_resp =
                DumpJson(BuildWsResponse("", SdkStatusCode::InvalidRequest, "text message required"));
            ErrorCode ec;
            impl_->server.send(hdl, bad_resp, websocketpp::frame::opcode::text, ec);
            return;
        }

        const std::string payload = msg->get_payload();
        Json request;
        if (!TryParseJson(payload, &request) || !request.is_object()) {
            const std::string bad_resp = DumpJson(BuildWsResponse("", SdkStatusCode::InvalidRequest, "invalid json"));
            ErrorCode ec;
            impl_->server.send(hdl, bad_resp, websocketpp::frame::opcode::text, ec);
            return;
        }

        Json response;
        if (request_handler_) {
            response = request_handler_(request);
        } else {
            const std::string req_id = GetOptionalStringField(request, "request_id").empty()
                                           ? GetOptionalStringField(request, "id")
                                           : GetOptionalStringField(request, "request_id");
            const std::string method = GetOptionalStringField(request, "method");
            if (method.empty()) {
                response = BuildWsResponse(req_id, SdkStatusCode::InvalidMethod, "missing method");
            } else {
                Json data_json = Json::object();
                SdkStatusCode code = SdkStatusCode::Ok;
                std::string message = "ok";
                if (method == "ping") {
                    data_json = Json{{"pong", true}};
                } else if (method == "getStatus") {
                    data_json = status_json_supplier_ ? status_json_supplier_() : Json::object();
                } else if (method == "listCapabilities") {
                    data_json = capabilities_json_supplier_ ? capabilities_json_supplier_() : Json::object();
                } else {
                    code = SdkStatusCode::UnsupportedMethod;
                    message = "unsupported method";
                }
                response = BuildWsResponse(req_id, code, message, data_json);
            }
        }

        int response_code = ToCode(SdkStatusCode::InternalError);
        const auto code_it = response.find("code");
        if (code_it != response.end() && code_it->is_number_integer()) {
            response_code = code_it->get<int>();
        }
        if (IsAuthStatusCode(response_code)) {
            impl_->auth_failed.fetch_add(1);
        }

        const std::string resp = DumpJson(response);
        ErrorCode ec;
        impl_->server.send(hdl, resp, websocketpp::frame::opcode::text, ec);
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
