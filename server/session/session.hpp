#pragma once

#include <string>
#include <unordered_set>
#include <chrono>
#include <cstdint>

namespace chat::server {

struct Session {
    uint64_t connection_id = 0;
    uint64_t user_id = 0;
    std::string username;
    std::string remote_ip;
    std::unordered_set<std::string> rooms;
    std::chrono::steady_clock::time_point connected_at;
    std::chrono::steady_clock::time_point last_activity;

    bool is_authenticated() const { return user_id != 0; }

    void touch() {
        last_activity = std::chrono::steady_clock::now();
    }
};

} // namespace chat::server

