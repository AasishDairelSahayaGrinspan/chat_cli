#include "input_handler.hpp"
#include <algorithm>

namespace chat::client {

InputHandler::InputHandler(TlsClient& client, KeyManager& key_manager)
    : client_(client), key_manager_(key_manager) {}

const std::vector<std::string>& InputHandler::available_commands() {
    static const std::vector<std::string> commands = {
        "/login", "/register", "/join", "/leave", "/rooms",
        "/users", "/dm", "/rename", "/quit", "/exit", "/help"
    };
    return commands;
}

void InputHandler::report_error(const std::string& error) {
    if (error_callback_) {
        error_callback_(error);
    }
}

void InputHandler::process_line(const std::string& line) {
    if (line.empty()) return;

    // Check if it's a command
    if (line[0] == '/') {
        auto args = split_args(line.substr(1));
        if (!args.empty()) {
            std::string cmd = args[0];
            std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);
            args.erase(args.begin());
            handle_command(cmd, args);
        }
    } else {
        // Regular chat message
        client_.send_chat(line, current_room_);
    }
}

void InputHandler::handle_command(const std::string& cmd,
                                  const std::vector<std::string>& args) {
    if (cmd == "quit" || cmd == "exit" || cmd == "q") {
        client_.send_command("quit");
        if (quit_callback_) {
            quit_callback_();
        }
        return;
    }

    if (cmd == "help" || cmd == "h" || cmd == "?") {
        client_.send_command("help");
        return;
    }

    if (cmd == "login") {
        if (args.size() < 2) {
            report_error("Usage: /login <username> <password>");
            return;
        }
        nlohmann::json payload = {
            {"username", args[0]},
            {"password", args[1]}
        };
        client_.send_command("login", payload);
        return;
    }

    if (cmd == "register") {
        if (args.size() < 2) {
            report_error("Usage: /register <username> <password>");
            return;
        }
        nlohmann::json payload = {
            {"username", args[0]},
            {"password", args[1]}
        };
        client_.send_command("register", payload);
        return;
    }

    if (cmd == "join" || cmd == "j") {
        if (args.empty()) {
            report_error("Usage: /join <room>");
            return;
        }
        nlohmann::json payload = {{"room", args[0]}};
        client_.send_command("join", payload);
        return;
    }

    if (cmd == "leave" || cmd == "part") {
        nlohmann::json payload;
        if (!args.empty()) {
            payload["room"] = args[0];
        }
        client_.send_command("leave", payload);
        return;
    }

    if (cmd == "users" || cmd == "who") {
        nlohmann::json payload;
        if (!args.empty()) {
            payload["room"] = args[0];
        }
        client_.send_command("users", payload);
        return;
    }

    if (cmd == "dm" || cmd == "msg" || cmd == "pm") {
        if (args.size() < 2) {
            report_error("Usage: /dm <user> <message>");
            return;
        }
        std::string recipient = args[0];
        std::string message;
        for (size_t i = 1; i < args.size(); ++i) {
            if (i > 1) message += " ";
            message += args[i];
        }
        nlohmann::json payload = {
            {"to", recipient},
            {"message", message}
        };

        // Attempt E2EE encryption if we have the recipient's key
        auto cached_key = key_manager_.get_cached_key(recipient);
        if (cached_key.has_value() && key_manager_.is_initialized()) {
            auto encrypted = key_manager_.encrypt(message, cached_key.value());
            if (encrypted.has_value()) {
                payload["message"] = encrypted->ciphertext;
                payload["nonce"] = encrypted->nonce;
                payload["sender_public_key"] = key_manager_.get_public_key();
                payload["encrypted"] = true;
            }
        } else if (key_manager_.is_initialized()) {
            // Request the recipient's key for next time
            auto key_req = protocol::Message::create(protocol::MessageType::KEY_EXCHANGE, username_);
            key_req.payload = {
                {"action", "request_key"},
                {"username", recipient}
            };
            client_.send(key_req);
        }

        client_.send_command("dm", payload);
        return;
    }

    if (cmd == "rename" || cmd == "nick") {
        if (args.empty()) {
            report_error("Usage: /rename <newname>");
            return;
        }
        nlohmann::json payload = {{"newname", args[0]}};
        client_.send_command("rename", payload);
        return;
    }

    if (cmd == "rooms") {
        client_.send_command("rooms");
        return;
    }

    // Unknown command - send to server anyway
    nlohmann::json payload;
    if (!args.empty()) {
        payload["args"] = args;
    }
    client_.send_command(cmd, payload);
}

std::vector<std::string> InputHandler::split_args(const std::string& input) {
    std::vector<std::string> args;
    std::istringstream iss(input);
    std::string arg;

    while (iss >> arg) {
        args.push_back(arg);
    }

    return args;
}

} // namespace chat::client
