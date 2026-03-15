#pragma once

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <string>
#include <optional>
#include <stdexcept>

namespace chat::config {

struct ServerConfig {
    std::string bind_address = "0.0.0.0";
    uint16_t port = 8443;
    uint16_t health_port = 8080;
    size_t thread_pool_size = 4;

    // TLS
    std::string cert_file = "certs/server.crt";
    std::string key_file = "certs/server.key";
    std::string dh_file = "certs/dh2048.pem";

    // Storage
    std::string storage_type = "sqlite";  // sqlite or postgres
    std::string sqlite_path = "chat.db";
    std::string postgres_conn = "";

    // Redis (optional)
    bool redis_enabled = false;
    std::string redis_host = "localhost";
    uint16_t redis_port = 6379;

    // Rate limiting
    size_t rate_limit_messages_per_second = 10;
    size_t rate_limit_burst = 20;

    // Connection limits
    size_t max_connections = 1000;
    size_t max_connections_per_ip = 5;

    // Session management
    size_t session_timeout_seconds = 1800;  // 30 minutes

    // Logging
    std::string log_level = "info";
    std::string log_file = "";
};

struct ClientConfig {
    std::string server_host = "localhost";
    uint16_t server_port = 8443;
    bool verify_ssl = true;  // Enabled by default; use --insecure to disable

    // Reconnection
    size_t reconnect_delay_ms = 1000;
    size_t max_reconnect_attempts = 10;

    // Logging
    std::string log_level = "info";
};

inline void to_json(nlohmann::json& j, const ServerConfig& c) {
    j = nlohmann::json{
        {"bind_address", c.bind_address},
        {"port", c.port},
        {"health_port", c.health_port},
        {"thread_pool_size", c.thread_pool_size},
        {"cert_file", c.cert_file},
        {"key_file", c.key_file},
        {"dh_file", c.dh_file},
        {"storage_type", c.storage_type},
        {"sqlite_path", c.sqlite_path},
        {"postgres_conn", c.postgres_conn},
        {"redis_enabled", c.redis_enabled},
        {"redis_host", c.redis_host},
        {"redis_port", c.redis_port},
        {"rate_limit_messages_per_second", c.rate_limit_messages_per_second},
        {"rate_limit_burst", c.rate_limit_burst},
        {"max_connections", c.max_connections},
        {"max_connections_per_ip", c.max_connections_per_ip},
        {"session_timeout_seconds", c.session_timeout_seconds},
        {"log_level", c.log_level},
        {"log_file", c.log_file}
    };
}

inline void from_json(const nlohmann::json& j, ServerConfig& c) {
    if (j.contains("bind_address")) j.at("bind_address").get_to(c.bind_address);
    if (j.contains("port")) j.at("port").get_to(c.port);
    if (j.contains("health_port")) j.at("health_port").get_to(c.health_port);
    if (j.contains("thread_pool_size")) j.at("thread_pool_size").get_to(c.thread_pool_size);
    if (j.contains("cert_file")) j.at("cert_file").get_to(c.cert_file);
    if (j.contains("key_file")) j.at("key_file").get_to(c.key_file);
    if (j.contains("dh_file")) j.at("dh_file").get_to(c.dh_file);
    if (j.contains("storage_type")) j.at("storage_type").get_to(c.storage_type);
    if (j.contains("sqlite_path")) j.at("sqlite_path").get_to(c.sqlite_path);
    if (j.contains("postgres_conn")) j.at("postgres_conn").get_to(c.postgres_conn);
    if (j.contains("redis_enabled")) j.at("redis_enabled").get_to(c.redis_enabled);
    if (j.contains("redis_host")) j.at("redis_host").get_to(c.redis_host);
    if (j.contains("redis_port")) j.at("redis_port").get_to(c.redis_port);
    if (j.contains("rate_limit_messages_per_second"))
        j.at("rate_limit_messages_per_second").get_to(c.rate_limit_messages_per_second);
    if (j.contains("rate_limit_burst")) j.at("rate_limit_burst").get_to(c.rate_limit_burst);
    if (j.contains("max_connections")) j.at("max_connections").get_to(c.max_connections);
    if (j.contains("max_connections_per_ip")) j.at("max_connections_per_ip").get_to(c.max_connections_per_ip);
    if (j.contains("session_timeout_seconds"))
        j.at("session_timeout_seconds").get_to(c.session_timeout_seconds);
    if (j.contains("log_level")) j.at("log_level").get_to(c.log_level);
    if (j.contains("log_file")) j.at("log_file").get_to(c.log_file);
}

inline void to_json(nlohmann::json& j, const ClientConfig& c) {
    j = nlohmann::json{
        {"server_host", c.server_host},
        {"server_port", c.server_port},
        {"verify_ssl", c.verify_ssl},
        {"reconnect_delay_ms", c.reconnect_delay_ms},
        {"max_reconnect_attempts", c.max_reconnect_attempts},
        {"log_level", c.log_level}
    };
}

inline void from_json(const nlohmann::json& j, ClientConfig& c) {
    if (j.contains("server_host")) j.at("server_host").get_to(c.server_host);
    if (j.contains("server_port")) j.at("server_port").get_to(c.server_port);
    if (j.contains("verify_ssl")) j.at("verify_ssl").get_to(c.verify_ssl);
    if (j.contains("reconnect_delay_ms")) j.at("reconnect_delay_ms").get_to(c.reconnect_delay_ms);
    if (j.contains("max_reconnect_attempts")) j.at("max_reconnect_attempts").get_to(c.max_reconnect_attempts);
    if (j.contains("log_level")) j.at("log_level").get_to(c.log_level);
}

template<typename T>
T load_config(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file: " + path);
    }

    nlohmann::json j;
    file >> j;
    return j.get<T>();
}

template<typename T>
T load_config_or_default(const std::string& path) {
    try {
        return load_config<T>(path);
    } catch (...) {
        return T{};
    }
}

inline spdlog::level::level_enum parse_log_level(const std::string& level) {
    if (level == "trace") return spdlog::level::trace;
    if (level == "debug") return spdlog::level::debug;
    if (level == "info") return spdlog::level::info;
    if (level == "warn" || level == "warning") return spdlog::level::warn;
    if (level == "error" || level == "err") return spdlog::level::err;
    if (level == "critical") return spdlog::level::critical;
    if (level == "off") return spdlog::level::off;
    return spdlog::level::info;
}

} // namespace chat::config

