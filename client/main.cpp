#include "tls_client.hpp"
#include "input_handler.hpp"
#include "ui_renderer.hpp"
#include "config/config.hpp"
#include "logging/logger.hpp"

#include <iostream>
#include <atomic>
#include <thread>
#include <csignal>

using namespace chat;
using namespace chat::client;

namespace {
    std::atomic<bool> running{true};
}

void signal_handler(int) {
    running = false;
}

int main(int argc, char* argv[]) {
    // Parse arguments
    std::string host = "localhost";
    uint16_t port = 8443;

    if (argc > 1) {
        host = argv[1];
    }
    if (argc > 2) {
        port = static_cast<uint16_t>(std::stoi(argv[2]));
    }

    // Initialize logging (minimal for client)
    logging::Logger::init("chat_client", "", spdlog::level::warn);

    // Setup signal handling
    std::signal(SIGINT, signal_handler);
#ifdef SIGTERM
    std::signal(SIGTERM, signal_handler);
#endif

    // Create components
    UiRenderer renderer;
    TlsClient client(host, port, false);  // No SSL verification for self-signed certs
    InputHandler input(client);

    // Setup quit handler
    input.set_quit_callback([&]() {
        running = false;
    });

    // Setup message handler
    client.set_message_handler([&](const protocol::Message& msg) {
        // Clear current input line, render message, reprint prompt
        renderer.clear_line();
        renderer.render(msg);

        // Update state based on message
        if (msg.type == protocol::MessageType::AUTH_RESPONSE) {
            if (msg.payload.value("success", false)) {
                input.set_authenticated(true);
                if (msg.payload.contains("username")) {
                    input.set_username(msg.payload["username"]);
                }
            }
        } else if (msg.type == protocol::MessageType::ROOM_EVENT) {
            std::string action = msg.payload.value("action", "");
            if (action == "joined") {
                input.set_current_room(msg.payload.value("room", ""));
            } else if (action == "left" &&
                       msg.payload.value("room", "") == input.current_room()) {
                input.set_current_room("");
            }
        } else if (msg.type == protocol::MessageType::PRESENCE) {
            std::string action = msg.payload.value("action", "");
            if (action == "rename" &&
                msg.payload.value("old_name", "") == input.username()) {
                input.set_username(msg.payload.value("new_name", ""));
            }
        }

        renderer.print_prompt(input.username(), input.current_room());
    });

    // Setup disconnect handler
    client.set_disconnect_handler([&](const std::string& reason) {
        renderer.clear_line();
        renderer.print_error("Disconnected: " + reason);

        if (!client.is_connected()) {
            renderer.print_status("Attempting to reconnect...");
        }
    });

    // Enable reconnection
    client.enable_reconnect(5, 3000);

    // Start the client
    client.run();

    std::cout << "Connecting to " << host << ":" << port << "..." << std::endl;

    // Connect
    std::atomic<bool> connected{false};
    std::string connect_error;

    client.connect([&](bool success, const std::string& error) {
        if (success) {
            connected = true;
        } else {
            connect_error = error;
            running = false;
        }
    });

    // Wait for connection
    for (int i = 0; i < 50 && !connected && running; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (!connected) {
        if (!connect_error.empty()) {
            std::cerr << "Connection failed: " << connect_error << std::endl;
        } else {
            std::cerr << "Connection timeout" << std::endl;
        }
        client.stop();
        return 1;
    }

    std::cout << "Connected! Type /help for available commands." << std::endl;
    std::cout << std::endl;

    // Main input loop
    renderer.print_prompt(input.username(), input.current_room());

    std::string line;
    while (running && std::getline(std::cin, line)) {
        if (!running) break;

        if (!line.empty()) {
            input.process_line(line);
        }

        if (running) {
            renderer.print_prompt(input.username(), input.current_room());
        }
    }

    // Cleanup
    std::cout << std::endl << "Goodbye!" << std::endl;
    client.stop();

    return 0;
}

