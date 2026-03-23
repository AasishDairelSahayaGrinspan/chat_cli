#include "network/tls_server.hpp"
#include "network/connection_manager.hpp"
#include "session/session_manager.hpp"
#include "auth/auth_service.hpp"
#include "commands/command_handler.hpp"
#include "commands/room_manager.hpp"
#include "storage/sqlite_storage.hpp"
#include "storage/postgres_storage.hpp"
#include "health/health_endpoint.hpp"
#include "config/config.hpp"
#include "logging/logger.hpp"

#include <asio/signal_set.hpp>
#include <asio/steady_timer.hpp>
#include <iostream>
#include <memory>
#include <csignal>

using namespace chat;
using namespace chat::server;

namespace {
    std::atomic<bool> shutdown_requested{false};

    storage::StoragePtr create_storage_from_config(const config::ServerConfig& cfg) {
        if (cfg.storage_type == "sqlite") {
            LOG_INFO("Using SQLite storage: {}", cfg.sqlite_path);
            return std::make_unique<storage::SqliteStorage>(cfg.sqlite_path);
        }

        if (cfg.storage_type == "postgres") {
            if (cfg.postgres_conn.empty()) {
                throw std::runtime_error(
                    "storage_type=postgres requires non-empty 'postgres_conn' in config");
            }
            LOG_INFO("Using PostgreSQL storage backend");
            return std::make_unique<storage::PostgresStorage>(cfg.postgres_conn);
        }

        throw std::runtime_error("Unsupported storage_type: " + cfg.storage_type);
    }
}

int main(int argc, char* argv[]) {
    try {
        // Load configuration
        config::ServerConfig cfg;
        if (argc > 1) {
            cfg = config::load_config_or_default<config::ServerConfig>(argv[1]);
        }

        // Initialize logging
        logging::Logger::init("chat_server", cfg.log_file,
                             config::parse_log_level(cfg.log_level));

        LOG_INFO("Chat Server v1.0.0 starting...");
        LOG_INFO("Configuration: bind={}:{}, threads={}",
                 cfg.bind_address, cfg.port, cfg.thread_pool_size);

        // Initialize storage
        auto storage = create_storage_from_config(cfg);
        storage->init_schema();

        // Create server
        TlsServer server(cfg);

        // Create managers
        DualRateLimiter::Config rl_config;
        rl_config.ip_config = {cfg.rate_limit_messages_per_second / 2, cfg.rate_limit_burst / 2};
        rl_config.user_config = {cfg.rate_limit_messages_per_second, cfg.rate_limit_burst};
        SessionManager session_manager(rl_config);
        AuthService auth_service(*storage);
        RoomManager room_manager(server.connection_manager(), *storage);
        CommandHandler command_handler(
            server.connection_manager(),
            session_manager,
            auth_service,
            room_manager,
            *storage
        );

        // Setup connection handlers
        server.set_new_connection_handler([&](Connection::Ptr conn) {
            // Create session
            session_manager.create(conn->id(), conn->remote_address());

            // Set message handler
            conn->set_message_handler([&](Connection::Ptr c, const protocol::Message& msg) {
                command_handler.handle(c, msg);
            });

            // Set error handler
            conn->set_error_handler([&](Connection::Ptr c, const std::error_code& ec) {
                if (c->is_authenticated()) {
                    // Notify others of disconnect
                    auto presence = protocol::Message::create(
                        protocol::MessageType::PRESENCE, c->username());
                    presence.payload = {{"action", "offline"}, {"user", c->username()}};
                    server.connection_manager().broadcast_except(c->id(), presence);

                    room_manager.leave_all(c);
                }

                session_manager.remove(c->id());
                server.connection_manager().remove(c);

                if (ec && ec != asio::error::operation_aborted) {
                    LOG_DEBUG("Connection {} closed: {}", c->id(), ec.message());
                }
            });

            // Send welcome message
            auto welcome = protocol::Message::system(
                "Welcome to Chat Server! Please /login or /register to continue.");
            conn->send(welcome);
        });

        // Setup health endpoint
        HealthEndpoint health(server.io_context(), cfg.health_port,
                             session_manager, *storage);

        // Setup signal handling
#ifdef SIGTERM
        asio::signal_set signals(server.io_context(), SIGINT, SIGTERM);
#else
        asio::signal_set signals(server.io_context(), SIGINT);
#endif
        signals.async_wait([&](const std::error_code&, int sig) {
            LOG_INFO("Received signal {}, initiating shutdown...", sig);
            shutdown_requested = true;

            // Notify all clients
            auto shutdown_msg = protocol::Message::system(
                "Server is shutting down. Goodbye!");
            server.connection_manager().broadcast(shutdown_msg);

            // Stop accepting - but don't join threads from within a worker thread
            health.stop();
            std::error_code ec;
            server.io_context().stop();
        });

        // Periodic session cleanup timer
        std::function<void(const std::error_code&)> session_cleanup;
        asio::steady_timer session_timer(server.io_context());

        session_cleanup = [&](const std::error_code& ec) {
            if (ec || shutdown_requested) return;

            LOG_DEBUG("Running session cleanup...");

            // Collect stale sessions to clean up
            std::vector<uint64_t> stale_connections;
            session_manager.cleanup_inactive(
                std::chrono::seconds(cfg.session_timeout_seconds));

            // Reschedule
            session_timer.expires_after(std::chrono::seconds(60));
            session_timer.async_wait(session_cleanup);
        };

        session_timer.expires_after(std::chrono::seconds(60));
        session_timer.async_wait(session_cleanup);

        // Periodic rate limiter bucket cleanup timer
        std::function<void(const std::error_code&)> rate_limit_cleanup;
        asio::steady_timer rate_limit_timer(server.io_context());

        rate_limit_cleanup = [&](const std::error_code& ec) {
            if (ec || shutdown_requested) return;

            LOG_DEBUG("Running rate limiter cleanup...");
            session_manager.rate_limiter().cleanup();

            rate_limit_timer.expires_after(std::chrono::minutes(5));
            rate_limit_timer.async_wait(rate_limit_cleanup);
        };

        rate_limit_timer.expires_after(std::chrono::minutes(5));
        rate_limit_timer.async_wait(rate_limit_cleanup);

        // Start services
        health.start();
        server.start();

        LOG_INFO("Server is ready and accepting connections");

        // Wait for shutdown
        while (!shutdown_requested) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Clean shutdown from main thread (safe to join worker threads here)
        server.stop();

        LOG_INFO("Server shutdown complete");
        logging::Logger::shutdown();

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        LOG_CRITICAL("Fatal error: {}", e.what());
        return 1;
    }
}

