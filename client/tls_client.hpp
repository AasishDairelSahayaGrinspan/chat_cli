#pragma once

#include "protocol/message.hpp"
#include "protocol/framing.hpp"
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <functional>
#include <memory>
#include <deque>
#include <mutex>
#include <atomic>
#include <thread>

namespace chat::client {

using tcp = asio::ip::tcp;
using ssl_socket = asio::ssl::stream<tcp::socket>;

class TlsClient {
public:
    using MessageHandler = std::function<void(const protocol::Message&)>;
    using ConnectHandler = std::function<void(bool success, const std::string& error)>;
    using DisconnectHandler = std::function<void(const std::string& reason)>;

    TlsClient(const std::string& host, uint16_t port, bool verify_ssl = true, const std::string& ca_cert_path = "");
    ~TlsClient();

    // Non-copyable
    TlsClient(const TlsClient&) = delete;
    TlsClient& operator=(const TlsClient&) = delete;

    // Connection
    void connect(ConnectHandler handler);
    void disconnect();
    bool is_connected() const { return connected_; }

    // Send message
    void send(const protocol::Message& msg);
    void send_command(const std::string& cmd, const nlohmann::json& payload = {});
    void send_chat(const std::string& message, const std::string& room = "");

    // Handlers
    void set_message_handler(MessageHandler handler) { message_handler_ = std::move(handler); }
    void set_disconnect_handler(DisconnectHandler handler) { disconnect_handler_ = std::move(handler); }

    // Reconnection
    void enable_reconnect(size_t max_attempts = 5, size_t delay_ms = 2000);
    void disable_reconnect();

    // Run the io_context (call from main thread or dedicated thread)
    void run();
    void stop();

private:
    void do_connect(tcp::resolver::results_type endpoints);
    void do_handshake();
    void do_read_header();
    void do_read_body(uint32_t length);
    void do_write();
    void do_reconnect();

    void handle_disconnect(const std::string& reason);

    std::string host_;
    uint16_t port_;
    bool verify_ssl_;
    std::string ca_cert_path_;

    asio::io_context io_context_;
    asio::ssl::context ssl_context_;
    std::unique_ptr<ssl_socket> socket_;
    tcp::resolver resolver_;

    std::array<uint8_t, protocol::Framing::HEADER_SIZE> header_buffer_;
    std::vector<uint8_t> body_buffer_;

    std::mutex write_mutex_;
    std::deque<std::vector<uint8_t>> write_queue_;
    bool writing_ = false;

    MessageHandler message_handler_;
    ConnectHandler connect_handler_;
    DisconnectHandler disconnect_handler_;

    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};

    // Reconnection
    bool reconnect_enabled_ = false;
    size_t max_reconnect_attempts_ = 5;
    size_t reconnect_delay_ms_ = 2000;
    size_t reconnect_attempts_ = 0;
    std::unique_ptr<asio::steady_timer> reconnect_timer_;

    std::thread io_thread_;
};

} // namespace chat::client

