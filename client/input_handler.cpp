#include "input_handler.hpp"
#include <algorithm>
#include <iostream>

namespace chat::client {

InputHandler::InputHandler(TlsClient& client) : client_(client) {}

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
            std::cout << "Usage: /login <username> <password>" << std::endl;
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
            std::cout << "Usage: /register <username> <password>" << std::endl;
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
            std::cout << "Usage: /join <room>" << std::endl;
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
            std::cout << "Usage: /dm <user> <message>" << std::endl;
            return;
        }
        std::string message;
        for (size_t i = 1; i < args.size(); ++i) {
            if (i > 1) message += " ";
            message += args[i];
        }
        nlohmann::json payload = {
            {"to", args[0]},
            {"message", message}
        };
        client_.send_command("dm", payload);
        return;
    }

    if (cmd == "rename" || cmd == "nick") {
        if (args.empty()) {
            std::cout << "Usage: /rename <newname>" << std::endl;
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

