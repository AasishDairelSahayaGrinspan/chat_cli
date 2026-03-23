#include "postgres_storage.hpp"

#include <stdexcept>

namespace chat::server::storage {

PostgresStorage::PostgresStorage(const std::string& connection_string)
    : connection_string_(connection_string) {
    if (connection_string_.empty()) {
        throw std::runtime_error("PostgreSQL connection string is empty. Set 'postgres_conn' in config.");
    }

#ifndef ENABLE_POSTGRES
    throw std::runtime_error(
        "PostgreSQL backend is disabled at build time. Rebuild with -DENABLE_POSTGRES=ON.");
#endif
}

bool PostgresStorage::create_user(const std::string&, const std::string&) {
#ifdef ENABLE_POSTGRES
    throw std::runtime_error("PostgreSQL create_user not implemented yet.");
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

std::optional<User> PostgresStorage::get_user(uint64_t) {
#ifdef ENABLE_POSTGRES
    throw std::runtime_error("PostgreSQL get_user not implemented yet.");
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

std::optional<User> PostgresStorage::get_user_by_username(const std::string&) {
#ifdef ENABLE_POSTGRES
    throw std::runtime_error("PostgreSQL get_user_by_username not implemented yet.");
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

bool PostgresStorage::update_last_login(uint64_t) {
#ifdef ENABLE_POSTGRES
    throw std::runtime_error("PostgreSQL update_last_login not implemented yet.");
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

bool PostgresStorage::update_password(uint64_t, const std::string&) {
#ifdef ENABLE_POSTGRES
    throw std::runtime_error("PostgreSQL update_password not implemented yet.");
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

bool PostgresStorage::rename_user(uint64_t, const std::string&) {
#ifdef ENABLE_POSTGRES
    throw std::runtime_error("PostgreSQL rename_user not implemented yet.");
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

bool PostgresStorage::create_room(const std::string&, const std::string&) {
#ifdef ENABLE_POSTGRES
    throw std::runtime_error("PostgreSQL create_room not implemented yet.");
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

std::optional<Room> PostgresStorage::get_room(const std::string&) {
#ifdef ENABLE_POSTGRES
    throw std::runtime_error("PostgreSQL get_room not implemented yet.");
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

std::vector<Room> PostgresStorage::list_rooms() {
#ifdef ENABLE_POSTGRES
    throw std::runtime_error("PostgreSQL list_rooms not implemented yet.");
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

bool PostgresStorage::store_message(const StoredMessage&) {
#ifdef ENABLE_POSTGRES
    throw std::runtime_error("PostgreSQL store_message not implemented yet.");
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

std::vector<StoredMessage> PostgresStorage::get_room_messages(const std::string&, size_t, uint64_t) {
#ifdef ENABLE_POSTGRES
    throw std::runtime_error("PostgreSQL get_room_messages not implemented yet.");
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

bool PostgresStorage::store_public_key(uint64_t, const std::string&) {
#ifdef ENABLE_POSTGRES
    throw std::runtime_error("PostgreSQL store_public_key not implemented yet.");
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

std::optional<std::string> PostgresStorage::get_public_key(uint64_t) {
#ifdef ENABLE_POSTGRES
    throw std::runtime_error("PostgreSQL get_public_key not implemented yet.");
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

std::optional<std::string> PostgresStorage::get_public_key_by_username(const std::string&) {
#ifdef ENABLE_POSTGRES
    throw std::runtime_error("PostgreSQL get_public_key_by_username not implemented yet.");
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

bool PostgresStorage::is_healthy() {
#ifdef ENABLE_POSTGRES
    return false;
#else
    return false;
#endif
}

void PostgresStorage::init_schema() {
#ifdef ENABLE_POSTGRES
    throw std::runtime_error("PostgreSQL init_schema not implemented yet.");
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

} // namespace chat::server::storage
