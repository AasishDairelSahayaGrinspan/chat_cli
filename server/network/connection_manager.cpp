#include "connection_manager.hpp"
#include "logging/logger.hpp"

namespace chat::server {

void ConnectionManager::add(Connection::Ptr conn) {
    std::unique_lock lock(mutex_);
    connections_[conn->id()] = conn;
    LOG_DEBUG("Added connection {} (total: {})", conn->id(), connections_.size());
}

void ConnectionManager::remove(Connection::Ptr conn) {
    remove(conn->id());
}

void ConnectionManager::remove(uint64_t conn_id) {
    std::unique_lock lock(mutex_);

    auto it = connections_.find(conn_id);
    if (it != connections_.end()) {
        // Remove from username index
        if (!it->second->username().empty()) {
            username_index_.erase(it->second->username());
        }

        // Remove from all rooms
        auto room_it = conn_rooms_.find(conn_id);
        if (room_it != conn_rooms_.end()) {
            for (const auto& room : room_it->second) {
                auto& members = rooms_[room];
                members.erase(conn_id);
                if (members.empty()) {
                    rooms_.erase(room);
                }
            }
            conn_rooms_.erase(room_it);
        }

        connections_.erase(it);
        LOG_DEBUG("Removed connection {} (total: {})", conn_id, connections_.size());
    }
}

Connection::Ptr ConnectionManager::get(uint64_t conn_id) {
    std::shared_lock lock(mutex_);
    auto it = connections_.find(conn_id);
    return it != connections_.end() ? it->second : nullptr;
}

Connection::Ptr ConnectionManager::get_by_username(const std::string& username) {
    std::shared_lock lock(mutex_);
    auto it = username_index_.find(username);
    return it != username_index_.end() ? it->second : nullptr;
}

void ConnectionManager::register_username(const std::string& username, Connection::Ptr conn) {
    std::unique_lock lock(mutex_);
    username_index_[username] = conn;
    LOG_DEBUG("Registered username '{}' for connection {}", username, conn->id());
}

void ConnectionManager::unregister_username(const std::string& username) {
    std::unique_lock lock(mutex_);
    username_index_.erase(username);
    LOG_DEBUG("Unregistered username '{}'", username);
}

void ConnectionManager::broadcast(const protocol::Message& msg) {
    std::vector<Connection::Ptr> conns;
    {
        std::shared_lock lock(mutex_);
        conns.reserve(connections_.size());
        for (const auto& [id, conn] : connections_) {
            if (conn->is_authenticated()) {
                conns.push_back(conn);
            }
        }
    }

    for (auto& conn : conns) {
        conn->send(msg);
    }
}

void ConnectionManager::broadcast_to_room(const std::string& room, const protocol::Message& msg) {
    std::vector<Connection::Ptr> conns;
    {
        std::shared_lock lock(mutex_);
        auto it = rooms_.find(room);
        if (it != rooms_.end()) {
            for (uint64_t conn_id : it->second) {
                auto conn_it = connections_.find(conn_id);
                if (conn_it != connections_.end()) {
                    conns.push_back(conn_it->second);
                }
            }
        }
    }

    for (auto& conn : conns) {
        conn->send(msg);
    }
}

void ConnectionManager::broadcast_except(uint64_t exclude_id, const protocol::Message& msg) {
    std::vector<Connection::Ptr> conns;
    {
        std::shared_lock lock(mutex_);
        conns.reserve(connections_.size());
        for (const auto& [id, conn] : connections_) {
            if (id != exclude_id && conn->is_authenticated()) {
                conns.push_back(conn);
            }
        }
    }

    for (auto& conn : conns) {
        conn->send(msg);
    }
}

void ConnectionManager::for_each(ConnectionIterator fn) {
    std::vector<Connection::Ptr> conns;
    {
        std::shared_lock lock(mutex_);
        conns.reserve(connections_.size());
        for (const auto& [id, conn] : connections_) {
            conns.push_back(conn);
        }
    }

    for (auto& conn : conns) {
        fn(conn);
    }
}

size_t ConnectionManager::count() const {
    std::shared_lock lock(mutex_);
    return connections_.size();
}

size_t ConnectionManager::count_by_ip(const std::string& ip) const {
    std::shared_lock lock(mutex_);
    size_t count = 0;
    for (const auto& [id, conn] : connections_) {
        if (conn->remote_address() == ip) {
            ++count;
        }
    }
    return count;
}

size_t ConnectionManager::authenticated_count() const {
    std::shared_lock lock(mutex_);
    size_t count = 0;
    for (const auto& [id, conn] : connections_) {
        if (conn->is_authenticated()) {
            ++count;
        }
    }
    return count;
}

void ConnectionManager::close_all() {
    std::vector<Connection::Ptr> conns;
    {
        std::shared_lock lock(mutex_);
        conns.reserve(connections_.size());
        for (const auto& [id, conn] : connections_) {
            conns.push_back(conn);
        }
    }

    for (auto& conn : conns) {
        conn->close();
    }
}

void ConnectionManager::add_to_room(uint64_t conn_id, const std::string& room) {
    std::unique_lock lock(mutex_);
    rooms_[room].insert(conn_id);
    conn_rooms_[conn_id].insert(room);
    LOG_DEBUG("Connection {} joined room '{}'", conn_id, room);
}


void ConnectionManager::remove_from_room(uint64_t conn_id, const std::string& room) {
    std::unique_lock lock(mutex_);

    auto& members = rooms_[room];
    members.erase(conn_id);
    if (members.empty()) {
        rooms_.erase(room);
    }

    auto it = conn_rooms_.find(conn_id);
    if (it != conn_rooms_.end()) {
        it->second.erase(room);
    }

    LOG_DEBUG("Connection {} left room '{}'", conn_id, room);
}

void ConnectionManager::remove_from_all_rooms(uint64_t conn_id) {
    std::unique_lock lock(mutex_);

    auto it = conn_rooms_.find(conn_id);
    if (it != conn_rooms_.end()) {
        for (const auto& room : it->second) {
            auto& members = rooms_[room];
            members.erase(conn_id);
            if (members.empty()) {
                rooms_.erase(room);
            }
        }
        conn_rooms_.erase(it);
    }
}

std::vector<std::string> ConnectionManager::get_rooms(uint64_t conn_id) {
    std::shared_lock lock(mutex_);
    std::vector<std::string> result;

    auto it = conn_rooms_.find(conn_id);
    if (it != conn_rooms_.end()) {
        result.assign(it->second.begin(), it->second.end());
    }

    return result;
}

std::vector<Connection::Ptr> ConnectionManager::get_room_members(const std::string& room) {
    std::shared_lock lock(mutex_);
    std::vector<Connection::Ptr> result;

    auto it = rooms_.find(room);
    if (it != rooms_.end()) {
        for (uint64_t conn_id : it->second) {
            auto conn_it = connections_.find(conn_id);
            if (conn_it != connections_.end()) {
                result.push_back(conn_it->second);
            }
        }
    }

    return result;
}

} // namespace chat::server

