#include "message_store.hpp"
#include <ctime>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace chat::client {

void MessageStore::push(const protocol::Message& msg) {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_.push_back(msg);
}

size_t MessageStore::drain() {
    std::vector<protocol::Message> batch;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        batch.swap(pending_);
    }

    for (const auto& msg : batch) {
        display_items_.push_back(convert(msg));
    }

    // Trim to max size
    while (display_items_.size() > max_items_) {
        display_items_.pop_front();
    }

    return batch.size();
}

void MessageStore::push_local(DisplayItem::Kind kind, const std::string& content) {
    DisplayItem item;
    item.kind = kind;
    item.content = content;
    item.is_new = true;

    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    item.timestamp = format_timestamp(static_cast<int64_t>(time_t_now));

    display_items_.push_back(std::move(item));

    while (display_items_.size() > max_items_) {
        display_items_.pop_front();
    }
}

void MessageStore::mark_all_seen() {
    for (auto& item : display_items_) {
        item.is_new = false;
    }
}

DisplayItem MessageStore::convert(const protocol::Message& msg) const {
    DisplayItem item;
    item.original = msg;
    item.timestamp = format_timestamp(msg.timestamp);
    item.sender = msg.from;
    item.room = msg.room;
    item.is_new = true;

    switch (msg.type) {
        case protocol::MessageType::MESSAGE:
            item.kind = DisplayItem::Kind::CHAT;
            item.content = msg.payload.value("message", "");
            break;

        case protocol::MessageType::DIRECT_MESSAGE: {
            item.kind = DisplayItem::Kind::DM;
            std::string from = msg.payload.value("from", msg.from);
            std::string to = msg.payload.value("to", "");
            std::string message = msg.payload.value("message", "");
            bool sent = msg.payload.value("sent", false);
            bool is_encrypted = msg.payload.value("encrypted", false) ||
                                msg.payload.value("e2ee", false);
            item.encrypted = is_encrypted;
            if (sent) {
                item.content = "[DM -> " + to + "] " + message;
            } else {
                item.content = "[DM <- " + from + "] " + message;
                item.sender = from;
            }
            break;
        }

        case protocol::MessageType::SYSTEM: {
            item.kind = DisplayItem::Kind::SYSTEM;
            if (msg.payload.contains("message")) {
                item.content = msg.payload["message"].get<std::string>();
            } else if (msg.payload.contains("action")) {
                std::string action = msg.payload["action"].get<std::string>();
                if (action == "user_list") {
                    std::string text = "Online users";
                    if (msg.payload.contains("room") && !msg.payload["room"].get<std::string>().empty()) {
                        text += " in #" + msg.payload["room"].get<std::string>();
                    }
                    text += ": ";
                    if (msg.payload.contains("users")) {
                        bool first = true;
                        for (const auto& user : msg.payload["users"]) {
                            if (!first) text += ", ";
                            text += user.get<std::string>();
                            first = false;
                        }
                    }
                    text += " (" + std::to_string(msg.payload.value("count", 0)) + " total)";
                    item.content = text;
                } else if (action == "room_list") {
                    std::string text = "Available rooms:\n";
                    if (msg.payload.contains("rooms")) {
                        for (const auto& room : msg.payload["rooms"]) {
                            text += "  #" + room["name"].get<std::string>();
                            if (room.contains("users")) {
                                text += " (" + std::to_string(room["users"].get<int>()) + " users)";
                            }
                            if (room.contains("description") &&
                                !room["description"].get<std::string>().empty()) {
                                text += " - " + room["description"].get<std::string>();
                            }
                            text += "\n";
                        }
                    }
                    item.content = text;
                }
            }
            break;
        }

        case protocol::MessageType::ERROR:
            item.kind = DisplayItem::Kind::ERROR;
            item.content = msg.payload.value("message", "Unknown error");
            if (msg.payload.contains("code")) {
                item.content = "[" + msg.payload["code"].get<std::string>() + "] " + item.content;
            }
            break;

        case protocol::MessageType::PRESENCE: {
            item.kind = DisplayItem::Kind::PRESENCE;
            std::string action = msg.payload.value("action", "");
            std::string user = msg.payload.value("user", msg.from);
            item.sender = user;
            if (action == "online") {
                item.content = user + " is now online";
            } else if (action == "offline") {
                item.content = user + " has disconnected";
            } else if (action == "join") {
                item.content = user + " joined #" + msg.room;
            } else if (action == "leave") {
                item.content = user + " left #" + msg.room;
            } else if (action == "rename") {
                std::string old_name = msg.payload.value("old_name", "");
                std::string new_name = msg.payload.value("new_name", "");
                item.content = old_name + " is now known as " + new_name;
            } else {
                item.content = user + " " + action;
            }
            break;
        }

        case protocol::MessageType::AUTH_RESPONSE: {
            item.kind = DisplayItem::Kind::AUTH;
            bool success = msg.payload.value("success", false);
            std::string message = msg.payload.value("message", "");
            item.content = (success ? "✓ " : "✗ ") + message;
            break;
        }

        case protocol::MessageType::ROOM_EVENT: {
            item.kind = DisplayItem::Kind::ROOM_EVENT;
            std::string action = msg.payload.value("action", "");
            std::string room = msg.payload.value("room", msg.room);
            item.room = room;
            if (action == "joined") {
                std::string text = "Joined #" + room;
                if (msg.payload.contains("users")) {
                    text += " - Users: ";
                    bool first = true;
                    for (const auto& user : msg.payload["users"]) {
                        if (!first) text += ", ";
                        text += user.get<std::string>();
                        first = false;
                    }
                }
                item.content = text;
            } else if (action == "left") {
                item.content = "Left #" + room;
            }
            break;
        }

        default:
            item.kind = DisplayItem::Kind::SYSTEM;
            item.content = "[DEBUG] " + nlohmann::json(msg).dump();
            break;
    }

    return item;
}

std::string MessageStore::format_timestamp(int64_t timestamp) const {
    time_t time = static_cast<time_t>(timestamp);
    struct tm* tm_info = localtime(&time);
    char buffer[16];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", tm_info);
    return buffer;
}

} // namespace chat::client
