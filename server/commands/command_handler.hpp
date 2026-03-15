#pragma once

#include "room_manager.hpp"
#include "network/connection.hpp"
#include "network/connection_manager.hpp"
#include "session/session_manager.hpp"
#include "auth/auth_service.hpp"
#include "storage/storage_interface.hpp"
#include "protocol/message.hpp"
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <unordered_map>
#include <mutex>

namespace chat::server {

class CommandHandler {
public:
    CommandHandler(ConnectionManager& conn_manager,
                  SessionManager& session_manager,
                  AuthService& auth_service,
                  RoomManager& room_manager,
                  storage::IStorage& storage);

    // Handle incoming message
    void handle(Connection::Ptr conn, const protocol::Message& msg);

private:
    // Dispatch command from payload
    void handle_command(Connection::Ptr conn, const nlohmann::json& payload);

    // Command handlers
    void handle_login(Connection::Ptr conn, const nlohmann::json& payload);
    void handle_register(Connection::Ptr conn, const nlohmann::json& payload);
    void handle_join(Connection::Ptr conn, const nlohmann::json& payload);
    void handle_leave(Connection::Ptr conn, const nlohmann::json& payload);
    void handle_users(Connection::Ptr conn, const nlohmann::json& payload);
    void handle_dm(Connection::Ptr conn, const nlohmann::json& payload);
    void handle_rename(Connection::Ptr conn, const nlohmann::json& payload);
    void handle_quit(Connection::Ptr conn);
    void handle_rooms(Connection::Ptr conn);
    void handle_help(Connection::Ptr conn);

    // Handle key exchange for E2EE
    void handle_key_exchange(Connection::Ptr conn, const nlohmann::json& payload);

    // Handle chat message
    void handle_chat_message(Connection::Ptr conn, const protocol::Message& msg);

    // Parse command from text
    struct ParsedCommand {
        std::string name;
        std::vector<std::string> args;
    };
    std::optional<ParsedCommand> parse_command(const std::string& text);

    // Send error to connection
    void send_error(Connection::Ptr conn, const std::string& message,
                   const std::string& code = "ERROR");

    // Send system message to connection
    void send_system(Connection::Ptr conn, const std::string& message);

    // Require authentication
    bool require_auth(Connection::Ptr conn);

    // Require no authentication (for login/register)
    bool require_no_auth(Connection::Ptr conn);

    // Check rate limit
    bool check_rate_limit(Connection::Ptr conn);

    ConnectionManager& conn_manager_;
    SessionManager& session_manager_;
    AuthService& auth_service_;
    RoomManager& room_manager_;
    storage::IStorage& storage_;

    // Brute-force protection
    struct LoginAttempt {
        size_t failed_count = 0;
        std::chrono::steady_clock::time_point last_attempt{};
        std::chrono::steady_clock::time_point lockout_until{};
    };
    std::mutex login_mutex_;
    std::unordered_map<std::string, LoginAttempt> login_attempts_;

    static constexpr size_t MAX_LOGIN_ATTEMPTS = 5;
    static constexpr size_t LOGIN_LOCKOUT_SECONDS = 900; // 15 minutes
};

} // namespace chat::server

