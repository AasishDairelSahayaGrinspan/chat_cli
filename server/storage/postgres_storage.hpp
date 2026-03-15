#pragma once

// Postgres storage stub - interface ready for production implementation
// This would require libpq or libpqxx

#include "storage_interface.hpp"
#include <stdexcept>

namespace chat::server::storage {

class PostgresStorage : public IStorage {
public:
    explicit PostgresStorage(const std::string& connection_string) {
        (void)connection_string;
        throw std::runtime_error("PostgreSQL storage not implemented. "
                                "Install libpq and implement this class for production use.");
    }

    ~PostgresStorage() override = default;

    bool create_user(const std::string&, const std::string&) override { return false; }
    std::optional<User> get_user(uint64_t) override { return std::nullopt; }
    std::optional<User> get_user_by_username(const std::string&) override { return std::nullopt; }
    bool update_last_login(uint64_t) override { return false; }
    bool update_password(uint64_t, const std::string&) override { return false; }
    bool rename_user(uint64_t, const std::string&) override { return false; }

    bool create_room(const std::string&, const std::string&) override { return false; }
    std::optional<Room> get_room(const std::string&) override { return std::nullopt; }
    std::vector<Room> list_rooms() override { return {}; }

    bool store_message(const StoredMessage&) override { return false; }
    std::vector<StoredMessage> get_room_messages(const std::string&, size_t, uint64_t) override { return {}; }

    bool store_public_key(uint64_t, const std::string&) override {
        throw std::runtime_error("PostgreSQL not implemented");
    }
    std::optional<std::string> get_public_key(uint64_t) override {
        throw std::runtime_error("PostgreSQL not implemented");
    }
    std::optional<std::string> get_public_key_by_username(const std::string&) override {
        throw std::runtime_error("PostgreSQL not implemented");
    }

    bool is_healthy() override { return false; }
    void init_schema() override {}
};

} // namespace chat::server::storage

