#include "room_manager.hpp"
#include "logging/logger.hpp"

namespace chat::server {

RoomManager::RoomManager(ConnectionManager& conn_manager, storage::IStorage& storage)
    : conn_manager_(conn_manager), storage_(storage) {}

bool RoomManager::join(Connection::Ptr conn, const std::string& room) {
    if (!conn->is_authenticated()) {
        return false;
    }

    // Create room in storage if needed
    if (!storage_.get_room(room).has_value()) {
        storage_.create_room(room);
    }

    conn_manager_.add_to_room(conn->id(), room);
    set_current_room(conn->id(), room);

    // Notify room members
    auto msg = protocol::Message::create(protocol::MessageType::PRESENCE, conn->username(), room);
    msg.payload = {
        {"action", "join"},
        {"user", conn->username()}
    };
    broadcast_except(room, conn->id(), msg);

    // Rotate room keys: the new member must not be able to read older
    // epochs, so everyone regenerates sender keys on membership change.
    broadcast_membership(room, "join", conn->username());

    LOG_INFO("User '{}' joined room '{}'", conn->username(), room);
    return true;
}

bool RoomManager::leave(Connection::Ptr conn, const std::string& room) {
    if (!conn->is_authenticated()) {
        return false;
    }

    conn_manager_.remove_from_room(conn->id(), room);

    // Update current room if leaving current
    {
        std::unique_lock lock(mutex_);
        if (current_rooms_[conn->id()] == room) {
            auto rooms = conn_manager_.get_rooms(conn->id());
            current_rooms_[conn->id()] = rooms.empty() ? "" : rooms.back();
        }
    }

    // Notify room members
    auto msg = protocol::Message::create(protocol::MessageType::PRESENCE, conn->username(), room);
    msg.payload = {
        {"action", "leave"},
        {"user", conn->username()}
    };
    broadcast(room, msg);

    // Rotate room keys: the leaving member must not be able to read
    // subsequent epochs.
    broadcast_membership(room, "leave", conn->username());

    LOG_INFO("User '{}' left room '{}'", conn->username(), room);
    return true;
}

void RoomManager::leave_all(Connection::Ptr conn) {
    auto rooms = conn_manager_.get_rooms(conn->id());
    conn_manager_.remove_from_all_rooms(conn->id());

    for (const auto& room : rooms) {
        // Notify room members (the leaving connection is already removed)
        auto msg = protocol::Message::create(protocol::MessageType::PRESENCE, conn->username(), room);
        msg.payload = {
            {"action", "leave"},
            {"user", conn->username()}
        };
        broadcast(room, msg);

        // Rotate keys for the remaining members.
        broadcast_membership(room, "leave", conn->username());
    }

    {
        std::unique_lock lock(mutex_);
        current_rooms_.erase(conn->id());
    }
}

void RoomManager::broadcast(const std::string& room, const protocol::Message& msg) {
    conn_manager_.broadcast_to_room(room, msg);
}

void RoomManager::broadcast_except(const std::string& room, uint64_t exclude_id,
                                   const protocol::Message& msg) {
    auto members = conn_manager_.get_room_members(room);
    for (auto& conn : members) {
        if (conn->id() != exclude_id) {
            conn->send(msg);
        }
    }
}

std::vector<std::string> RoomManager::get_rooms(uint64_t conn_id) {
    return conn_manager_.get_rooms(conn_id);
}

std::vector<std::string> RoomManager::get_room_users(const std::string& room) {
    std::vector<std::string> users;
    auto members = conn_manager_.get_room_members(room);
    users.reserve(members.size());

    for (const auto& conn : members) {
        if (conn->is_authenticated()) {
            users.push_back(conn->username());
        }
    }

    return users;
}

bool RoomManager::room_exists(const std::string& room) {
    return storage_.get_room(room).has_value();
}

bool RoomManager::create_room(const std::string& room) {
    return storage_.create_room(room);
}

std::string RoomManager::get_current_room(uint64_t conn_id) {
    std::shared_lock lock(mutex_);
    auto it = current_rooms_.find(conn_id);
    return it != current_rooms_.end() ? it->second : "";
}

void RoomManager::set_current_room(uint64_t conn_id, const std::string& room) {
    std::unique_lock lock(mutex_);
    current_rooms_[conn_id] = room;
}

void RoomManager::broadcast_membership(const std::string& room,
                                       const std::string& action,
                                       const std::string& user) {
    uint64_t epoch = storage_.bump_room_epoch(room);
    if (epoch == 0) {
        LOG_WARN("Failed to bump room epoch for '{}'", room);
        return;
    }

    auto msg = protocol::Message::create(protocol::MessageType::ROOM_MEMBERSHIP, user, room);
    msg.payload = {
        {"action", action},
        {"user", user},
        {"room", room},
        {"epoch", epoch},
        {"members", get_room_users(room)}
    };
    broadcast(room, msg);

    LOG_INFO("Room '{}' epoch rotated to {} ({} by {})", room, epoch, action, user);
}

} // namespace chat::server

