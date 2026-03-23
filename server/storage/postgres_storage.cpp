#include "postgres_storage.hpp"

#include "logging/logger.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <stdexcept>

#ifdef ENABLE_POSTGRES
#include <libpq-fe.h>
#endif

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

namespace {

#ifdef ENABLE_POSTGRES
struct PgResultDeleter {
    void operator()(PGresult* r) const {
        if (r) {
            PQclear(r);
        }
    }
};

struct PgConnDeleter {
    void operator()(PGconn* c) const {
        if (c) {
            PQfinish(c);
        }
    }
};

using PgResultPtr = std::unique_ptr<PGresult, PgResultDeleter>;
using PgConnPtr = std::unique_ptr<PGconn, PgConnDeleter>;

PgConnPtr connect_or_throw(const std::string& conn_string) {
    PgConnPtr conn(PQconnectdb(conn_string.c_str()));
    if (!conn || PQstatus(conn.get()) != CONNECTION_OK) {
        std::string err = conn ? PQerrorMessage(conn.get()) : "Unknown PostgreSQL connection error";
        throw std::runtime_error("PostgreSQL connection failed: " + err);
    }
    return conn;
}

bool command_ok(const PGresult* res) {
    return res && (PQresultStatus(res) == PGRES_COMMAND_OK);
}

bool tuples_ok(const PGresult* res) {
    return res && (PQresultStatus(res) == PGRES_TUPLES_OK);
}

bool is_unique_violation(const PGresult* res) {
    if (!res) {
        return false;
    }
    const char* sqlstate = PQresultErrorField(res, PG_DIAG_SQLSTATE);
    return sqlstate && std::string(sqlstate) == "23505";
}

std::optional<int64_t> parse_int64_cell(const PGresult* res, int row, int col) {
    if (PQgetisnull(res, row, col)) {
        return std::nullopt;
    }
    try {
        return std::stoll(PQgetvalue(res, row, col));
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<uint64_t> parse_uint64_cell(const PGresult* res, int row, int col) {
    auto signed_val = parse_int64_cell(res, row, col);
    if (!signed_val.has_value() || signed_val.value() < 0) {
        return std::nullopt;
    }
    return static_cast<uint64_t>(signed_val.value());
}

#endif

} // namespace

bool PostgresStorage::create_user(const std::string& username, const std::string& password_hash) {
#ifdef ENABLE_POSTGRES
    auto conn = connect_or_throw(connection_string_);

    auto now = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    const char* params[3] = {username.c_str(), password_hash.c_str(), now.c_str()};

    PgResultPtr res(PQexecParams(
        conn.get(),
        "INSERT INTO users (username, password_hash, created_at) VALUES ($1, $2, $3)",
        3,
        nullptr,
        params,
        nullptr,
        nullptr,
        0));

    if (!command_ok(res.get())) {
        if (is_unique_violation(res.get())) {
            return false;
        }
        LOG_ERROR("PostgreSQL create_user failed: {}", PQresultErrorMessage(res.get()));
        return false;
    }

    return true;
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

std::optional<User> PostgresStorage::get_user(uint64_t id) {
#ifdef ENABLE_POSTGRES
    auto conn = connect_or_throw(connection_string_);
    auto id_str = std::to_string(id);
    const char* params[1] = {id_str.c_str()};

    PgResultPtr res(PQexecParams(
        conn.get(),
        "SELECT id, username, password_hash, created_at, last_login, active FROM users WHERE id = $1",
        1,
        nullptr,
        params,
        nullptr,
        nullptr,
        0));

    if (!tuples_ok(res.get())) {
        LOG_ERROR("PostgreSQL get_user failed: {}", PQresultErrorMessage(res.get()));
        return std::nullopt;
    }
    if (PQntuples(res.get()) == 0) {
        return std::nullopt;
    }

    User user;
    user.id = parse_uint64_cell(res.get(), 0, 0).value_or(0);
    user.username = PQgetvalue(res.get(), 0, 1);
    user.password_hash = PQgetvalue(res.get(), 0, 2);
    user.created_at = parse_int64_cell(res.get(), 0, 3).value_or(0);
    user.last_login = parse_int64_cell(res.get(), 0, 4).value_or(0);
    user.active = std::string(PQgetvalue(res.get(), 0, 5)) == "t";
    return user;
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

std::optional<User> PostgresStorage::get_user_by_username(const std::string& username) {
#ifdef ENABLE_POSTGRES
    auto conn = connect_or_throw(connection_string_);
    const char* params[1] = {username.c_str()};

    PgResultPtr res(PQexecParams(
        conn.get(),
        "SELECT id, username, password_hash, created_at, last_login, active FROM users WHERE username = $1",
        1,
        nullptr,
        params,
        nullptr,
        nullptr,
        0));

    if (!tuples_ok(res.get())) {
        LOG_ERROR("PostgreSQL get_user_by_username failed: {}", PQresultErrorMessage(res.get()));
        return std::nullopt;
    }
    if (PQntuples(res.get()) == 0) {
        return std::nullopt;
    }

    User user;
    user.id = parse_uint64_cell(res.get(), 0, 0).value_or(0);
    user.username = PQgetvalue(res.get(), 0, 1);
    user.password_hash = PQgetvalue(res.get(), 0, 2);
    user.created_at = parse_int64_cell(res.get(), 0, 3).value_or(0);
    user.last_login = parse_int64_cell(res.get(), 0, 4).value_or(0);
    user.active = std::string(PQgetvalue(res.get(), 0, 5)) == "t";
    return user;
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

bool PostgresStorage::update_last_login(uint64_t user_id) {
#ifdef ENABLE_POSTGRES
    auto conn = connect_or_throw(connection_string_);
    auto user_id_str = std::to_string(user_id);
    auto now = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    const char* params[2] = {now.c_str(), user_id_str.c_str()};

    PgResultPtr res(PQexecParams(
        conn.get(),
        "UPDATE users SET last_login = $1 WHERE id = $2",
        2,
        nullptr,
        params,
        nullptr,
        nullptr,
        0));

    if (!command_ok(res.get())) {
        LOG_ERROR("PostgreSQL update_last_login failed: {}", PQresultErrorMessage(res.get()));
        return false;
    }

    return std::string(PQcmdTuples(res.get())) != "0";
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

bool PostgresStorage::update_password(uint64_t user_id, const std::string& new_hash) {
#ifdef ENABLE_POSTGRES
    auto conn = connect_or_throw(connection_string_);
    auto user_id_str = std::to_string(user_id);
    const char* params[2] = {new_hash.c_str(), user_id_str.c_str()};

    PgResultPtr res(PQexecParams(
        conn.get(),
        "UPDATE users SET password_hash = $1 WHERE id = $2",
        2,
        nullptr,
        params,
        nullptr,
        nullptr,
        0));

    if (!command_ok(res.get())) {
        LOG_ERROR("PostgreSQL update_password failed: {}", PQresultErrorMessage(res.get()));
        return false;
    }

    return std::string(PQcmdTuples(res.get())) != "0";
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

bool PostgresStorage::rename_user(uint64_t user_id, const std::string& new_username) {
#ifdef ENABLE_POSTGRES
    auto conn = connect_or_throw(connection_string_);
    auto user_id_str = std::to_string(user_id);
    const char* params[2] = {new_username.c_str(), user_id_str.c_str()};

    PgResultPtr res(PQexecParams(
        conn.get(),
        "UPDATE users SET username = $1 WHERE id = $2",
        2,
        nullptr,
        params,
        nullptr,
        nullptr,
        0));

    if (!command_ok(res.get())) {
        if (is_unique_violation(res.get())) {
            return false;
        }
        LOG_ERROR("PostgreSQL rename_user failed: {}", PQresultErrorMessage(res.get()));
        return false;
    }

    return std::string(PQcmdTuples(res.get())) != "0";
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

bool PostgresStorage::create_room(const std::string& name, const std::string& description) {
#ifdef ENABLE_POSTGRES
    auto conn = connect_or_throw(connection_string_);
    auto now = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    const char* params[3] = {name.c_str(), description.c_str(), now.c_str()};

    PgResultPtr res(PQexecParams(
        conn.get(),
        "INSERT INTO rooms (name, description, created_at) VALUES ($1, $2, $3) "
        "ON CONFLICT (name) DO NOTHING",
        3,
        nullptr,
        params,
        nullptr,
        nullptr,
        0));

    if (!command_ok(res.get())) {
        LOG_ERROR("PostgreSQL create_room failed: {}", PQresultErrorMessage(res.get()));
        return false;
    }

    return true;
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

std::optional<Room> PostgresStorage::get_room(const std::string& name) {
#ifdef ENABLE_POSTGRES
    auto conn = connect_or_throw(connection_string_);
    const char* params[1] = {name.c_str()};

    PgResultPtr res(PQexecParams(
        conn.get(),
        "SELECT id, name, description, created_at, is_private FROM rooms WHERE name = $1",
        1,
        nullptr,
        params,
        nullptr,
        nullptr,
        0));

    if (!tuples_ok(res.get())) {
        LOG_ERROR("PostgreSQL get_room failed: {}", PQresultErrorMessage(res.get()));
        return std::nullopt;
    }
    if (PQntuples(res.get()) == 0) {
        return std::nullopt;
    }

    Room room;
    room.id = parse_uint64_cell(res.get(), 0, 0).value_or(0);
    room.name = PQgetvalue(res.get(), 0, 1);
    room.description = PQgetisnull(res.get(), 0, 2) ? "" : PQgetvalue(res.get(), 0, 2);
    room.created_at = parse_int64_cell(res.get(), 0, 3).value_or(0);
    room.is_private = std::string(PQgetvalue(res.get(), 0, 4)) == "t";
    return room;
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

std::vector<Room> PostgresStorage::list_rooms() {
#ifdef ENABLE_POSTGRES
    auto conn = connect_or_throw(connection_string_);

    PgResultPtr res(PQexecParams(
        conn.get(),
        "SELECT id, name, description, created_at, is_private "
        "FROM rooms WHERE is_private = false ORDER BY name ASC",
        0,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        0));

    std::vector<Room> rooms;
    if (!tuples_ok(res.get())) {
        LOG_ERROR("PostgreSQL list_rooms failed: {}", PQresultErrorMessage(res.get()));
        return rooms;
    }

    int count = PQntuples(res.get());
    rooms.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        Room room;
        room.id = parse_uint64_cell(res.get(), i, 0).value_or(0);
        room.name = PQgetvalue(res.get(), i, 1);
        room.description = PQgetisnull(res.get(), i, 2) ? "" : PQgetvalue(res.get(), i, 2);
        room.created_at = parse_int64_cell(res.get(), i, 3).value_or(0);
        room.is_private = std::string(PQgetvalue(res.get(), i, 4)) == "t";
        rooms.push_back(std::move(room));
    }

    return rooms;
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

bool PostgresStorage::store_message(const StoredMessage& msg) {
#ifdef ENABLE_POSTGRES
    auto conn = connect_or_throw(connection_string_);
    auto sender_id_str = std::to_string(msg.sender_id);
    auto created_at_str = std::to_string(msg.created_at);
    const char* params[5] = {
        msg.message_id.c_str(),
        sender_id_str.c_str(),
        msg.room.c_str(),
        msg.content.c_str(),
        created_at_str.c_str()};

    PgResultPtr res(PQexecParams(
        conn.get(),
        "INSERT INTO messages (message_id, sender_id, room, content, created_at) VALUES ($1, $2, $3, $4, $5)",
        5,
        nullptr,
        params,
        nullptr,
        nullptr,
        0));

    if (!command_ok(res.get())) {
        LOG_ERROR("PostgreSQL store_message failed: {}", PQresultErrorMessage(res.get()));
        return false;
    }

    return true;
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

std::vector<StoredMessage> PostgresStorage::get_room_messages(const std::string& room,
                                                              size_t limit,
                                                              uint64_t before_id) {
#ifdef ENABLE_POSTGRES
    auto conn = connect_or_throw(connection_string_);

    auto limit_str = std::to_string(limit);
    std::vector<StoredMessage> messages;

    const char* sql_with_before =
        "SELECT id, message_id, sender_id, room, content, created_at "
        "FROM messages WHERE room = $1 AND id < $2 ORDER BY id DESC LIMIT $3";
    const char* sql_without_before =
        "SELECT id, message_id, sender_id, room, content, created_at "
        "FROM messages WHERE room = $1 ORDER BY id DESC LIMIT $2";

    PgResultPtr res(nullptr);
    if (before_id > 0) {
        auto before_id_str = std::to_string(before_id);
        const char* params[3] = {room.c_str(), before_id_str.c_str(), limit_str.c_str()};
        res.reset(PQexecParams(conn.get(), sql_with_before, 3, nullptr, params, nullptr, nullptr, 0));
    } else {
        const char* params[2] = {room.c_str(), limit_str.c_str()};
        res.reset(PQexecParams(conn.get(), sql_without_before, 2, nullptr, params, nullptr, nullptr, 0));
    }

    if (!tuples_ok(res.get())) {
        LOG_ERROR("PostgreSQL get_room_messages failed: {}", PQresultErrorMessage(res.get()));
        return messages;
    }

    int count = PQntuples(res.get());
    messages.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        StoredMessage msg;
        msg.id = parse_uint64_cell(res.get(), i, 0).value_or(0);
        msg.message_id = PQgetvalue(res.get(), i, 1);
        msg.sender_id = parse_uint64_cell(res.get(), i, 2).value_or(0);
        msg.room = PQgetvalue(res.get(), i, 3);
        msg.content = PQgetvalue(res.get(), i, 4);
        msg.created_at = parse_int64_cell(res.get(), i, 5).value_or(0);
        messages.push_back(std::move(msg));
    }

    std::reverse(messages.begin(), messages.end());
    return messages;
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

bool PostgresStorage::store_public_key(uint64_t user_id, const std::string& public_key) {
#ifdef ENABLE_POSTGRES
    auto conn = connect_or_throw(connection_string_);
    auto user_id_str = std::to_string(user_id);
    const char* params[2] = {user_id_str.c_str(), public_key.c_str()};

    PgResultPtr res(PQexecParams(
        conn.get(),
        "INSERT INTO user_keys (user_id, public_key, created_at, updated_at) "
        "VALUES ($1, $2, EXTRACT(EPOCH FROM NOW())::BIGINT, EXTRACT(EPOCH FROM NOW())::BIGINT) "
        "ON CONFLICT (user_id) DO UPDATE SET public_key = EXCLUDED.public_key, updated_at = EXCLUDED.updated_at",
        2,
        nullptr,
        params,
        nullptr,
        nullptr,
        0));

    if (!command_ok(res.get())) {
        LOG_ERROR("PostgreSQL store_public_key failed: {}", PQresultErrorMessage(res.get()));
        return false;
    }

    return true;
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

std::optional<std::string> PostgresStorage::get_public_key(uint64_t user_id) {
#ifdef ENABLE_POSTGRES
    auto conn = connect_or_throw(connection_string_);
    auto user_id_str = std::to_string(user_id);
    const char* params[1] = {user_id_str.c_str()};

    PgResultPtr res(PQexecParams(
        conn.get(),
        "SELECT public_key FROM user_keys WHERE user_id = $1",
        1,
        nullptr,
        params,
        nullptr,
        nullptr,
        0));

    if (!tuples_ok(res.get())) {
        LOG_ERROR("PostgreSQL get_public_key failed: {}", PQresultErrorMessage(res.get()));
        return std::nullopt;
    }
    if (PQntuples(res.get()) == 0 || PQgetisnull(res.get(), 0, 0)) {
        return std::nullopt;
    }

    return std::string(PQgetvalue(res.get(), 0, 0));
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

std::optional<std::string> PostgresStorage::get_public_key_by_username(const std::string& username) {
#ifdef ENABLE_POSTGRES
    auto conn = connect_or_throw(connection_string_);
    const char* params[1] = {username.c_str()};

    PgResultPtr res(PQexecParams(
        conn.get(),
        "SELECT uk.public_key FROM user_keys uk "
        "JOIN users u ON uk.user_id = u.id WHERE u.username = $1",
        1,
        nullptr,
        params,
        nullptr,
        nullptr,
        0));

    if (!tuples_ok(res.get())) {
        LOG_ERROR("PostgreSQL get_public_key_by_username failed: {}", PQresultErrorMessage(res.get()));
        return std::nullopt;
    }
    if (PQntuples(res.get()) == 0 || PQgetisnull(res.get(), 0, 0)) {
        return std::nullopt;
    }

    return std::string(PQgetvalue(res.get(), 0, 0));
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

bool PostgresStorage::is_healthy() {
#ifdef ENABLE_POSTGRES
    try {
        auto conn = connect_or_throw(connection_string_);
        PgResultPtr res(PQexec(conn.get(), "SELECT 1"));
        return tuples_ok(res.get()) && PQntuples(res.get()) == 1;
    } catch (...) {
        return false;
    }
#else
    return false;
#endif
}

void PostgresStorage::init_schema() {
#ifdef ENABLE_POSTGRES
    auto conn = connect_or_throw(connection_string_);

    const char* schema_sql = R"SQL(
        CREATE TABLE IF NOT EXISTS users (
            id BIGSERIAL PRIMARY KEY,
            username TEXT UNIQUE NOT NULL,
            password_hash TEXT NOT NULL,
            created_at BIGINT NOT NULL,
            last_login BIGINT,
            active BOOLEAN DEFAULT TRUE
        );

        CREATE TABLE IF NOT EXISTS rooms (
            id BIGSERIAL PRIMARY KEY,
            name TEXT UNIQUE NOT NULL,
            description TEXT,
            created_at BIGINT NOT NULL,
            is_private BOOLEAN DEFAULT FALSE
        );

        CREATE TABLE IF NOT EXISTS messages (
            id BIGSERIAL PRIMARY KEY,
            message_id TEXT NOT NULL,
            sender_id BIGINT NOT NULL REFERENCES users(id),
            room TEXT NOT NULL,
            content TEXT NOT NULL,
            created_at BIGINT NOT NULL
        );

        CREATE INDEX IF NOT EXISTS idx_messages_room ON messages(room, created_at);
        CREATE INDEX IF NOT EXISTS idx_users_username ON users(username);

        CREATE TABLE IF NOT EXISTS user_keys (
            user_id BIGINT PRIMARY KEY REFERENCES users(id),
            public_key TEXT NOT NULL,
            created_at BIGINT NOT NULL,
            updated_at BIGINT NOT NULL
        );
    )SQL";

    PgResultPtr schema_res(PQexec(conn.get(), schema_sql));
    if (!command_ok(schema_res.get())) {
        throw std::runtime_error("PostgreSQL schema initialization failed: " +
                                 std::string(PQresultErrorMessage(schema_res.get())));
    }

    auto now = std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    const char* params[2] = {"general", now.c_str()};
    PgResultPtr room_res(PQexecParams(
        conn.get(),
        "INSERT INTO rooms (name, description, created_at) VALUES ($1, 'General discussion', $2) "
        "ON CONFLICT (name) DO NOTHING",
        2,
        nullptr,
        params,
        nullptr,
        nullptr,
        0));

    if (!command_ok(room_res.get())) {
        throw std::runtime_error("PostgreSQL default room initialization failed: " +
                                 std::string(PQresultErrorMessage(room_res.get())));
    }

    LOG_INFO("PostgreSQL schema initialized");
#else
    throw std::runtime_error("PostgreSQL backend is disabled at build time.");
#endif
}

} // namespace chat::server::storage
