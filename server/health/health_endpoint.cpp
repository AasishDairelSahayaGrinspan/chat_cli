#include "health_endpoint.hpp"
#include "logging/logger.hpp"
#include <nlohmann/json.hpp>
#include <sstream>

namespace chat::server {

HealthEndpoint::HealthEndpoint(asio::io_context& io_context,
                              uint16_t port,
                              SessionManager& session_manager,
                              storage::IStorage& storage)
    : io_context_(io_context),
      acceptor_(io_context),
      port_(port),
      session_manager_(session_manager),
      storage_(storage) {}

HealthEndpoint::~HealthEndpoint() {
    stop();
}

void HealthEndpoint::start() {
    if (running_.exchange(true)) {
        return;
    }

    tcp::endpoint endpoint(tcp::v4(), port_);
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(tcp::acceptor::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen();

    LOG_INFO("Health endpoint listening on port {}", port_);
    do_accept();
}

void HealthEndpoint::stop() {
    if (!running_.exchange(false)) {
        return;
    }

    std::error_code ec;
    acceptor_.close(ec);
}

void HealthEndpoint::do_accept() {
    acceptor_.async_accept(
        [this](const std::error_code& ec, tcp::socket socket) {
            if (!running_) return;

            if (!ec) {
                handle_request(std::move(socket));
            }

            do_accept();
        });
}

void HealthEndpoint::handle_request(tcp::socket socket) {
    auto buffer = std::make_shared<std::array<char, 1024>>();
    auto sock = std::make_shared<tcp::socket>(std::move(socket));

    sock->async_read_some(
        asio::buffer(*buffer),
        [this, sock, buffer](
            const std::error_code& ec, size_t bytes) {

            if (ec) return;

            std::string request(buffer->data(), bytes);
            auto response = std::make_shared<std::string>();

            // Parse simple HTTP request
            if (request.find("GET /healthz") != std::string::npos ||
                request.find("GET /health") != std::string::npos) {
                *response = generate_health_response();
            } else if (request.find("GET /readyz") != std::string::npos ||
                       request.find("GET /ready") != std::string::npos) {
                *response = generate_ready_response();
            } else {
                // Default to health check
                *response = generate_health_response();
            }

            asio::async_write(
                *sock,
                asio::buffer(*response),
                [sock, response](const std::error_code&, size_t) {});
        });
}

std::string HealthEndpoint::generate_health_response() {
    nlohmann::json body = {
        {"status", "healthy"},
        {"connections", session_manager_.count()},
        {"authenticated_users", session_manager_.authenticated_count()},
        {"storage_healthy", storage_.is_healthy()}
    };

    std::ostringstream response;
    response << "HTTP/1.1 200 OK\r\n";
    response << "Content-Type: application/json\r\n";
    response << "Connection: close\r\n";
    response << "Content-Length: " << body.dump().size() << "\r\n";
    response << "\r\n";
    response << body.dump();

    return response.str();
}

std::string HealthEndpoint::generate_ready_response() {
    bool storage_ok = storage_.is_healthy();

    nlohmann::json body = {
        {"ready", storage_ok},
        {"checks", {
            {"storage", storage_ok ? "ok" : "failed"}
        }}
    };

    int status = storage_ok ? 200 : 503;
    std::string status_text = storage_ok ? "OK" : "Service Unavailable";

    std::ostringstream response;
    response << "HTTP/1.1 " << status << " " << status_text << "\r\n";
    response << "Content-Type: application/json\r\n";
    response << "Connection: close\r\n";
    response << "Content-Length: " << body.dump().size() << "\r\n";
    response << "\r\n";
    response << body.dump();

    return response.str();
}

} // namespace chat::server

