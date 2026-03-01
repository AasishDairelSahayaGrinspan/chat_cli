#include "ui_renderer.hpp"

namespace chat::client {

void UiRenderer::render(const protocol::Message& msg) {
    std::string timestamp = format_timestamp(msg.timestamp);

    switch (msg.type) {
        case protocol::MessageType::MESSAGE: {
            std::string content;
            if (msg.payload.contains("message")) {
                content = msg.payload["message"];
            }

            std::cout << color(GRAY) << "[" << timestamp << "] "
                      << color(RESET);

            if (!msg.room.empty()) {
                std::cout << color(CYAN) << "#" << msg.room << " " << color(RESET);
            }

            std::cout << color(BOLD) << color(GREEN) << msg.from << color(RESET)
                      << ": " << content << std::endl;
            break;
        }

        case protocol::MessageType::DIRECT_MESSAGE: {
            std::string from = msg.payload.value("from", msg.from);
            std::string to = msg.payload.value("to", "");
            std::string message = msg.payload.value("message", "");
            bool sent = msg.payload.value("sent", false);

            std::cout << color(GRAY) << "[" << timestamp << "] "
                      << color(RESET) << color(MAGENTA);

            if (sent) {
                std::cout << "[DM -> " << to << "] ";
            } else {
                std::cout << "[DM <- " << from << "] ";
            }

            std::cout << color(RESET) << message << std::endl;
            break;
        }

        case protocol::MessageType::SYSTEM: {
            std::string message;
            if (msg.payload.contains("message")) {
                message = msg.payload["message"];
            } else if (msg.payload.contains("action")) {
                std::string action = msg.payload["action"];

                if (action == "user_list") {
                    std::cout << color(CYAN) << "Online users";
                    if (msg.payload.contains("room") && !msg.payload["room"].get<std::string>().empty()) {
                        std::cout << " in #" << msg.payload["room"].get<std::string>();
                    }
                    std::cout << ": " << color(RESET);

                    if (msg.payload.contains("users")) {
                        auto users = msg.payload["users"];
                        bool first = true;
                        for (const auto& user : users) {
                            if (!first) std::cout << ", ";
                            std::cout << user.get<std::string>();
                            first = false;
                        }
                    }
                    std::cout << " (" << msg.payload.value("count", 0) << " total)" << std::endl;
                    return;
                }

                if (action == "room_list") {
                    std::cout << color(CYAN) << "Available rooms:" << color(RESET) << std::endl;
                    if (msg.payload.contains("rooms")) {
                        for (const auto& room : msg.payload["rooms"]) {
                            std::cout << "  #" << room["name"].get<std::string>();
                            if (room.contains("users")) {
                                std::cout << " (" << room["users"].get<int>() << " users)";
                            }
                            if (room.contains("description") &&
                                !room["description"].get<std::string>().empty()) {
                                std::cout << " - " << room["description"].get<std::string>();
                            }
                            std::cout << std::endl;
                        }
                    }
                    return;
                }
            }

            if (!message.empty()) {
                std::cout << color(CYAN) << "* " << message << color(RESET) << std::endl;
            }
            break;
        }

        case protocol::MessageType::ERROR: {
            std::string message = msg.payload.value("message", "Unknown error");
            std::string code = msg.payload.value("code", "ERROR");

            std::cout << color(RED) << "Error [" << code << "]: "
                      << message << color(RESET) << std::endl;
            break;
        }

        case protocol::MessageType::PRESENCE: {
            std::string action = msg.payload.value("action", "");
            std::string user = msg.payload.value("user", msg.from);

            std::cout << color(YELLOW) << "* ";

            if (action == "online") {
                std::cout << user << " is now online";
            } else if (action == "offline") {
                std::cout << user << " has disconnected";
            } else if (action == "join") {
                std::cout << user << " joined #" << msg.room;
            } else if (action == "leave") {
                std::cout << user << " left #" << msg.room;
            } else if (action == "rename") {
                std::string old_name = msg.payload.value("old_name", "");
                std::string new_name = msg.payload.value("new_name", "");
                std::cout << old_name << " is now known as " << new_name;
            } else {
                std::cout << user << " " << action;
            }

            std::cout << color(RESET) << std::endl;
            break;
        }

        case protocol::MessageType::AUTH_RESPONSE: {
            bool success = msg.payload.value("success", false);
            std::string message = msg.payload.value("message", "");

            if (success) {
                std::cout << color(GREEN) << "✓ " << message << color(RESET) << std::endl;
            } else {
                std::cout << color(RED) << "✗ " << message << color(RESET) << std::endl;
            }
            break;
        }

        case protocol::MessageType::ROOM_EVENT: {
            std::string action = msg.payload.value("action", "");
            std::string room = msg.payload.value("room", msg.room);

            if (action == "joined") {
                std::cout << color(CYAN) << "Joined #" << room;

                if (msg.payload.contains("users")) {
                    std::cout << " - Users: ";
                    auto users = msg.payload["users"];
                    bool first = true;
                    for (const auto& user : users) {
                        if (!first) std::cout << ", ";
                        std::cout << user.get<std::string>();
                        first = false;
                    }
                }

                std::cout << color(RESET) << std::endl;
            } else if (action == "left") {
                std::cout << color(CYAN) << "Left #" << room << color(RESET) << std::endl;
            }
            break;
        }

        default:
            // Just dump the message for debugging
            std::cout << color(GRAY) << "[DEBUG] " << nlohmann::json(msg).dump()
                      << color(RESET) << std::endl;
            break;
    }
}

void UiRenderer::print_status(const std::string& status) {
    std::cout << color(CYAN) << "* " << status << color(RESET) << std::endl;
}

void UiRenderer::print_error(const std::string& error) {
    std::cout << color(RED) << "Error: " << error << color(RESET) << std::endl;
}

void UiRenderer::print_prompt(const std::string& username, const std::string& room) {
    std::cout << color(BOLD);

    if (!username.empty()) {
        std::cout << color(GREEN) << username << color(RESET) << color(BOLD);
    }

    if (!room.empty()) {
        std::cout << color(CYAN) << " #" << room;
    }

    std::cout << color(RESET) << "> " << std::flush;
}

void UiRenderer::clear_line() {
    std::cout << "\r\033[K" << std::flush;
}

std::string UiRenderer::format_timestamp(int64_t timestamp) {
    time_t time = static_cast<time_t>(timestamp);
    struct tm* tm_info = localtime(&time);

    char buffer[16];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", tm_info);
    return buffer;
}

std::string UiRenderer::color(const std::string& code) {
    return colors_enabled_ ? code : "";
}

} // namespace chat::client

