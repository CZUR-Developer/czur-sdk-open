// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "sdk_ws_video_server.h"

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <asio/ip/address.hpp>

#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "sdk_json_utils.h"
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

Json BuildDetectedRectJson(const SdkRect4P& rect) {
    return Json{{"left_top", Json::array({rect.left_top.x, rect.left_top.y})},
                {"right_top", Json::array({rect.right_top.x, rect.right_top.y})},
                {"right_down", Json::array({rect.right_down.x, rect.right_down.y})},
                {"left_down", Json::array({rect.left_down.x, rect.left_down.y})}};
}

} // namespace

class SdkWsVideoServer::Impl {
public:
    struct PendingConnection {
        std::string stream_id;
    };

    struct ActiveConnection {
        std::string stream_id;
    };

    WsServer server;
    std::thread io_thread;
    std::set<ConnectionHdl, std::owner_less<ConnectionHdl>> connections;
    std::map<ConnectionHdl, PendingConnection, std::owner_less<ConnectionHdl>> pending;
    std::map<ConnectionHdl, ActiveConnection, std::owner_less<ConnectionHdl>> active;
    std::map<std::string, std::set<ConnectionHdl, std::owner_less<ConnectionHdl>>> stream_connections;
    std::mutex connections_mu;
    std::atomic<uint64_t> active_connections{0};
    std::atomic<uint64_t> auth_failed{0};
    std::atomic<uint64_t> binary_frames{0};
    std::atomic<uint64_t> binary_bytes{0};
};

SdkWsVideoServer::SdkWsVideoServer(const std::string& host, int port)
    : host_(host),
      port_(port),
      running_(false) {}

SdkWsVideoServer::~SdkWsVideoServer() {
    Stop();
}

void SdkWsVideoServer::SetConnectionAuthHandler(ConnectionAuthHandler handler) {
    connection_auth_handler_ = handler;
}

bool SdkWsVideoServer::Start() {
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
        if (!connection_auth_handler_) {
            return false;
        }
        auto con = impl_->server.get_con_from_hdl(hdl);
        const std::string query = con->get_uri()->get_query();
        const std::string session_token = QueryValue(query, "session_token");
        const std::string stream_id = QueryValue(query, "stream_id");
        const ConnectionAuthResult result = connection_auth_handler_(session_token, stream_id);
        if (!result.authorized) {
            impl_->auth_failed.fetch_add(1);
            con->set_status(websocketpp::http::status_code::unauthorized);
            con->set_body(DumpJson(BuildErrorBody(result.code, result.message)));
            return false;
        }
        std::lock_guard<std::mutex> lock(impl_->connections_mu);
        Impl::PendingConnection pending;
        pending.stream_id = result.stream_id;
        impl_->pending[hdl] = pending;
        return true;
    });

    server.set_open_handler([this](ConnectionHdl hdl) {
        std::string stream_id;
        {
            std::lock_guard<std::mutex> lock(impl_->connections_mu);
            impl_->connections.insert(hdl);
            const std::map<ConnectionHdl, Impl::PendingConnection, std::owner_less<ConnectionHdl>>::iterator it =
                impl_->pending.find(hdl);
            if (it != impl_->pending.end()) {
                stream_id = it->second.stream_id;
                Impl::ActiveConnection active;
                active.stream_id = stream_id;
                impl_->active[hdl] = active;
                impl_->stream_connections[stream_id].insert(hdl);
                impl_->pending.erase(it);
            }
            impl_->active_connections.store(static_cast<uint64_t>(impl_->connections.size()));
        }
        ErrorCode ec;
        impl_->server.send(hdl,
                           DumpJson(BuildWsEvent("video.ready", Json{{"stream_id", stream_id}})),
                           websocketpp::frame::opcode::text,
                           ec);
    });

    server.set_close_handler([this](ConnectionHdl hdl) {
        std::lock_guard<std::mutex> lock(impl_->connections_mu);
        const std::map<ConnectionHdl, Impl::ActiveConnection, std::owner_less<ConnectionHdl>>::iterator active_it =
            impl_->active.find(hdl);
        if (active_it != impl_->active.end()) {
            const std::map<std::string, std::set<ConnectionHdl, std::owner_less<ConnectionHdl>>>::iterator stream_it =
                impl_->stream_connections.find(active_it->second.stream_id);
            if (stream_it != impl_->stream_connections.end()) {
                stream_it->second.erase(hdl);
                if (stream_it->second.empty()) {
                    impl_->stream_connections.erase(stream_it);
                }
            }
            impl_->active.erase(active_it);
        }
        impl_->connections.erase(hdl);
        impl_->pending.erase(hdl);
        impl_->active_connections.store(static_cast<uint64_t>(impl_->connections.size()));
    });

    server.set_message_handler([this](ConnectionHdl hdl, MessagePtr msg) {
        if (msg->get_opcode() == websocketpp::frame::opcode::binary) {
            impl_->binary_frames.fetch_add(1);
            impl_->binary_bytes.fetch_add(static_cast<uint64_t>(msg->get_payload().size()));
            return;
        }
        ErrorCode ec;
        impl_->server.send(hdl,
                           DumpJson(BuildWsEvent("error", Json{{"message", "video channel is output only"}}, SdkStatusCode::InvalidRequest, "invalid message")),
                           websocketpp::frame::opcode::text,
                           ec);
    });

    ErrorCode ec;
    const auto addr = asio::ip::make_address(host_, ec);
    if (ec) {
        SDK_OPEN_LOG_ERROR("[sdk_ws_video_server] invalid host: {}, err={}", host_, ec.message());
        impl_.reset();
        return false;
    }
    asio::ip::tcp::endpoint endpoint(addr, static_cast<uint16_t>(port_));
    server.listen(endpoint, ec);
    if (ec) {
        SDK_OPEN_LOG_ERROR("[sdk_ws_video_server] listen failed: {}", ec.message());
        impl_.reset();
        return false;
    }
    server.start_accept(ec);
    if (ec) {
        SDK_OPEN_LOG_ERROR("[sdk_ws_video_server] start_accept failed: {}", ec.message());
        impl_.reset();
        return false;
    }
    running_.store(true);
    impl_->io_thread = std::thread([this]() {
        impl_->server.run();
        running_.store(false);
    });
    SDK_OPEN_LOG_INFO("[sdk_ws_video_server] listening on ws://{}:{}", host_, port_);
    return true;
}

void SdkWsVideoServer::Stop() {
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
        impl_->pending.clear();
        impl_->active.clear();
        impl_->stream_connections.clear();
        impl_->active_connections.store(0);
    }
    impl_->server.stop();
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

    std::vector<ConnectionHdl> targets;
    {
        std::lock_guard<std::mutex> lock(impl_->connections_mu);
        const std::map<std::string, std::set<ConnectionHdl, std::owner_less<ConnectionHdl>>>::const_iterator it =
            impl_->stream_connections.find(frame.stream_id);
        if (it == impl_->stream_connections.end()) {
            return;
        }
        targets.assign(it->second.begin(), it->second.end());
    }
    if (targets.empty()) {
        return;
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

    impl_->server.get_io_service().post([this, targets, meta_payload, binary_payload]() {
        for (std::vector<ConnectionHdl>::const_iterator it = targets.begin(); it != targets.end(); ++it) {
            ErrorCode ec;
            impl_->server.send(*it, meta_payload, websocketpp::frame::opcode::text, ec);
            if (ec) {
                continue;
            }
            impl_->server.send(*it, binary_payload, websocketpp::frame::opcode::binary, ec);
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

    std::vector<ConnectionHdl> targets;
    {
        std::lock_guard<std::mutex> lock(impl_->connections_mu);
        const std::map<std::string, std::set<ConnectionHdl, std::owner_less<ConnectionHdl>>>::const_iterator it =
            impl_->stream_connections.find(stream_id);
        if (it == impl_->stream_connections.end()) {
            return;
        }
        targets.assign(it->second.begin(), it->second.end());
    }

    impl_->server.get_io_service().post([this, targets]() {
        for (std::vector<ConnectionHdl>::const_iterator it = targets.begin(); it != targets.end(); ++it) {
            ErrorCode ec;
            impl_->server.close(*it, websocketpp::close::status::normal, "stream stopped", ec);
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
