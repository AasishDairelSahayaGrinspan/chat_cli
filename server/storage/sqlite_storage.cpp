#include "sqlite_storage.hpp"
#include "logging/logger.hpp"
#include <chrono>
#include <algorithm>

namespace chat::server::storage {

SqliteStorage::SqliteStorage(const std::string& db_path) : db_path_(db_path) {
    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string error = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        throw std::runtime_error("Failed to open database: " + error);
    }

    // Enable WAL mode for better concurrency
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA synchronous=NORMAL");
    exec("PRAGMA foreign_keys=ON");

    LOG_INFO("SQLite database opened: {}", db_path);
}

SqliteStorage::~SqliteStorage() {
    if (db_) {
        sqlite3_close(db_);
        LOG_DEBUG("SQLite database closed");
    }
}

void SqliteStorage::exec(const char* sql) {
    char* errmsg = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string error = errmsg ? errmsg : "Unknown error";
        sqlite3_free(errmsg);
        throw std::runtime_error("SQL error: " + error);
    }
}

void SqliteStorage::init_schema() {
    std::lock_guard<std::mutex> lock(mutex_);

    exec(R"(
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            username TEXT UNIQUE NOT NULL,
            password_hash TEXT NOT NULL,
            created_at INTEGER NOT NULL,
            last_login INTEGER,
            active INTEGER DEFAULT 1
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS rooms (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT UNIQUE NOT NULL,
            description TEXT,
            created_at INTEGER NOT NULL,
            is_private INTEGER DEFAULT 0
        )
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            message_id TEXT NOT NULL,
            sender_id INTEGER NOT NULL,
            room TEXT NOT NULL,
            content TEXT NOT NULL,
            created_at INTEGER NOT NULL,
            FOREIGN KEY (sender_id) REFERENCES users(id)
        )
    )");

    exec("CREATE INDEX IF NOT EXISTS idx_messages_room ON messages(room, created_at)");
    exec("CREATE INDEX IF NOT EXISTS idx_users_username ON users(username)");

    exec(R"(
        CREATE TABLE IF NOT EXISTS user_keys (
            user_id INTEGER PRIMARY KEY REFERENCES users(id),
            public_key TEXT NOT NULL,
            created_at INTEGER DEFAULT (strftime('%s', 'now')),
            updated_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )");

    // Create default general room (inline to avoid mutex deadlock)
    {
        const char* sql = "INSERT OR IGNORE INTO rooms (name, description, created_at) VALUES ('general', 'General discussion', strftime('%s','now'))";
        exec(sql);
    }

    LOG_INFO("Database schema initialized");
}

bool SqliteStorage::create_user(const std::string& username, const std::string& password_hash) {
    std::lock_guard<std::mutex> lock(mutex_);

    const char* sql = "INSERT INTO users (username, password_hash, created_at) VALUES (?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;

    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("Failed to prepare statement: {}", sqlite3_errmsg(db_));
        return false;
    }

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, password_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, now);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        LOG_WARN("Failed to create user '{}': {}", username, sqlite3_errmsg(db_));
        return false;
    }

    LOG_INFO("Created user: {}", username);
    return true;
}

std::optional<User> SqliteStorage::get_user(uint64_t id) {
    std::lock_guard<std::mutex> lock(mutex_);

    const char* sql = "SELECT id, username, password_hash, created_at, last_login, active "
                      "FROM users WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(id));

    std::optional<User> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        User user;
        user.id = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
        user.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        user.password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        user.created_at = sqlite3_column_int64(stmt, 3);
        user.last_login = sqlite3_column_int64(stmt, 4);
        user.active = sqlite3_column_int(stmt, 5) != 0;
        result = user;
    }

    sqlite3_finalize(stmt);
    return result;
}

std::optional<User> SqliteStorage::get_user_by_username(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);

    const char* sql = "SELECT id, username, password_hash, created_at, last_login, active "
                      "FROM users WHERE username = ?";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<User> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        User user;
        user.id = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
        user.username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        user.password_hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        user.created_at = sqlite3_column_int64(stmt, 3);
        user.last_login = sqlite3_column_int64(stmt, 4);
        user.active = sqlite3_column_int(stmt, 5) != 0;
        result = user;
    }

    sqlite3_finalize(stmt);
    return result;
}

bool SqliteStorage::update_last_login(uint64_t user_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    const char* sql = "UPDATE users SET last_login = ? WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    sqlite3_bind_int64(stmt, 1, now);
    sqlite3_bind_int64(stmt, 2, static_cast<int64_t>(user_id));

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

bool SqliteStorage::update_password(uint64_t user_id, const std::string& new_hash) {
    std::lock_guard<std::mutex> lock(mutex_);

    const char* sql = "UPDATE users SET password_hash = ? WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, new_hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, static_cast<int64_t>(user_id));

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

bool SqliteStorage::rename_user(uint64_t user_id, const std::string& new_username) {
    std::lock_guard<std::mutex> lock(mutex_);

    const char* sql = "UPDATE users SET username = ? WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, new_username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, static_cast<int64_t>(user_id));

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        LOG_WARN("Failed to rename user {}: {}", user_id, sqlite3_errmsg(db_));
        return false;
    }

    return true;
}

bool SqliteStorage::create_room(const std::string& name, const std::string& description) {
    std::lock_guard<std::mutex> lock(mutex_);

    const char* sql = "INSERT OR IGNORE INTO rooms (name, description, created_at) VALUES (?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    auto now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, description.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, now);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

std::optional<Room> SqliteStorage::get_room(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    const char* sql = "SELECT id, name, description, created_at, is_private FROM rooms WHERE name = ?";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<Room> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        Room room;
        room.id = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
        room.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        auto desc = sqlite3_column_text(stmt, 2);
        room.description = desc ? reinterpret_cast<const char*>(desc) : "";
        room.created_at = sqlite3_column_int64(stmt, 3);
        room.is_private = sqlite3_column_int(stmt, 4) != 0;
        result = room;
    }

    sqlite3_finalize(stmt);
    return result;
}

std::vector<Room> SqliteStorage::list_rooms() {
    std::lock_guard<std::mutex> lock(mutex_);

    const char* sql = "SELECT id, name, description, created_at, is_private FROM rooms WHERE is_private = 0";
    sqlite3_stmt* stmt = nullptr;

    std::vector<Room> rooms;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return rooms;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Room room;
        room.id = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
        room.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        auto desc = sqlite3_column_text(stmt, 2);
        room.description = desc ? reinterpret_cast<const char*>(desc) : "";
        room.created_at = sqlite3_column_int64(stmt, 3);
        room.is_private = sqlite3_column_int(stmt, 4) != 0;
        rooms.push_back(room);
    }

    sqlite3_finalize(stmt);
    return rooms;
}

bool SqliteStorage::store_message(const StoredMessage& msg) {
    std::lock_guard<std::mutex> lock(mutex_);

    const char* sql = "INSERT INTO messages (message_id, sender_id, room, content, created_at) "
                      "VALUES (?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(stmt, 1, msg.message_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, static_cast<int64_t>(msg.sender_id));
    sqlite3_bind_text(stmt, 3, msg.room.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, msg.content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, msg.created_at);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

std::vector<StoredMessage> SqliteStorage::get_room_messages(const std::string& room,
                                                            size_t limit,
                                                            uint64_t before_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string sql = "SELECT id, message_id, sender_id, room, content, created_at "
                      "FROM messages WHERE room = ?";
    if (before_id > 0) {
        sql += " AND id < ?";
    }
    sql += " ORDER BY id DESC LIMIT ?";

    sqlite3_stmt* stmt = nullptr;
    std::vector<StoredMessage> messages;

    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return messages;
    }

    int idx = 1;
    sqlite3_bind_text(stmt, idx++, room.c_str(), -1, SQLITE_TRANSIENT);
    if (before_id > 0) {
        sqlite3_bind_int64(stmt, idx++, static_cast<int64_t>(before_id));
    }
    sqlite3_bind_int(stmt, idx, static_cast<int>(limit));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        StoredMessage msg;
        msg.id = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
        msg.message_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        msg.sender_id = static_cast<uint64_t>(sqlite3_column_int64(stmt, 2));
        msg.room = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        msg.content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        msg.created_at = sqlite3_column_int64(stmt, 5);
        messages.push_back(msg);
    }

    sqlite3_finalize(stmt);

    // Reverse to get chronological order
    std::reverse(messages.begin(), messages.end());
    return messages;
}

bool SqliteStorage::store_public_key(uint64_t user_id, const std::string& public_key) {
    std::lock_guard<std::mutex> lock(mutex_);

    const char* sql = "INSERT OR REPLACE INTO user_keys (user_id, public_key, updated_at) "
                      "VALUES (?, ?, strftime('%s', 'now'))";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_ERROR("Failed to prepare store_public_key: {}", sqlite3_errmsg(db_));
        return false;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));
    sqlite3_bind_text(stmt, 2, public_key.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        LOG_WARN("Failed to store public key for user {}: {}", user_id, sqlite3_errmsg(db_));
        return false;
    }

    return true;
}

std::optional<std::string> SqliteStorage::get_public_key(uint64_t user_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    const char* sql = "SELECT public_key FROM user_keys WHERE user_id = ?";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<int64_t>(user_id));

    std::optional<std::string> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    }

    sqlite3_finalize(stmt);
    return result;
}

std::optional<std::string> SqliteStorage::get_public_key_by_username(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);

    const char* sql = "SELECT uk.public_key FROM user_keys uk "
                      "JOIN users u ON uk.user_id = u.id "
                      "WHERE u.username = ?";
    sqlite3_stmt* stmt = nullptr;

    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return std::nullopt;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    std::optional<std::string> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    }

    sqlite3_finalize(stmt);
    return result;
}

bool SqliteStorage::is_healthy() {
    std::lock_guard<std::mutex> lock(mutex_);

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT 1", -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    bool healthy = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return healthy;
}

} // namespace chat::server::storage

