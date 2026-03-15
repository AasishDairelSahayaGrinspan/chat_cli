#pragma once

#include "storage_interface.hpp"
#include <sqlite3.h>
#include <mutex>
#include <stdexcept>

namespace chat::server::storage {

class SqliteStorage : public IStorage {
public:
    explicit SqliteStorage(const std::string& db_path);
    ~SqliteStorage() override;

    // Non-copyable
    SqliteStorage(const SqliteStorage&) = delete;
    SqliteStorage& operator=(const SqliteStorage&) = delete;

    // User operations
    bool create_user(const std::string& username, const std::string& password_hash) override;
    std::optional<User> get_user(uint64_t id) override;
    std::optional<User> get_user_by_username(const std::string& username) override;
    bool update_last_login(uint64_t user_id) override;
    bool update_password(uint64_t user_id, const std::string& new_hash) override;
    bool rename_user(uint64_t user_id, const std::string& new_username) override;

    // Room operations
    bool create_room(const std::string& name, const std::string& description = "") override;
    std::optional<Room> get_room(const std::string& name) override;
    std::vector<Room> list_rooms() override;

    // Message operations
    bool store_message(const StoredMessage& msg) override;
    std::vector<StoredMessage> get_room_messages(const std::string& room,
                                                  size_t limit = 50,
                                                  uint64_t before_id = 0) override;

    // Key storage for E2EE
    bool store_public_key(uint64_t user_id, const std::string& public_key) override;
    std::optional<std::string> get_public_key(uint64_t user_id) override;
    std::optional<std::string> get_public_key_by_username(const std::string& username) override;

    // Health check
    bool is_healthy() override;

    // Initialize schema
    void init_schema() override;

private:
    void exec(const char* sql);

    std::string db_path_;
    sqlite3* db_ = nullptr;
    std::mutex mutex_;
};

} // namespace chat::server::storage

