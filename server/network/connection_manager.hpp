#pragma once

#include "connection.hpp"
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <functional>

namespace chat::server {

class ConnectionManager {
public:
    using ConnectionIterator = std::function<void(Connection::Ptr)>;

    void add(Connection::Ptr conn);
    void remove(Connection::Ptr conn);
    void remove(uint64_t conn_id);

    Connection::Ptr get(uint64_t conn_id);
    Connection::Ptr get_by_username(const std::string& username);

    // Register/unregister username mapping (call on login/logout)
    void register_username(const std::string& username, Connection::Ptr conn);
    void unregister_username(const std::string& username);

    void broadcast(const protocol::Message& msg);
    void broadcast_to_room(const std::string& room, const protocol::Message& msg);
    void broadcast_except(uint64_t exclude_id, const protocol::Message& msg);

    void for_each(ConnectionIterator fn);

    size_t count() const;
    size_t authenticated_count() const;

    void close_all();


    // Room management helpers
    void add_to_room(uint64_t conn_id, const std::string& room);
    void remove_from_room(uint64_t conn_id, const std::string& room);
    void remove_from_all_rooms(uint64_t conn_id);
    std::vector<std::string> get_rooms(uint64_t conn_id);
    std::vector<Connection::Ptr> get_room_members(const std::string& room);

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<uint64_t, Connection::Ptr> connections_;
    std::unordered_map<std::string, Connection::Ptr> username_index_;

    // Room membership: room_name -> set of connection IDs
    std::unordered_map<std::string, std::unordered_set<uint64_t>> rooms_;
    // Connection -> rooms mapping
    std::unordered_map<uint64_t, std::unordered_set<std::string>> conn_rooms_;
};

} // namespace chat::server

