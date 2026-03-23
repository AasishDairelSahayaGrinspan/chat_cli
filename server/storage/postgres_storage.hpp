#pragma once

#include "storage_interface.hpp"

#include <memory>
#include <string>

namespace chat::server::storage {

class PostgresStorage : public IStorage {
public:
    explicit PostgresStorage(const std::string& connection_string);

    ~PostgresStorage() override = default;

    bool create_user(const std::string& username, const std::string& password_hash) override;
    std::optional<User> get_user(uint64_t id) override;
    std::optional<User> get_user_by_username(const std::string& username) override;
    bool update_last_login(uint64_t user_id) override;
    bool update_password(uint64_t user_id, const std::string& new_hash) override;
    bool rename_user(uint64_t user_id, const std::string& new_username) override;

    bool create_room(const std::string& name, const std::string& description = "") override;
    std::optional<Room> get_room(const std::string& name) override;
    std::vector<Room> list_rooms() override;

    bool store_message(const StoredMessage& msg) override;
    std::vector<StoredMessage> get_room_messages(const std::string& room,
                                                 size_t limit = 50,
                                                 uint64_t before_id = 0) override;

    bool store_public_key(uint64_t user_id, const std::string& public_key) override;
    std::optional<std::string> get_public_key(uint64_t user_id) override;
    std::optional<std::string> get_public_key_by_username(const std::string& username) override;

    bool is_healthy() override;
    void init_schema() override;

private:
    std::string connection_string_;
};

} // namespace chat::server::storage

