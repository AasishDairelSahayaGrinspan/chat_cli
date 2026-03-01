#include "tls_server.hpp"
#include "logging/logger.hpp"

namespace chat::server {

TlsServer::TlsServer(const config::ServerConfig& config)
    : config_(config),
      io_context_(static_cast<int>(config.thread_pool_size)),
      ssl_context_(asio::ssl::context::tlsv12_server),
      acceptor_(io_context_) {
    setup_ssl_context();
}

TlsServer::~TlsServer() {
    stop();
}

void TlsServer::setup_ssl_context() {
    ssl_context_.set_options(
        asio::ssl::context::default_workarounds |
        asio::ssl::context::no_sslv2 |
        asio::ssl::context::no_sslv3 |
        asio::ssl::context::no_tlsv1 |
        asio::ssl::context::no_tlsv1_1 |
        asio::ssl::context::single_dh_use
    );

    // Set strong cipher list
    SSL_CTX_set_cipher_list(ssl_context_.native_handle(),
        "ECDHE+AESGCM:DHE+AESGCM:ECDHE+CHACHA20:DHE+CHACHA20:!aNULL:!MD5:!DSS");

    ssl_context_.use_certificate_chain_file(config_.cert_file);
    ssl_context_.use_private_key_file(config_.key_file, asio::ssl::context::pem);

    if (!config_.dh_file.empty()) {
        ssl_context_.use_tmp_dh_file(config_.dh_file);
    }

    LOG_INFO("TLS context configured with certificate: {}", config_.cert_file);
}

void TlsServer::start() {
    if (running_.exchange(true)) {
        return;  // Already running
    }

    // Setup acceptor
    tcp::endpoint endpoint(
        asio::ip::make_address(config_.bind_address),
        config_.port
    );

    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(tcp::acceptor::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen(asio::socket_base::max_listen_connections);

    LOG_INFO("Server listening on {}:{}", config_.bind_address, config_.port);

    // Start accepting connections
    do_accept();

    // Start thread pool
    thread_pool_.reserve(config_.thread_pool_size);
    for (size_t i = 0; i < config_.thread_pool_size; ++i) {
        thread_pool_.emplace_back([this, i]() {
            LOG_DEBUG("Worker thread {} started", i);
            io_context_.run();
            LOG_DEBUG("Worker thread {} stopped", i);
        });
    }

    LOG_INFO("Server started with {} worker threads", config_.thread_pool_size);
}

void TlsServer::stop() {
    if (!running_.exchange(false)) {
        return;  // Not running
    }

    LOG_INFO("Server shutting down...");

    // Stop accepting new connections
    std::error_code ec;
    acceptor_.close(ec);

    // Close all existing connections
    connection_manager_.close_all();

    // Stop the io_context
    io_context_.stop();

    // Join all threads
    for (auto& thread : thread_pool_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    thread_pool_.clear();

    LOG_INFO("Server stopped");
}

void TlsServer::do_accept() {
    auto conn = std::make_shared<Connection>(io_context_, ssl_context_, connection_manager_);

    acceptor_.async_accept(
        conn->socket(),
        [this, conn](const std::error_code& ec) {
            if (!running_) {
                return;
            }

            if (ec) {
                if (ec != asio::error::operation_aborted) {
                    LOG_ERROR("Accept error: {}", ec.message());
                }
            } else {
                connection_manager_.add(conn);

                if (new_connection_handler_) {
                    new_connection_handler_(conn);
                }

                conn->start();
            }

            do_accept();
        });
}

} // namespace chat::server

