#pragma once

#include "protocol/message.hpp"
#include "protocol/framing.hpp"
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <memory>
#include <functional>
#include <deque>
#include <atomic>
#include <mutex>

namespace chat::server {

using tcp = asio::ip::tcp;
using ssl_socket = asio::ssl::stream<tcp::socket>;

class ConnectionManager;

class Connection : public std::enable_shared_from_this<Connection> {
public:
    using Ptr = std::shared_ptr<Connection>;
    using MessageHandler = std::function<void(Ptr, const protocol::Message&)>;
    using ErrorHandler = std::function<void(Ptr, const std::error_code&)>;

    Connection(asio::io_context& io_context,
              asio::ssl::context& ssl_context,
              ConnectionManager& manager);

    ~Connection();

    // Non-copyable
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Get underlying socket for accept
    ssl_socket::lowest_layer_type& socket() { return socket_.lowest_layer(); }

    // Start the connection (perform TLS handshake)
    void start();

    // Send a message
    void send(const protocol::Message& msg);
    void send_raw(const std::vector<uint8_t>& data);

    // Close the connection
    void close();

    // Setters for handlers
    void set_message_handler(MessageHandler handler) { message_handler_ = std::move(handler); }
    void set_error_handler(ErrorHandler handler) { error_handler_ = std::move(handler); }

    // Connection info
    std::string remote_address() const { return remote_address_; }
    uint16_t remote_port() const { return remote_port_; }
    uint64_t id() const { return id_; }

    // Session binding
    void set_user_id(uint64_t user_id) { user_id_ = user_id; }
    uint64_t user_id() const { return user_id_; }
    void set_username(const std::string& name) { username_ = name; }
    const std::string& username() const { return username_; }
    bool is_authenticated() const { return user_id_ != 0; }

private:
    void do_handshake();
    void do_read_header();
    void do_read_body(uint32_t length);
    void do_write();

    static std::atomic<uint64_t> next_id_;

    uint64_t id_;
    ssl_socket socket_;
    ConnectionManager& manager_;

    std::array<uint8_t, protocol::Framing::HEADER_SIZE> header_buffer_;
    std::vector<uint8_t> body_buffer_;

    std::mutex write_mutex_;
    std::deque<std::vector<uint8_t>> write_queue_;
    bool writing_ = false;

    MessageHandler message_handler_;
    ErrorHandler error_handler_;

    std::string remote_address_;
    uint16_t remote_port_ = 0;

    uint64_t user_id_ = 0;
    std::string username_;

    std::atomic<bool> closed_{false};
};

} // namespace chat::server

