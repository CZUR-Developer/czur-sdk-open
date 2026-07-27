// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_ws_video_server.h"

#include <websocketpp/config/asio_no_tls.hpp>
#if SDK_OPEN_ENABLE_TLS
#include <websocketpp/config/asio.hpp>
#endif
#include <websocketpp/server.hpp>

#include <asio/ip/address.hpp>

#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#include "sdk_json_utils.h"
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

Json BuildDetectedRectJson(const SdkRect4P& rect) {
    return Json{{"left_top", Json::array({rect.left_top.x, rect.left_top.y})},
                {"right_top", Json::array({rect.right_top.x, rect.right_top.y})},
                {"right_down", Json::array({rect.right_down.x, rect.right_down.y})},
                {"left_down", Json::array({rect.left_down.x, rect.left_down.y})}};
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

class SdkWsVideoServer::Impl {
public:
    struct PendingConnection {
        std::string stream_id;
        ListenerKind listener = ListenerKind::Plain;
    };
    struct ActiveConnection {
        std::string stream_id;
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
    std::map<ConnectionHdl, PendingConnection, std::owner_less<ConnectionHdl> > pending;
    std::map<ConnectionHdl, ActiveConnection, std::owner_less<ConnectionHdl> > active;
    std::map<std::string, std::set<ConnectionHdl, std::owner_less<ConnectionHdl> > > stream_connections;
    std::mutex connections_mu;
    std::atomic<uint64_t> active_connections{0};
    std::atomic<uint64_t> auth_failed{0};
    std::atomic<uint64_t> binary_frames{0};
    std::atomic<uint64_t> binary_bytes{0};

    template <typename ServerType>
    void ConfigureHandlers(ServerType* server, SdkWsVideoServer* owner, ListenerKind listener) {
        server->set_validate_handler([this, owner, listener, server](ConnectionHdl hdl) {
            if (!owner->connection_auth_handler_) {
                return false;
            }
            const typename ServerType::connection_ptr con = server->get_con_from_hdl(hdl);
            const std::string query = con->get_uri()->get_query();
            const std::string session_token = QueryValue(query, "session_token");
            const std::string stream_id = QueryValue(query, "stream_id");
            const ConnectionAuthResult result = owner->connection_auth_handler_(session_token, stream_id);
            if (!result.authorized) {
                auth_failed.fetch_add(1);
                con->set_status(websocketpp::http::status_code::unauthorized);
                con->set_body(DumpJson(BuildErrorBody(result.code, result.message)));
                return false;
            }
            std::lock_guard<std::mutex> lock(connections_mu);
            PendingConnection pending_connection;
            pending_connection.stream_id = result.stream_id;
            pending_connection.listener = listener;
            pending[hdl] = pending_connection;
            return true;
        });

        server->set_open_handler([this, server](ConnectionHdl hdl) {
            std::string stream_id;
            {
                std::lock_guard<std::mutex> lock(connections_mu);
                connections.insert(hdl);
                const std::map<ConnectionHdl, PendingConnection, std::owner_less<ConnectionHdl> >::iterator pending_it =
                    pending.find(hdl);
                if (pending_it != pending.end()) {
                    stream_id = pending_it->second.stream_id;
                    ActiveConnection active_connection;
                    active_connection.stream_id = stream_id;
                    active_connection.listener = pending_it->second.listener;
                    active[hdl] = active_connection;
                    stream_connections[stream_id].insert(hdl);
                    pending.erase(pending_it);
                }
                active_connections.store(static_cast<uint64_t>(connections.size()));
            }
            ErrorCode ec;
            server->send(hdl,
                         DumpJson(BuildWsEvent("video.ready", Json{{"stream_id", stream_id}})),
                         websocketpp::frame::opcode::text,
                         ec);
        });

        server->set_close_handler([this](ConnectionHdl hdl) {
            std::lock_guard<std::mutex> lock(connections_mu);
            const std::map<ConnectionHdl, ActiveConnection, std::owner_less<ConnectionHdl> >::iterator active_it =
                active.find(hdl);
            if (active_it != active.end()) {
                const std::map<std::string, std::set<ConnectionHdl, std::owner_less<ConnectionHdl> > >::iterator stream_it =
                    stream_connections.find(active_it->second.stream_id);
                if (stream_it != stream_connections.end()) {
                    stream_it->second.erase(hdl);
                    if (stream_it->second.empty()) {
                        stream_connections.erase(stream_it);
                    }
                }
                active.erase(active_it);
            }
            connections.erase(hdl);
            pending.erase(hdl);
            active_connections.store(static_cast<uint64_t>(connections.size()));
        });

        server->set_message_handler([this, server](ConnectionHdl hdl, typename ServerType::message_ptr msg) {
            if (msg->get_opcode() == websocketpp::frame::opcode::binary) {
                binary_frames.fetch_add(1);
                binary_bytes.fetch_add(static_cast<uint64_t>(msg->get_payload().size()));
                return;
            }
            ErrorCode ec;
            server->send(hdl,
                         DumpJson(BuildWsEvent("error",
                                               Json{{"message", "video channel is output only"}},
                                               SdkStatusCode::InvalidRequest,
                                               "invalid message")),
                         websocketpp::frame::opcode::text,
                         ec);
        });
    }

    template <typename ServerType>
    bool StartListener(ServerType* server,
                       SdkWsVideoServer* owner,
                       const std::string& host,
                       int port,
                       ListenerKind listener) {
        server->clear_access_channels(websocketpp::log::alevel::all);
        server->clear_error_channels(websocketpp::log::elevel::all);
        ErrorCode ec;
        server->init_asio(&io_service, ec);
        if (ec) {
            SDK_OPEN_LOG_ERROR("[sdk_ws_video_server] {} init asio failed: {}",
                               listener == ListenerKind::Tls ? "WSS" : "WS", ec.message());
            return false;
        }
        server->set_reuse_addr(true);
        ConfigureHandlers(server, owner, listener);
        const asio::ip::address addr = asio::ip::make_address(host, ec);
        if (ec) {
            SDK_OPEN_LOG_ERROR("[sdk_ws_video_server] invalid {} host: {}, err={}",
                               listener == ListenerKind::Tls ? "WSS" : "WS", host, ec.message());
            return false;
        }
        server->listen(asio::ip::tcp::endpoint(addr, static_cast<uint16_t>(port)), ec);
        if (ec) {
            SDK_OPEN_LOG_ERROR("[sdk_ws_video_server] {} listen failed: {}", listener == ListenerKind::Tls ? "WSS" : "WS", ec.message());
            return false;
        }
        server->start_accept(ec);
        if (ec) {
            SDK_OPEN_LOG_ERROR("[sdk_ws_video_server] {} start_accept failed: {}", listener == ListenerKind::Tls ? "WSS" : "WS", ec.message());
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

    ListenerKind ListenerFor(ConnectionHdl hdl) const {
        const std::map<ConnectionHdl, ActiveConnection, std::owner_less<ConnectionHdl> >::const_iterator active_it = active.find(hdl);
        return active_it == active.end() ? ListenerKind::Plain : active_it->second.listener;
    }
};

SdkWsVideoServer::SdkWsVideoServer(const std::string& host, int port, const SdkTlsConfig& tls_config)
    : host_(host),
      port_(port),
      tls_config_(tls_config),
      running_(false) {}

SdkWsVideoServer::~SdkWsVideoServer() { Stop(); }

void SdkWsVideoServer::SetConnectionAuthHandler(ConnectionAuthHandler handler) { connection_auth_handler_ = handler; }

bool SdkWsVideoServer::Start() {
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
            SDK_OPEN_LOG_ERROR("[sdk_ws_video_server] TLS context initialization failed: {}", tls_error);
            impl_.reset();
            return false;
        }
        impl_->tls_server.set_tls_init_handler([this](ConnectionHdl) { return impl_->tls_context; });
        const std::string tls_host = tls_config_.bind_host.empty() ? host_ : tls_config_.bind_host;
        if (!impl_->StartListener(&impl_->tls_server,
                                  this,
                                  tls_host,
                                  tls_config_.video_wss_port,
                                  ListenerKind::Tls)) {
            impl_.reset();
            return false;
        }
#else
        SDK_OPEN_LOG_ERROR("[sdk_ws_video_server] TLS requested but this build has TLS disabled");
        impl_.reset();
        return false;
#endif
    }
    running_.store(true);
    impl_->io_thread = std::thread([this]() {
        impl_->io_service.run();
        running_.store(false);
    });
    SDK_OPEN_LOG_INFO("[sdk_ws_video_server] listening on ws://{}:{}", host_, port_);
    if (tls_config_.enabled) {
        SDK_OPEN_LOG_INFO("[sdk_ws_video_server] listening on wss://{}:{}",
                          tls_config_.bind_host.empty() ? host_ : tls_config_.bind_host,
                          tls_config_.video_wss_port);
    }
    return true;
}

void SdkWsVideoServer::Stop() {
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
        for (std::set<ConnectionHdl, std::owner_less<ConnectionHdl> >::const_iterator it = impl_->connections.begin();
             it != impl_->connections.end(); ++it) {
            impl_->Close(impl_->ListenerFor(*it), *it, websocketpp::close::status::going_away, "server stopping", &ec);
        }
        impl_->connections.clear();
        impl_->pending.clear();
        impl_->active.clear();
        impl_->stream_connections.clear();
        impl_->active_connections.store(0);
    }
    impl_->io_service.stop();
    if (impl_->io_thread.joinable()) {
        impl_->io_thread.join();
    }
    impl_.reset();
    SDK_OPEN_LOG_INFO("[sdk_ws_video_server] stopped");
}

void SdkWsVideoServer::PublishFrame(const SdkVideoFrame& frame) {
    if (!impl_ || frame.stream_id.empty() || frame.payload.empty()) {
        return;
    }
    std::vector<std::pair<ConnectionHdl, ListenerKind> > targets;
    {
        std::lock_guard<std::mutex> lock(impl_->connections_mu);
        const std::map<std::string, std::set<ConnectionHdl, std::owner_less<ConnectionHdl> > >::const_iterator stream_it =
            impl_->stream_connections.find(frame.stream_id);
        if (stream_it == impl_->stream_connections.end()) {
            return;
        }
        for (std::set<ConnectionHdl, std::owner_less<ConnectionHdl> >::const_iterator it = stream_it->second.begin();
             it != stream_it->second.end(); ++it) {
            targets.push_back(std::make_pair(*it, impl_->ListenerFor(*it)));
        }
    }
    Json meta = Json{{"device_id", frame.device_id},
                     {"stream_id", frame.stream_id},
                     {"frame_seq", frame.frame_seq},
                     {"timestamp_ms", frame.timestamp_ms},
                     {"width", frame.width},
                     {"height", frame.height},
                     {"pixel_format", frame.pixel_format}};
    if (!frame.detected_rects.empty()) {
        Json rects = Json::array();
        for (std::vector<SdkRect4P>::const_iterator it = frame.detected_rects.begin(); it != frame.detected_rects.end(); ++it) {
            rects.push_back(BuildDetectedRectJson(*it));
        }
        meta["detected_rects"] = rects;
        meta["detected_rects_source"] = Json{{"width", frame.detected_rects_source_width},
                                             {"height", frame.detected_rects_source_height}};
    }
    const std::string meta_payload = DumpJson(BuildWsEvent("stream.frame_meta", meta));
    const std::string binary_payload(reinterpret_cast<const char*>(frame.payload.data()), frame.payload.size());
    impl_->io_service.post([this, targets, meta_payload, binary_payload]() {
        for (std::vector<std::pair<ConnectionHdl, ListenerKind> >::const_iterator it = targets.begin(); it != targets.end(); ++it) {
            ErrorCode ec;
            impl_->Send(it->second, it->first, meta_payload, websocketpp::frame::opcode::text, &ec);
            if (ec) {
                continue;
            }
            impl_->Send(it->second, it->first, binary_payload, websocketpp::frame::opcode::binary, &ec);
            if (!ec) {
                impl_->binary_frames.fetch_add(1);
                impl_->binary_bytes.fetch_add(static_cast<uint64_t>(binary_payload.size()));
            }
        }
    });
}

void SdkWsVideoServer::CloseStream(const std::string& stream_id) {
    if (!impl_ || stream_id.empty()) {
        return;
    }
    std::vector<std::pair<ConnectionHdl, ListenerKind> > targets;
    {
        std::lock_guard<std::mutex> lock(impl_->connections_mu);
        const std::map<std::string, std::set<ConnectionHdl, std::owner_less<ConnectionHdl> > >::const_iterator stream_it =
            impl_->stream_connections.find(stream_id);
        if (stream_it == impl_->stream_connections.end()) {
            return;
        }
        for (std::set<ConnectionHdl, std::owner_less<ConnectionHdl> >::const_iterator it = stream_it->second.begin();
             it != stream_it->second.end(); ++it) {
            targets.push_back(std::make_pair(*it, impl_->ListenerFor(*it)));
        }
    }
    impl_->io_service.post([this, targets]() {
        for (std::vector<std::pair<ConnectionHdl, ListenerKind> >::const_iterator it = targets.begin(); it != targets.end(); ++it) {
            ErrorCode ec;
            impl_->Close(it->second, it->first, websocketpp::close::status::normal, "stream stopped", &ec);
        }
    });
}

SdkWsVideoServer::Stats SdkWsVideoServer::GetStats() const {
    Stats stats;
    if (!impl_) {
        return stats;
    }
    stats.active_connections = impl_->active_connections.load();
    stats.auth_failed = impl_->auth_failed.load();
    stats.binary_frames = impl_->binary_frames.load();
    stats.binary_bytes = impl_->binary_bytes.load();
    return stats;
}

} // namespace sdk
} // namespace editor
