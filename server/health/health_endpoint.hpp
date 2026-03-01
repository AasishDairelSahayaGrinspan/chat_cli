#pragma once

#include "session/session_manager.hpp"
#include "storage/storage_interface.hpp"
#include <asio.hpp>
#include <atomic>
#include <thread>

namespace chat::server {

using tcp = asio::ip::tcp;

// Simple HTTP health check endpoint
class HealthEndpoint {
public:
    HealthEndpoint(asio::io_context& io_context,
                  uint16_t port,
                  SessionManager& session_manager,
                  storage::IStorage& storage);

    ~HealthEndpoint();

    void start();
    void stop();

private:
    void do_accept();
    void handle_request(tcp::socket socket);

    std::string generate_health_response();
    std::string generate_ready_response();

    asio::io_context& io_context_;
    tcp::acceptor acceptor_;
    uint16_t port_;

    SessionManager& session_manager_;
    storage::IStorage& storage_;

    std::atomic<bool> running_{false};
};

} // namespace chat::server

