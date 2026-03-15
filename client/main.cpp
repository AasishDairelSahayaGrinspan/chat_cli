#include "ftxui_app.hpp"
#include "logging/logger.hpp"

#include <iostream>
#include <string>
#include <cstdint>

int main(int argc, char* argv[]) {
    std::string host = "localhost";
    uint16_t port = 8443;
    bool verify_ssl = true;
    std::string ca_cert_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--insecure") {
            verify_ssl = false;
            std::cerr << "WARNING: TLS certificate verification disabled. "
                      << "Do not use in production." << std::endl;
        } else if (arg == "--ca-cert" && i + 1 < argc) {
            ca_cert_path = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: chat_client [host] [port] [options]\n"
                      << "Options:\n"
                      << "  --insecure     Disable TLS certificate verification\n"
                      << "  --ca-cert PATH Load custom CA certificate\n"
                      << "  --help, -h     Show this help\n";
            return 0;
        } else if (host == "localhost" && arg[0] != '-') {
            host = arg;
        } else if (arg[0] != '-') {
            try {
                port = static_cast<uint16_t>(std::stoi(arg));
            } catch (...) {
                std::cerr << "Invalid port: " << arg << std::endl;
                return 1;
            }
        }
    }

    chat::logging::Logger::init("chat_client", "", spdlog::level::warn);

    chat::client::FtxuiApp app(host, port, verify_ssl, ca_cert_path);
    return app.run();
}
