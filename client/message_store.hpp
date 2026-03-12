#pragma once

#include "protocol/message.hpp"
#include <mutex>
#include <deque>
#include <vector>
#include <string>
#include <chrono>

namespace chat::client {

struct DisplayItem {
    enum class Kind { CHAT, SYSTEM, ERROR, STATUS, PRESENCE, DM, ROOM_EVENT, AUTH };
    Kind kind;
    std::string timestamp;
    std::string sender;
    std::string room;
    std::string content;
    bool is_new = true;  // For fade-in animation
    protocol::Message original;
};

class MessageStore {
public:
    // Called from network thread — pushes a message
    void push(const protocol::Message& msg);

    // Called from UI thread — drains all pending messages into display buffer
    // Returns count of new items added
    size_t drain();

    // Called from UI thread — read-only access to display history
    const std::deque<DisplayItem>& items() const { return display_items_; }

    // Max items to keep in display buffer (scroll-back limit)
    void set_max_items(size_t n) { max_items_ = n; }

    // Push a local status/error message (from UI thread)
    void push_local(DisplayItem::Kind kind, const std::string& content);

    // Mark all items as no longer new (for fade-in animation)
    void mark_all_seen();

private:
    DisplayItem convert(const protocol::Message& msg) const;
    std::string format_timestamp(int64_t timestamp) const;

    std::mutex pending_mutex_;
    std::vector<protocol::Message> pending_;
    std::deque<DisplayItem> display_items_;
    size_t max_items_ = 1000;
};

} // namespace chat::client
