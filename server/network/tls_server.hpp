#pragma once

#include "connection.hpp"
#include "connection_manager.hpp"
#include "config/config.hpp"
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <vector>
#include <thread>
#include <atomic>
#include <functional>

namespace chat::server {

using tcp = asio::ip::tcp;

class TlsServer {
public:
    using NewConnectionHandler = std::function<void(Connection::Ptr)>;

    explicit TlsServer(const config::ServerConfig& config);
    ~TlsServer();

    // Non-copyable, non-movable
    TlsServer(const TlsServer&) = delete;
    TlsServer& operator=(const TlsServer&) = delete;

    void set_new_connection_handler(NewConnectionHandler handler) {
        new_connection_handler_ = std::move(handler);
    }

    void start();
    void stop();

    ConnectionManager& connection_manager() { return connection_manager_; }
    asio::io_context& io_context() { return io_context_; }

    bool is_running() const { return running_; }

private:
    void do_accept();
    void setup_ssl_context();

    config::ServerConfig config_;

    asio::io_context io_context_;
    asio::ssl::context ssl_context_;
    tcp::acceptor acceptor_;

    ConnectionManager connection_manager_;
    NewConnectionHandler new_connection_handler_;

    std::vector<std::thread> thread_pool_;
    std::atomic<bool> running_{false};
};

} // namespace chat::server

