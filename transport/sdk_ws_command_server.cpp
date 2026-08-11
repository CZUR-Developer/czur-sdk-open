// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_ws_command_server.h"

#include <websocketpp/config/asio_no_tls.hpp>
#if SDK_OPEN_ENABLE_TLS
#include <websocketpp/config/asio.hpp>
#endif
#include <websocketpp/server.hpp>

#include <asio/ip/address.hpp>

#include <exception>
#include <future>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#include "sdk_logger.h"
#include "sdk_tls_utils.h"

namespace editor {
namespace sdk {

namespace {

using PlainWsServer = websocketpp::server<websocketpp::config::asio>;
#if SDK_OPEN_ENABLE_TLS
using TlsWsServer = websocketpp::server<websocketpp::config::asio_tls>;
using TlsContextPtr = websocketpp::lib::shared_ptr<websocketpp::lib::asio::ssl::context>;
#endif
using ConnectionHdl = websocketpp::connection_hdl;
using ErrorCode = websocketpp::lib::error_code;

enum class ListenerKind {
    Plain,
    Tls,
};

template <typename ServerType>
std::string SafeRemoteEndpoint(ServerType& server, ConnectionHdl hdl) {
    try {
        return server.get_con_from_hdl(hdl)->get_remote_endpoint();
    } catch (...) {
        return "<unknown-remote>";
    }
}

#if SDK_OPEN_ENABLE_TLS
TlsContextPtr CreateTlsContext(const SdkTlsConfig& config, std::string* error_message) {
    TlsContextPtr context = websocketpp::lib::make_shared<websocketpp::lib::asio::ssl::context>(
        websocketpp::lib::asio::ssl::context::tls_server);
    if (!ConfigureSdkTlsServerContext(context->native_handle(), config, error_message)) {
        return TlsContextPtr();
    }
    return context;
}
#endif

} // namespace

class SdkWsCommandServer::Impl {
public:
    struct ConnectionEntry {
        std::string connection_id;
        ListenerKind listener = ListenerKind::Plain;
    };

    websocketpp::lib::asio::io_service io_service;
    PlainWsServer plain_server;
#if SDK_OPEN_ENABLE_TLS
    TlsWsServer tls_server;
    TlsContextPtr tls_context;
#endif
    std::thread io_thread;
    std::set<ConnectionHdl, std::owner_less<ConnectionHdl> > connections;
    std::map<ConnectionHdl, ConnectionEntry, std::owner_less<ConnectionHdl> > connection_entries;
    std::mutex connections_mu;
    std::atomic<uint64_t> active_connections{0};
    std::atomic<uint64_t> auth_failed{0};
    std::atomic<uint64_t> request_count{0};
    std::atomic<uint64_t> next_connection_id{1};

    template <typename ServerType>
    void ConfigureHandlers(ServerType* server, SdkWsCommandServer* owner, ListenerKind listener) {
        server->set_open_handler([this, owner, listener, server](ConnectionHdl hdl) {
            const std::string connection_id = "conn-" + std::to_string(next_connection_id.fetch_add(1));
            {
                std::lock_guard<std::mutex> lock(connections_mu);
                connections.insert(hdl);
                ConnectionEntry entry;
                entry.connection_id = connection_id;
                entry.listener = listener;
                connection_entries[hdl] = entry;
                active_connections.store(static_cast<uint64_t>(connections.size()));
            }
            SDK_OPEN_LOG_INFO("[sdk_ws_command_server] {} connection opened, id={}, remote={}, active_connections={}",
                              listener == ListenerKind::Tls ? "WSS" : "WS",
                              connection_id,
                              SafeRemoteEndpoint(*server, hdl),
                              active_connections.load());
            if (owner->open_handler_) {
                owner->open_handler_(connection_id);
            }
        });

        server->set_close_handler([this, owner, listener, server](ConnectionHdl hdl) {
            std::string connection_id;
            {
                std::lock_guard<std::mutex> lock(connections_mu);
                const std::map<ConnectionHdl, ConnectionEntry, std::owner_less<ConnectionHdl> >::iterator it =
                    connection_entries.find(hdl);
                if (it != connection_entries.end()) {
                    connection_id = it->second.connection_id;
                    connection_entries.erase(it);
                }
                connections.erase(hdl);
                active_connections.store(static_cast<uint64_t>(connections.size()));
            }
            SDK_OPEN_LOG_INFO("[sdk_ws_command_server] {} connection closed, id={}, remote={}, active_connections={}",
                              listener == ListenerKind::Tls ? "WSS" : "WS",
                              connection_id,
                              SafeRemoteEndpoint(*server, hdl),
                              active_connections.load());
            if (!connection_id.empty() && owner->close_handler_) {
                try {
                    owner->close_handler_(connection_id);
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

        server->set_message_handler([this, owner, server](ConnectionHdl hdl, typename ServerType::message_ptr msg) {
            request_count.fetch_add(1);
            if (msg->get_opcode() != websocketpp::frame::opcode::text) {
                ErrorCode ec;
                server->send(hdl,
                             DumpJson(BuildWsResponse("", SdkStatusCode::InvalidRequest, "text message required")),
                             websocketpp::frame::opcode::text,
                             ec);
                return;
            }

            std::string connection_id;
            {
                std::lock_guard<std::mutex> lock(connections_mu);
                const std::map<ConnectionHdl, ConnectionEntry, std::owner_less<ConnectionHdl> >::const_iterator it =
                    connection_entries.find(hdl);
                if (it != connection_entries.end()) {
                    connection_id = it->second.connection_id;
                }
            }

            Json request;
            if (!TryParseJson(msg->get_payload(), &request) || !request.is_object()) {
                ErrorCode ec;
                server->send(hdl,
                             DumpJson(BuildWsResponse("", SdkStatusCode::InvalidRequest, "invalid json")),
                             websocketpp::frame::opcode::text,
                             ec);
                return;
            }

            Json response;
            try {
                response = owner->request_handler_
                               ? owner->request_handler_(connection_id, request)
                               : BuildWsResponse("", SdkStatusCode::UnsupportedMethod, "request handler not ready");
            } catch (const std::exception& e) {
                SDK_OPEN_LOG_ERROR("[sdk_ws_command_server] request handler failed, id={}, err={}", connection_id, e.what());
                response = BuildWsResponse("", SdkStatusCode::InternalError, e.what());
            } catch (...) {
                SDK_OPEN_LOG_ERROR("[sdk_ws_command_server] request handler failed, id={}, err=<unknown>", connection_id);
                response = BuildWsResponse("", SdkStatusCode::InternalError, "unknown native error");
            }
            int response_code = ToCode(SdkStatusCode::InternalError);
            const Json::const_iterator code_it = response.find("code");
            if (code_it != response.end() && code_it->is_number_integer()) {
                response_code = code_it->get<int>();
            }
            if (IsAuthStatusCode(response_code)) {
                auth_failed.fetch_add(1);
            }

            std::string response_payload;
            try {
                response_payload = DumpJson(response);
            } catch (const std::exception& e) {
                SDK_OPEN_LOG_ERROR("[sdk_ws_command_server] response dump failed, id={}, err={}", connection_id, e.what());
                response_payload = DumpJson(BuildWsResponse("", SdkStatusCode::InternalError, "response serialization failed"));
            } catch (...) {
                SDK_OPEN_LOG_ERROR("[sdk_ws_command_server] response dump failed, id={}, err=<unknown>", connection_id);
                response_payload = DumpJson(BuildWsResponse("", SdkStatusCode::InternalError, "response serialization failed"));
            }
            ErrorCode ec;
            server->send(hdl, response_payload, websocketpp::frame::opcode::text, ec);
            if (ec) {
                SDK_OPEN_LOG_ERROR("[sdk_ws_command_server] send failed, id={}, err={}", connection_id, ec.message());
            }
        });
    }

    template <typename ServerType>
    bool StartListener(ServerType* server,
                       SdkWsCommandServer* owner,
                       const std::string& host,
                       int port,
                       ListenerKind listener) {
        server->clear_access_channels(websocketpp::log::alevel::all);
        server->clear_error_channels(websocketpp::log::elevel::all);
        ErrorCode ec;
        server->init_asio(&io_service, ec);
        if (ec) {
            SDK_OPEN_LOG_ERROR("[sdk_ws_command_server] {} init asio failed: {}",
                               listener == ListenerKind::Tls ? "WSS" : "WS", ec.message());
            return false;
        }
        server->set_reuse_addr(true);
        ConfigureHandlers(server, owner, listener);

        const asio::ip::address addr = asio::ip::make_address(host, ec);
        if (ec) {
            SDK_OPEN_LOG_ERROR("[sdk_ws_command_server] invalid {} host: {}, err={}",
                               listener == ListenerKind::Tls ? "WSS" : "WS", host, ec.message());
            return false;
        }
        server->listen(asio::ip::tcp::endpoint(addr, static_cast<uint16_t>(port)), ec);
        if (ec) {
            SDK_OPEN_LOG_ERROR("[sdk_ws_command_server] {} listen failed: {}", listener == ListenerKind::Tls ? "WSS" : "WS", ec.message());
            return false;
        }
        server->start_accept(ec);
        if (ec) {
            SDK_OPEN_LOG_ERROR("[sdk_ws_command_server] {} start_accept failed: {}", listener == ListenerKind::Tls ? "WSS" : "WS", ec.message());
            return false;
        }
        return true;
    }

    void Send(ListenerKind listener, ConnectionHdl hdl, const std::string& payload, websocketpp::frame::opcode::value opcode, ErrorCode* ec) {
        if (listener == ListenerKind::Plain) {
            plain_server.send(hdl, payload, opcode, *ec);
            return;
        }
#if SDK_OPEN_ENABLE_TLS
        tls_server.send(hdl, payload, opcode, *ec);
#else
        *ec = ErrorCode();
#endif
    }

    void Close(ListenerKind listener, ConnectionHdl hdl, websocketpp::close::status::value status, const std::string& reason, ErrorCode* ec) {
        if (listener == ListenerKind::Plain) {
            plain_server.close(hdl, status, reason, *ec);
            return;
        }
#if SDK_OPEN_ENABLE_TLS
        tls_server.close(hdl, status, reason, *ec);
#else
        *ec = ErrorCode();
#endif
    }
};

SdkWsCommandServer::SdkWsCommandServer(const std::string& host, int port, const SdkTlsConfig& tls_config)
    : host_(host),
      port_(port),
      tls_config_(tls_config),
      running_(false) {}

SdkWsCommandServer::~SdkWsCommandServer() {
    Stop();
}

void SdkWsCommandServer::SetRequestHandler(RequestHandler handler) { request_handler_ = handler; }
void SdkWsCommandServer::SetOpenHandler(ConnectionHandler handler) { open_handler_ = handler; }
void SdkWsCommandServer::SetCloseHandler(ConnectionHandler handler) { close_handler_ = handler; }

bool SdkWsCommandServer::SendEvent(const std::string& connection_id, const Json& event) {
    if (!impl_ || connection_id.empty()) {
        SDK_OPEN_LOG_WARN("[sdk_ws_command_server] send event dropped, reason={} event={} connection={}",
                          !impl_ ? "server_not_started" : "empty_connection_id",
                          event.value("event", "event"), connection_id);
        return false;
    }
    ConnectionHdl target;
    ListenerKind listener = ListenerKind::Plain;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(impl_->connections_mu);
        for (std::map<ConnectionHdl, Impl::ConnectionEntry, std::owner_less<ConnectionHdl> >::const_iterator it =
                 impl_->connection_entries.begin(); it != impl_->connection_entries.end(); ++it) {
            if (it->second.connection_id == connection_id) {
                target = it->first;
                listener = it->second.listener;
                found = true;
                break;
            }
        }
    }
    if (!found) {
        SDK_OPEN_LOG_WARN("[sdk_ws_command_server] send event dropped, reason=connection_not_found event={} connection={}",
                          event.value("event", "event"), connection_id);
        return false;
    }
    impl_->io_service.post([this, target, listener, event]() {
        ErrorCode ec;
        impl_->Send(listener, target, DumpJson(event), websocketpp::frame::opcode::text, &ec);
        if (ec) {
            SDK_OPEN_LOG_WARN("[sdk_ws_command_server] send event failed, event={} error={}",
                              event.value("event", "event"), ec.message());
        }
    });
    return true;
}

void SdkWsCommandServer::RunOnIoThreadSync(const std::function<void()>& task) {
    if (!task) {
        return;
    }
    if (!impl_ || !running_.load() || !impl_->io_thread.joinable()) {
        task();
        return;
    }
    if (impl_->io_thread.get_id() == std::this_thread::get_id()) {
        task();
        return;
    }
    std::shared_ptr<std::promise<void> > done(new std::promise<void>());
    std::future<void> future = done->get_future();
    impl_->io_service.post([task, done]() {
        try {
            task();
            done->set_value();
        } catch (...) {
            done->set_exception(std::current_exception());
        }
    });
    future.get();
}

bool SdkWsCommandServer::Start() {
    if (running_.load()) {
        return true;
    }
    impl_.reset(new Impl());
    if (!impl_->StartListener(&impl_->plain_server, this, host_, port_, ListenerKind::Plain)) {
        impl_.reset();
        return false;
    }

    if (tls_config_.enabled) {
#if SDK_OPEN_ENABLE_TLS
        std::string tls_error;
        impl_->tls_context = CreateTlsContext(tls_config_, &tls_error);
        if (!impl_->tls_context) {
            SDK_OPEN_LOG_ERROR("[sdk_ws_command_server] TLS context initialization failed: {}", tls_error);
            impl_.reset();
            return false;
        }
        impl_->tls_server.set_tls_init_handler([this](ConnectionHdl) { return impl_->tls_context; });
        const std::string tls_host = tls_config_.bind_host.empty() ? host_ : tls_config_.bind_host;
        if (!impl_->StartListener(&impl_->tls_server,
                                  this,
                                  tls_host,
                                  tls_config_.command_wss_port,
                                  ListenerKind::Tls)) {
            impl_.reset();
            return false;
        }
#else
        SDK_OPEN_LOG_ERROR("[sdk_ws_command_server] TLS requested but this build has TLS disabled");
        impl_.reset();
        return false;
#endif
    }

    running_.store(true);
    impl_->io_thread = std::thread([this]() {
        impl_->io_service.run();
        running_.store(false);
    });
    SDK_OPEN_LOG_INFO("[sdk_ws_command_server] listening on ws://{}:{}", host_, port_);
    if (tls_config_.enabled) {
        SDK_OPEN_LOG_INFO("[sdk_ws_command_server] listening on wss://{}:{}",
                          tls_config_.bind_host.empty() ? host_ : tls_config_.bind_host,
                          tls_config_.command_wss_port);
    }
    return true;
}

void SdkWsCommandServer::Stop() {
    if (!impl_) {
        return;
    }
    running_.store(false);
    ErrorCode ec;
    impl_->plain_server.stop_listening(ec);
#if SDK_OPEN_ENABLE_TLS
    if (tls_config_.enabled) {
        impl_->tls_server.stop_listening(ec);
    }
#endif
    {
        std::lock_guard<std::mutex> lock(impl_->connections_mu);
        for (std::map<ConnectionHdl, Impl::ConnectionEntry, std::owner_less<ConnectionHdl> >::const_iterator it =
                 impl_->connection_entries.begin(); it != impl_->connection_entries.end(); ++it) {
            impl_->Close(it->second.listener, it->first, websocketpp::close::status::going_away, "server stopping", &ec);
        }
        impl_->connections.clear();
        impl_->connection_entries.clear();
        impl_->active_connections.store(0);
    }
    impl_->io_service.stop();
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
