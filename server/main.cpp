#include "network/tls_server.hpp"
#include "network/connection_manager.hpp"
#include "session/session_manager.hpp"
#include "auth/auth_service.hpp"
#include "commands/command_handler.hpp"
#include "commands/room_manager.hpp"
#include "storage/sqlite_storage.hpp"
#include "health/health_endpoint.hpp"
#include "config/config.hpp"
#include "logging/logger.hpp"

#include <asio/signal_set.hpp>
#include <iostream>
#include <memory>
#include <csignal>

using namespace chat;
using namespace chat::server;

namespace {
    std::atomic<bool> shutdown_requested{false};
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
        auto storage = std::make_unique<storage::SqliteStorage>(cfg.sqlite_path);
        storage->init_schema();

        // Create server
        TlsServer server(cfg);

        // Create managers
        SessionManager session_manager;
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

