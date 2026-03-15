#include "session_manager.hpp"
#include "logging/logger.hpp"

namespace chat::server {

SessionManager::SessionManager()
    : rate_limiter_() {
    // Uses default rate limiter config
}

SessionManager::SessionManager(const DualRateLimiter::Config& rl_config)
    : rate_limiter_(rl_config) {
}

Session& SessionManager::create(uint64_t conn_id, const std::string& remote_ip) {
    std::unique_lock lock(mutex_);

    auto& session = sessions_[conn_id];
    session.connection_id = conn_id;
    session.remote_ip = remote_ip;
    session.connected_at = std::chrono::steady_clock::now();
    session.last_activity = session.connected_at;

    LOG_DEBUG("Created session for connection {} from {}", conn_id, remote_ip);
    return session;
}

Session* SessionManager::get(uint64_t conn_id) {
    std::shared_lock lock(mutex_);
    auto it = sessions_.find(conn_id);
    return it != sessions_.end() ? &it->second : nullptr;
}

Session* SessionManager::get_by_username(const std::string& username) {
    std::shared_lock lock(mutex_);
    auto it = username_index_.find(username);
    if (it == username_index_.end()) {
        return nullptr;
    }

    auto session_it = sessions_.find(it->second);
    return session_it != sessions_.end() ? &session_it->second : nullptr;
}

bool SessionManager::authenticate(uint64_t conn_id, uint64_t user_id, const std::string& username) {
    std::unique_lock lock(mutex_);

    auto it = sessions_.find(conn_id);
    if (it == sessions_.end()) {
        return false;
    }

    // Check if username is already taken by another session
    auto existing = username_index_.find(username);
    if (existing != username_index_.end() && existing->second != conn_id) {
        // Check if the existing session is actually still alive
        auto existing_session = sessions_.find(existing->second);
        if (existing_session != sessions_.end()) {
            auto inactive_time = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - existing_session->second.last_activity);

            // If the old session has been inactive for more than 30 seconds,
            // consider it stale and allow the new login
            if (inactive_time.count() > 30) {
                LOG_WARN("Removing stale session {} for username '{}' (inactive {}s)",
                         existing->second, username, inactive_time.count());
                // Clean up stale session
                rate_limiter_.remove_user(username);
                rate_limiter_.remove_ip(existing_session->second.remote_ip);
                sessions_.erase(existing_session);
                username_index_.erase(existing);
            } else {
                LOG_WARN("Username '{}' already logged in on connection {}", username, existing->second);
                return false;
            }
        } else {
            // Session doesn't exist but index entry remains — clean it up
            username_index_.erase(existing);
        }
    }

    it->second.user_id = user_id;
    it->second.username = username;
    it->second.touch();

    username_index_[username] = conn_id;

    LOG_INFO("Session {} authenticated as user '{}' (id: {})", conn_id, username, user_id);
    return true;
}

void SessionManager::remove(uint64_t conn_id) {
    std::unique_lock lock(mutex_);

    auto it = sessions_.find(conn_id);
    if (it != sessions_.end()) {
        if (!it->second.username.empty()) {
            username_index_.erase(it->second.username);
            rate_limiter_.remove_user(it->second.username);
        }
        rate_limiter_.remove_ip(it->second.remote_ip);

        LOG_DEBUG("Removed session for connection {}", conn_id);
        sessions_.erase(it);
    }
}

bool SessionManager::check_rate_limit(uint64_t conn_id) {
    std::shared_lock lock(mutex_);

    auto it = sessions_.find(conn_id);
    if (it == sessions_.end()) {
        return false;
    }

    const auto& session = it->second;
    return rate_limiter_.check(session.remote_ip, session.username);
}

void SessionManager::for_each_authenticated(SessionIterator fn) {
    std::shared_lock lock(mutex_);
    for (const auto& [id, session] : sessions_) {
        if (session.is_authenticated()) {
            fn(session);
        }
    }
}

std::vector<std::string> SessionManager::get_online_users() {
    std::shared_lock lock(mutex_);
    std::vector<std::string> users;
    users.reserve(username_index_.size());

    for (const auto& [username, conn_id] : username_index_) {
        users.push_back(username);
    }

    return users;
}

size_t SessionManager::count() const {
    std::shared_lock lock(mutex_);
    return sessions_.size();
}

size_t SessionManager::authenticated_count() const {
    std::shared_lock lock(mutex_);
    size_t count = 0;
    for (const auto& [id, session] : sessions_) {
        if (session.is_authenticated()) {
            ++count;
        }
    }
    return count;
}

void SessionManager::cleanup_inactive(std::chrono::seconds timeout) {
    std::unique_lock lock(mutex_);
    auto now = std::chrono::steady_clock::now();

    for (auto it = sessions_.begin(); it != sessions_.end();) {
        auto inactive_time = std::chrono::duration_cast<std::chrono::seconds>(
            now - it->second.last_activity);

        if (inactive_time > timeout) {
            if (!it->second.username.empty()) {
                username_index_.erase(it->second.username);
                rate_limiter_.remove_user(it->second.username);
            }
            rate_limiter_.remove_ip(it->second.remote_ip);
            LOG_INFO("Removing inactive session {} (user: '{}', inactive: {}s)",
                     it->second.connection_id,
                     it->second.username.empty() ? "anonymous" : it->second.username,
                     inactive_time.count());
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace chat::server

