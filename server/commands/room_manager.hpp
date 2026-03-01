#pragma once

#include "network/connection.hpp"
#include "network/connection_manager.hpp"
#include "session/session_manager.hpp"
#include "storage/storage_interface.hpp"
#include <unordered_map>
#include <unordered_set>
#include <shared_mutex>
#include <string>

namespace chat::server {

class RoomManager {
public:
    explicit RoomManager(ConnectionManager& conn_manager, storage::IStorage& storage);

    // Join a room
    bool join(Connection::Ptr conn, const std::string& room);

    // Leave a room
    bool leave(Connection::Ptr conn, const std::string& room);

    // Leave all rooms
    void leave_all(Connection::Ptr conn);

    // Broadcast to room
    void broadcast(const std::string& room, const protocol::Message& msg);

    // Broadcast to room except sender
    void broadcast_except(const std::string& room, uint64_t exclude_id, const protocol::Message& msg);

    // Get rooms for a connection
    std::vector<std::string> get_rooms(uint64_t conn_id);

    // Get users in a room
    std::vector<std::string> get_room_users(const std::string& room);

    // Check if room exists
    bool room_exists(const std::string& room);

    // Create room if it doesn't exist
    bool create_room(const std::string& room);

    // Get current room for connection (last joined)
    std::string get_current_room(uint64_t conn_id);

    // Set current room
    void set_current_room(uint64_t conn_id, const std::string& room);

private:
    ConnectionManager& conn_manager_;
    storage::IStorage& storage_;

    mutable std::shared_mutex mutex_;
    std::unordered_map<uint64_t, std::string> current_rooms_;  // conn_id -> current room
};

} // namespace chat::server

