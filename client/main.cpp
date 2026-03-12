#include "ftxui_app.hpp"
#include "logging/logger.hpp"

#include <iostream>
#include <string>
#include <cstdint>

int main(int argc, char* argv[]) {
    // Parse arguments
    std::string host = "localhost";
    uint16_t port = 8443;

    if (argc > 1) {
        host = argv[1];
    }
    if (argc > 2) {
        try {
            port = static_cast<uint16_t>(std::stoi(argv[2]));
        } catch (...) {
            std::cerr << "Invalid port: " << argv[2] << std::endl;
            return 1;
        }
    }

    // Initialize logging (file-only for client, since FTXUI owns the terminal)
    chat::logging::Logger::init("chat_client", "", spdlog::level::warn);

    // Run the FTXUI-based application
    chat::client::FtxuiApp app(host, port);
    return app.run();
}
