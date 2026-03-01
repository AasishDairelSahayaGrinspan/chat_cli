#include "command_handler.hpp"
#include "logging/logger.hpp"
#include <sstream>
#include <algorithm>

namespace chat::server {

CommandHandler::CommandHandler(ConnectionManager& conn_manager,
                              SessionManager& session_manager,
                              AuthService& auth_service,
                              RoomManager& room_manager,
                              storage::IStorage& storage)
    : conn_manager_(conn_manager),
      session_manager_(session_manager),
      auth_service_(auth_service),
      room_manager_(room_manager),
      storage_(storage) {}

void CommandHandler::handle(Connection::Ptr conn, const protocol::Message& msg) {
    // Rate limiting
    if (!check_rate_limit(conn)) {
        send_error(conn, "Rate limit exceeded. Please slow down.", "RATE_LIMITED");
        return;
    }

    // Update session activity
    if (auto* session = session_manager_.get(conn->id())) {
        session->touch();
    }

    switch (msg.type) {
        case protocol::MessageType::COMMAND:
            handle_command(conn, msg.payload);
            break;

        case protocol::MessageType::MESSAGE:
            handle_chat_message(conn, msg);
            break;

        case protocol::MessageType::AUTH_REQUEST:
            if (msg.payload.contains("action")) {
                std::string action = msg.payload["action"];
                if (action == "login") {
                    handle_login(conn, msg.payload);
                } else if (action == "register") {
                    handle_register(conn, msg.payload);
                }
            }
            break;

        default:
            send_error(conn, "Unknown message type", "INVALID_TYPE");
            break;
    }
}

void CommandHandler::handle_command(Connection::Ptr conn, const nlohmann::json& payload) {
    if (!payload.contains("command")) {
        send_error(conn, "Missing command", "INVALID_COMMAND");
        return;
    }

    std::string cmd = payload["command"];

    if (cmd == "login") {
        handle_login(conn, payload);
    } else if (cmd == "register") {
        handle_register(conn, payload);
    } else if (cmd == "join") {
        handle_join(conn, payload);
    } else if (cmd == "leave") {
        handle_leave(conn, payload);
    } else if (cmd == "users") {
        handle_users(conn, payload);
    } else if (cmd == "dm") {
        handle_dm(conn, payload);
    } else if (cmd == "rename") {
        handle_rename(conn, payload);
    } else if (cmd == "quit") {
        handle_quit(conn);
    } else if (cmd == "rooms") {
        handle_rooms(conn);
    } else if (cmd == "help") {
        handle_help(conn);
    } else {
        send_error(conn, "Unknown command: " + cmd, "UNKNOWN_COMMAND");
    }
}

void CommandHandler::handle_login(Connection::Ptr conn, const nlohmann::json& payload) {
    if (!require_no_auth(conn)) return;

    if (!payload.contains("username") || !payload.contains("password")) {
        send_error(conn, "Missing username or password", "INVALID_PARAMS");
        return;
    }

    std::string username = payload["username"];
    std::string password = payload["password"];

    auto [result, user_id] = auth_service_.login(username, password);

    if (result != AuthService::Result::SUCCESS) {
        send_error(conn, AuthService::result_message(result), "AUTH_FAILED");
        return;
    }

    // Check if already logged in elsewhere
    if (auto existing = conn_manager_.get_by_username(username)) {
        send_error(conn, "User already logged in from another location", "ALREADY_LOGGED_IN");
        return;
    }

    // Authenticate session
    if (!session_manager_.authenticate(conn->id(), user_id, username)) {
        send_error(conn, "Failed to create session", "SESSION_ERROR");
        return;
    }

    conn->set_user_id(user_id);
    conn->set_username(username);

    // Register in connection manager so get_by_username works
    conn_manager_.register_username(username, conn);


    // Send success response
    auto response = protocol::Message::create(protocol::MessageType::AUTH_RESPONSE, "server");
    response.payload = {
        {"success", true},
        {"user_id", user_id},
        {"username", username},
        {"message", "Welcome back, " + username + "!"}
    };
    conn->send(response);

    // Auto-join general room
    nlohmann::json join_payload = {{"room", "general"}};
    handle_join(conn, join_payload);

    // Broadcast presence
    auto presence = protocol::Message::create(protocol::MessageType::PRESENCE, username);
    presence.payload = {{"action", "online"}, {"user", username}};
    conn_manager_.broadcast_except(conn->id(), presence);

    LOG_INFO("User '{}' logged in from {}", username, conn->remote_address());
}

void CommandHandler::handle_register(Connection::Ptr conn, const nlohmann::json& payload) {
    if (!require_no_auth(conn)) return;

    if (!payload.contains("username") || !payload.contains("password")) {
        send_error(conn, "Missing username or password", "INVALID_PARAMS");
        return;
    }

    std::string username = payload["username"];
    std::string password = payload["password"];

    auto result = auth_service_.register_user(username, password);

    if (result != AuthService::Result::SUCCESS) {
        send_error(conn, AuthService::result_message(result), "REGISTER_FAILED");
        return;
    }

    auto response = protocol::Message::create(protocol::MessageType::AUTH_RESPONSE, "server");
    response.payload = {
        {"success", true},
        {"message", "Registration successful! You can now login with /login"}
    };
    conn->send(response);

    LOG_INFO("New user registered: {}", username);
}

void CommandHandler::handle_join(Connection::Ptr conn, const nlohmann::json& payload) {
    if (!require_auth(conn)) return;

    if (!payload.contains("room")) {
        send_error(conn, "Missing room name", "INVALID_PARAMS");
        return;
    }

    std::string room = payload["room"];

    // Validate room name
    if (room.empty() || room.length() > protocol::MAX_ROOM_NAME_LENGTH) {
        send_error(conn, "Invalid room name", "INVALID_ROOM");
        return;
    }

    if (room_manager_.join(conn, room)) {
        auto response = protocol::Message::create(protocol::MessageType::ROOM_EVENT, conn->username(), room);
        response.payload = {
            {"action", "joined"},
            {"room", room},
            {"users", room_manager_.get_room_users(room)}
        };
        conn->send(response);

        send_system(conn, "You joined #" + room);
    } else {
        send_error(conn, "Failed to join room", "JOIN_FAILED");
    }
}

void CommandHandler::handle_leave(Connection::Ptr conn, const nlohmann::json& payload) {
    if (!require_auth(conn)) return;

    std::string room;
    if (payload.contains("room")) {
        room = payload["room"];
    } else {
        room = room_manager_.get_current_room(conn->id());
    }

    if (room.empty()) {
        send_error(conn, "Not in any room", "NOT_IN_ROOM");
        return;
    }

    if (room_manager_.leave(conn, room)) {
        auto response = protocol::Message::create(protocol::MessageType::ROOM_EVENT, conn->username(), room);
        response.payload = {{"action", "left"}, {"room", room}};
        conn->send(response);

        send_system(conn, "You left #" + room);
    } else {
        send_error(conn, "Failed to leave room", "LEAVE_FAILED");
    }
}

void CommandHandler::handle_users(Connection::Ptr conn, const nlohmann::json& payload) {
    if (!require_auth(conn)) return;

    std::string room;
    if (payload.contains("room")) {
        room = payload["room"];
    } else {
        room = room_manager_.get_current_room(conn->id());
    }

    std::vector<std::string> users;
    if (room.empty()) {
        // List all online users
        users = session_manager_.get_online_users();
    } else {
        users = room_manager_.get_room_users(room);
    }

    auto response = protocol::Message::create(protocol::MessageType::SYSTEM, "server");
    response.payload = {
        {"action", "user_list"},
        {"room", room},
        {"users", users},
        {"count", users.size()}
    };
    conn->send(response);
}

void CommandHandler::handle_dm(Connection::Ptr conn, const nlohmann::json& payload) {
    if (!require_auth(conn)) return;

    if (!payload.contains("to") || !payload.contains("message")) {
        send_error(conn, "Usage: /dm <username> <message>", "INVALID_PARAMS");
        return;
    }

    std::string target_user = payload["to"];
    std::string message = payload["message"];

    if (message.empty() || message.length() > protocol::MAX_MESSAGE_SIZE) {
        send_error(conn, "Invalid message", "INVALID_MESSAGE");
        return;
    }

    auto target = conn_manager_.get_by_username(target_user);
    if (!target) {
        send_error(conn, "User '" + target_user + "' is not online", "USER_OFFLINE");
        return;
    }

    // Send to recipient
    auto dm = protocol::Message::create(protocol::MessageType::DIRECT_MESSAGE, conn->username());
    dm.payload = {
        {"from", conn->username()},
        {"to", target_user},
        {"message", message}
    };
    target->send(dm);

    // Confirm to sender
    auto confirm = protocol::Message::create(protocol::MessageType::DIRECT_MESSAGE, conn->username());
    confirm.payload = {
        {"from", conn->username()},
        {"to", target_user},
        {"message", message},
        {"sent", true}
    };
    conn->send(confirm);

    LOG_DEBUG("DM from '{}' to '{}': {}", conn->username(), target_user,
              message.substr(0, 50));
}

void CommandHandler::handle_rename(Connection::Ptr conn, const nlohmann::json& payload) {
    if (!require_auth(conn)) return;

    if (!payload.contains("newname")) {
        send_error(conn, "Usage: /rename <newname>", "INVALID_PARAMS");
        return;
    }

    std::string new_name = payload["newname"];

    if (!AuthService::is_valid_username(new_name)) {
        send_error(conn, "Invalid username format", "INVALID_USERNAME");
        return;
    }

    // Check if name is taken
    if (conn_manager_.get_by_username(new_name)) {
        send_error(conn, "Username already taken", "NAME_TAKEN");
        return;
    }

    // Update in storage
    if (!storage_.rename_user(conn->user_id(), new_name)) {
        send_error(conn, "Failed to update username", "RENAME_FAILED");
        return;
    }

    std::string old_name = conn->username();
    conn->set_username(new_name);

    // Update username index in connection manager
    conn_manager_.unregister_username(old_name);
    conn_manager_.register_username(new_name, conn);

    // Update session
    if (auto* session = session_manager_.get(conn->id())) {
        session->username = new_name;
    }

    // Notify everyone
    auto notification = protocol::Message::create(protocol::MessageType::PRESENCE, "server");
    notification.payload = {
        {"action", "rename"},
        {"old_name", old_name},
        {"new_name", new_name}
    };
    conn_manager_.broadcast(notification);

    send_system(conn, "You are now known as " + new_name);
    LOG_INFO("User '{}' renamed to '{}'", old_name, new_name);
}

void CommandHandler::handle_quit(Connection::Ptr conn) {
    send_system(conn, "Goodbye!");

    if (conn->is_authenticated()) {
        auto presence = protocol::Message::create(protocol::MessageType::PRESENCE, conn->username());
        presence.payload = {{"action", "offline"}, {"user", conn->username()}};
        conn_manager_.broadcast_except(conn->id(), presence);

        room_manager_.leave_all(conn);
    }

    conn->close();
}

void CommandHandler::handle_rooms(Connection::Ptr conn) {
    if (!require_auth(conn)) return;

    auto rooms = storage_.list_rooms();
    std::vector<nlohmann::json> room_list;

    for (const auto& room : rooms) {
        room_list.push_back({
            {"name", room.name},
            {"description", room.description},
            {"users", room_manager_.get_room_users(room.name).size()}
        });
    }

    auto response = protocol::Message::create(protocol::MessageType::SYSTEM, "server");
    response.payload = {
        {"action", "room_list"},
        {"rooms", room_list}
    };
    conn->send(response);
}

void CommandHandler::handle_help(Connection::Ptr conn) {
    std::string help = R"(
Available commands:
  /login <username> <password>  - Login to your account
  /register <username> <password> - Create a new account
  /join <room>                  - Join a chat room
  /leave [room]                 - Leave current or specified room
  /rooms                        - List available rooms
  /users [room]                 - List users in room or online
  /dm <user> <message>          - Send private message
  /rename <newname>             - Change your username
  /quit                         - Disconnect from server
  /help                         - Show this help message
)";

    send_system(conn, help);
}

void CommandHandler::handle_chat_message(Connection::Ptr conn, const protocol::Message& msg) {
    if (!require_auth(conn)) return;

    std::string room = msg.room;
    if (room.empty()) {
        room = room_manager_.get_current_room(conn->id());
    }

    if (room.empty()) {
        send_error(conn, "Not in any room. Use /join <room> first.", "NOT_IN_ROOM");
        return;
    }

    std::string content;
    if (msg.payload.contains("message")) {
        content = msg.payload["message"];
    } else if (msg.payload.contains("content")) {
        content = msg.payload["content"];
    }

    if (content.empty()) {
        return;
    }

    if (content.length() > protocol::MAX_MESSAGE_SIZE) {
        send_error(conn, "Message too long", "MESSAGE_TOO_LONG");
        return;
    }

    // Check for command
    if (content[0] == '/') {
        auto cmd = parse_command(content);
        if (cmd) {
            nlohmann::json payload = {{"command", cmd->name}};
            if (!cmd->args.empty()) {
                if (cmd->name == "join" || cmd->name == "leave") {
                    payload["room"] = cmd->args[0];
                } else if (cmd->name == "dm" && cmd->args.size() >= 2) {
                    payload["to"] = cmd->args[0];
                    payload["message"] = cmd->args[1];
                    for (size_t i = 2; i < cmd->args.size(); ++i) {
                        payload["message"] = payload["message"].get<std::string>() + " " + cmd->args[i];
                    }
                } else if (cmd->name == "rename" && !cmd->args.empty()) {
                    payload["newname"] = cmd->args[0];
                } else if (cmd->name == "users" && !cmd->args.empty()) {
                    payload["room"] = cmd->args[0];
                } else if ((cmd->name == "login" || cmd->name == "register") && cmd->args.size() >= 2) {
                    payload["username"] = cmd->args[0];
                    payload["password"] = cmd->args[1];
                }
            }
            handle_command(conn, payload);
            return;
        }
    }

    // Store message
    storage::StoredMessage stored_msg;
    stored_msg.message_id = msg.id;
    stored_msg.sender_id = conn->user_id();
    stored_msg.room = room;
    stored_msg.content = content;
    stored_msg.created_at = msg.timestamp;
    storage_.store_message(stored_msg);

    // Broadcast to room
    auto chat_msg = protocol::Message::create(protocol::MessageType::MESSAGE, conn->username(), room);
    chat_msg.id = msg.id;
    chat_msg.payload = {{"message", content}};

    room_manager_.broadcast(room, chat_msg);
}

std::optional<CommandHandler::ParsedCommand> CommandHandler::parse_command(const std::string& text) {
    if (text.empty() || text[0] != '/') {
        return std::nullopt;
    }

    std::istringstream iss(text.substr(1));
    ParsedCommand cmd;

    iss >> cmd.name;
    if (cmd.name.empty()) {
        return std::nullopt;
    }

    // Convert to lowercase
    std::transform(cmd.name.begin(), cmd.name.end(), cmd.name.begin(), ::tolower);

    std::string arg;
    while (iss >> arg) {
        cmd.args.push_back(arg);
    }

    return cmd;
}

void CommandHandler::send_error(Connection::Ptr conn, const std::string& message,
                                const std::string& code) {
    conn->send(protocol::Message::error(message, code));
}

void CommandHandler::send_system(Connection::Ptr conn, const std::string& message) {
    conn->send(protocol::Message::system(message));
}

bool CommandHandler::require_auth(Connection::Ptr conn) {
    if (!conn->is_authenticated()) {
        send_error(conn, "Please login first. Use /login <username> <password>", "NOT_AUTHENTICATED");
        return false;
    }
    return true;
}

bool CommandHandler::require_no_auth(Connection::Ptr conn) {
    if (conn->is_authenticated()) {
        send_error(conn, "Already logged in", "ALREADY_AUTHENTICATED");
        return false;
    }
    return true;
}

bool CommandHandler::check_rate_limit(Connection::Ptr conn) {
    return session_manager_.check_rate_limit(conn->id());
}

} // namespace chat::server

