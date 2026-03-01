#pragma once

#include <string>
#include <optional>
#include <vector>
#include <cstdint>
#include <memory>

namespace chat::server::storage {

struct User {
    uint64_t id = 0;
    std::string username;
    std::string password_hash;
    int64_t created_at = 0;
    int64_t last_login = 0;
    bool active = true;
};

struct StoredMessage {
    uint64_t id = 0;
    std::string message_id;  // UUID
    uint64_t sender_id = 0;
    std::string room;
    std::string content;
    int64_t created_at = 0;
};

struct Room {
    uint64_t id = 0;
    std::string name;
    std::string description;
    int64_t created_at = 0;
    bool is_private = false;
};

class IStorage {
public:
    virtual ~IStorage() = default;

    // User operations
    virtual bool create_user(const std::string& username, const std::string& password_hash) = 0;
    virtual std::optional<User> get_user(uint64_t id) = 0;
    virtual std::optional<User> get_user_by_username(const std::string& username) = 0;
    virtual bool update_last_login(uint64_t user_id) = 0;
    virtual bool update_password(uint64_t user_id, const std::string& new_hash) = 0;
    virtual bool rename_user(uint64_t user_id, const std::string& new_username) = 0;

    // Room operations
    virtual bool create_room(const std::string& name, const std::string& description = "") = 0;
    virtual std::optional<Room> get_room(const std::string& name) = 0;
    virtual std::vector<Room> list_rooms() = 0;

    // Message operations
    virtual bool store_message(const StoredMessage& msg) = 0;
    virtual std::vector<StoredMessage> get_room_messages(const std::string& room,
                                                         size_t limit = 50,
                                                         uint64_t before_id = 0) = 0;

    // Health check
    virtual bool is_healthy() = 0;

    // Initialize schema
    virtual void init_schema() = 0;
};

using StoragePtr = std::unique_ptr<IStorage>;

} // namespace chat::server::storage

