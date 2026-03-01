#pragma once

#include "session.hpp"
#include "rate_limiter.hpp"
#include <unordered_map>
#include <shared_mutex>
#include <memory>
#include <functional>

namespace chat::server {

class SessionManager {
public:
    using SessionIterator = std::function<void(const Session&)>;

    SessionManager();

    // Create session for new connection
    Session& create(uint64_t conn_id, const std::string& remote_ip);

    // Get session by connection ID
    Session* get(uint64_t conn_id);

    // Get session by username
    Session* get_by_username(const std::string& username);

    // Authenticate session
    bool authenticate(uint64_t conn_id, uint64_t user_id, const std::string& username);

    // Remove session
    void remove(uint64_t conn_id);

    // Check rate limit for connection
    bool check_rate_limit(uint64_t conn_id);

    // Iterate over authenticated sessions
    void for_each_authenticated(SessionIterator fn);

    // Get all authenticated usernames
    std::vector<std::string> get_online_users();

    // Get count
    size_t count() const;
    size_t authenticated_count() const;

    // Cleanup inactive sessions
    void cleanup_inactive(std::chrono::seconds timeout);

    DualRateLimiter& rate_limiter() { return rate_limiter_; }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<uint64_t, Session> sessions_;
    std::unordered_map<std::string, uint64_t> username_index_;

    DualRateLimiter rate_limiter_;
};

} // namespace chat::server

