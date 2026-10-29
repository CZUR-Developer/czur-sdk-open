// Copyright (c) 2026 CZUR Tech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "sdk_json_utils.h"
#include "sdk_ws_video_server.h"

namespace editor {
namespace sdk {
namespace {

typedef websocketpp::client<websocketpp::config::asio_client> TestWsClientEndpoint;
typedef websocketpp::connection_hdl TestConnectionHdl;
typedef websocketpp::lib::error_code TestErrorCode;

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Predicate>
bool WaitUntil(Predicate predicate, int timeout_ms) {
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return predicate();
}

std::string FrameMarker(uint64_t sequence) {
    return "frame:" + std::to_string(static_cast<unsigned long long>(sequence)) + ":";
}

SdkVideoFrame MakeFrame(uint64_t sequence, size_t payload_size) {
    SdkVideoFrame frame;
    frame.device_id = "integration-device";
    frame.stream_id = "integration-stream";
    frame.frame_seq = sequence;
    frame.timestamp_ms = static_cast<int64_t>(sequence);
    frame.width = 1536;
    frame.height = 1152;
    frame.pixel_format = "mjpeg";
    const std::string marker = FrameMarker(sequence);
    frame.payload.assign(payload_size > marker.size() ? payload_size : marker.size(), static_cast<uint8_t>('x'));
    for (size_t index = 0; index < marker.size(); ++index) {
        frame.payload[index] = static_cast<uint8_t>(marker[index]);
    }
    return frame;
}

class TestWsClient {
public:
    TestWsClient() : opened_(false), closed_(false), pairing_error_(false), pending_metadata_(false), pending_sequence_(0), slow_ms_(0) {
        endpoint_.clear_access_channels(websocketpp::log::alevel::all);
        endpoint_.clear_error_channels(websocketpp::log::elevel::all);
        endpoint_.init_asio();
        endpoint_.set_open_handler([this](TestConnectionHdl hdl) {
            {
                std::lock_guard<std::mutex> lock(mu_);
                hdl_ = hdl;
                opened_ = true;
            }
            cv_.notify_all();
        });
        endpoint_.set_close_handler([this](TestConnectionHdl) {
            {
                std::lock_guard<std::mutex> lock(mu_);
                closed_ = true;
            }
            cv_.notify_all();
        });
        endpoint_.set_fail_handler([this](TestConnectionHdl) {
            {
                std::lock_guard<std::mutex> lock(mu_);
                closed_ = true;
            }
            cv_.notify_all();
        });
        endpoint_.set_message_handler(
            [this](TestConnectionHdl, TestWsClientEndpoint::message_ptr message) { HandleMessage(message); });
    }

    ~TestWsClient() { Shutdown(); }

    bool Connect(int port) {
        TestErrorCode ec;
        const std::string uri = "ws://127.0.0.1:" + std::to_string(port) +
                                "/?session_token=integration-token&stream_id=integration-stream";
        const TestWsClientEndpoint::connection_ptr connection = endpoint_.get_connection(uri, ec);
        if (ec) {
            return false;
        }
        endpoint_.connect(connection);
        io_thread_ = std::thread([this]() { endpoint_.run(); });
        std::unique_lock<std::mutex> lock(mu_);
        return cv_.wait_for(lock, std::chrono::seconds(3), [this]() { return opened_ || closed_; }) && opened_;
    }

    void SetSlowConsumerDelay(int delay_ms) { slow_ms_.store(delay_ms); }

    bool WaitForSequence(uint64_t sequence, int timeout_ms) {
        std::unique_lock<std::mutex> lock(mu_);
        return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this, sequence]() {
            for (std::vector<uint64_t>::const_iterator it = received_sequences_.begin();
                 it != received_sequences_.end(); ++it) {
                if (*it == sequence) {
                    return true;
                }
            }
            return false;
        });
    }

    bool WaitForClose(int timeout_ms) {
        std::unique_lock<std::mutex> lock(mu_);
        return cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this]() { return closed_; });
    }

    bool PairingError() const {
        std::lock_guard<std::mutex> lock(mu_);
        return pairing_error_;
    }

    std::vector<uint64_t> ReceivedSequences() const {
        std::lock_guard<std::mutex> lock(mu_);
        return received_sequences_;
    }

    void Shutdown() {
        TestConnectionHdl hdl;
        bool should_close = false;
        {
            std::lock_guard<std::mutex> lock(mu_);
            hdl = hdl_;
            should_close = opened_ && !closed_;
        }
        if (should_close) {
            endpoint_.get_io_service().post([this, hdl]() {
                TestErrorCode ec;
                endpoint_.close(hdl, websocketpp::close::status::normal, "test complete", ec);
            });
            WaitForClose(1000);
        }
        endpoint_.stop();
        if (io_thread_.joinable()) {
            io_thread_.join();
        }
    }

private:
    void HandleMessage(const TestWsClientEndpoint::message_ptr& message) {
        const int delay_ms = slow_ms_.load();
        if (delay_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }

        if (message->get_opcode() == websocketpp::frame::opcode::text) {
            Json value;
            std::string error;
            if (!TryParseJson(message->get_payload(), &value, &error) || value.value("event", "") != "stream.frame_meta") {
                return;
            }
            const uint64_t sequence = value["payload"].value("frame_seq", static_cast<uint64_t>(0));
            {
                std::lock_guard<std::mutex> lock(mu_);
                if (pending_metadata_) {
                    pairing_error_ = true;
                }
                pending_metadata_ = true;
                pending_sequence_ = sequence;
            }
            cv_.notify_all();
            return;
        }

        if (message->get_opcode() != websocketpp::frame::opcode::binary) {
            return;
        }
        const std::string& payload = message->get_payload();
        {
            std::lock_guard<std::mutex> lock(mu_);
            if (!pending_metadata_ || payload.compare(0, FrameMarker(pending_sequence_).size(), FrameMarker(pending_sequence_)) != 0) {
                pairing_error_ = true;
            } else {
                received_sequences_.push_back(pending_sequence_);
            }
            pending_metadata_ = false;
        }
        cv_.notify_all();
    }

    mutable std::mutex mu_;
    std::condition_variable cv_;
    TestWsClientEndpoint endpoint_;
    TestConnectionHdl hdl_;
    std::thread io_thread_;
    bool opened_;
    bool closed_;
    bool pairing_error_;
    bool pending_metadata_;
    uint64_t pending_sequence_;
    std::vector<uint64_t> received_sequences_;
    std::atomic<int> slow_ms_;
};

struct RunningServer {
    std::unique_ptr<SdkWsVideoServer> server;
    int port = 0;
};

RunningServer StartServer() {
    for (int port = 19370; port < 19420; ++port) {
        std::unique_ptr<SdkWsVideoServer> server(new SdkWsVideoServer("127.0.0.1", port));
        server->SetConnectionAuthHandler([](const std::string&, const std::string&) {
            SdkWsVideoServer::ConnectionAuthResult result;
            result.authorized = true;
            result.code = 0;
            result.message = "ok";
            result.connection_id = "integration-connection";
            result.stream_id = "integration-stream";
            return result;
        });
        if (server->Start()) {
            RunningServer running;
            running.server = std::move(server);
            running.port = port;
            return running;
        }
    }
    throw std::runtime_error("unable to bind a websocket integration test port");
}

void TestWirePairingBackpressureAndClose() {
    RunningServer running = StartServer();
    TestWsClient client;
    Require(client.Connect(running.port), "client should connect to the real video websocket server");
    Require(WaitUntil([&running]() { return running.server->GetStats().active_connections == 1u; }, 3000),
            "server should observe the active websocket connection");

    running.server->PublishFrame(MakeFrame(1, 1024));
    Require(client.WaitForSequence(1, 3000), "client should receive the initial metadata and JPEG pair");
    Require(!client.PairingError(), "initial websocket metadata and binary should match");

    client.SetSlowConsumerDelay(3);
    const uint64_t final_sequence = 500;
    for (uint64_t sequence = 2; sequence <= final_sequence; ++sequence) {
        running.server->PublishFrame(MakeFrame(sequence, 64 * 1024));
    }
    Require(client.WaitForSequence(final_sequence, 8000),
            "slow consumer should eventually receive the newest complete frame");
    Require(!client.PairingError(), "all delivered metadata and binary messages should remain paired");
    const SdkWsVideoServer::Stats stats = running.server->GetStats();
    Require(stats.preview_frames_dropped > 0u, "producer burst should replace stale pending preview frames");
    Require(stats.send_failures == 0u, "healthy slow connection should not report send failures");
    Require(stats.binary_frames < final_sequence, "bounded delivery should avoid sending the entire stale burst");

    const std::vector<uint64_t> sequences = client.ReceivedSequences();
    Require(!sequences.empty() && sequences.back() == final_sequence,
            "the newest sequence should be the last delivered preview frame");

    running.server->CloseStream("integration-stream");
    Require(client.WaitForClose(3000), "CloseStream should close the real websocket connection");
    Require(WaitUntil([&running]() { return running.server->GetStats().active_connections == 0u; }, 3000),
            "CloseStream should remove active connection state");
    running.server->PublishFrame(MakeFrame(final_sequence + 1, 1024));
    running.server->Stop();
}

void TestConcurrentPublishAndStop() {
    RunningServer running = StartServer();
    TestWsClient client;
    client.SetSlowConsumerDelay(10);
    Require(client.Connect(running.port), "concurrent-stop client should connect");

    std::atomic<bool> publish(true);
    std::thread publisher([&running, &publish]() {
        uint64_t sequence = 1;
        while (publish.load()) {
            running.server->PublishFrame(MakeFrame(sequence++, 128 * 1024));
        }
        for (int index = 0; index < 50; ++index) {
            running.server->PublishFrame(MakeFrame(sequence++, 1024));
        }
    });

    Require(WaitUntil([&running]() {
                const SdkWsVideoServer::Stats stats = running.server->GetStats();
                return stats.binary_frames > 0u || stats.preview_frames_dropped > 0u;
            }, 3000),
            "publisher should exercise the active connection before stop");
    const std::chrono::steady_clock::time_point stop_started = std::chrono::steady_clock::now();
    running.server->Stop();
    const int64_t stop_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - stop_started)
                                .count();
    publish.store(false);
    publisher.join();
    Require(stop_ms < 3000, "Stop should cancel timers and join without hanging concurrent publishers");
    Require(!client.PairingError(), "stop during publish must not deliver mismatched frame pairs");
    Require(client.WaitForClose(3000), "server Stop should close the active client connection");
    client.Shutdown();
}

} // namespace
} // namespace sdk
} // namespace editor

int main() {
    try {
        editor::sdk::TestWirePairingBackpressureAndClose();
        editor::sdk::TestConcurrentPublishAndStop();
        std::cout << "sdk_ws_video_server_integration_test passed" << std::endl;
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "sdk_ws_video_server_integration_test failed: " << error.what() << std::endl;
        return 1;
    }
}
