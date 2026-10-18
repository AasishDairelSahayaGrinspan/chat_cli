#pragma once

#include "tls_client.hpp"
#include "crypto.hpp"
#include "group_manager.hpp"
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <sstream>
#include <vector>

namespace chat::client {

class InputHandler {
public:
    using CommandCallback = std::function<void(const std::string& cmd,
                                               const std::vector<std::string>& args)>;
    using ChatCallback = std::function<void(const std::string& message)>;
    using QuitCallback = std::function<void()>;
    using ErrorCallback = std::function<void(const std::string&)>;
    using StatusCallback = std::function<void(const std::string&)>;

    InputHandler(TlsClient& client, KeyManager& key_manager, GroupManager& group_manager);

    // Process a line of input
    void process_line(const std::string& line);

    // Set callbacks
    void set_quit_callback(QuitCallback cb) { quit_callback_ = std::move(cb); }
    void set_error_callback(ErrorCallback cb) { error_callback_ = std::move(cb); }
    void set_status_callback(StatusCallback cb) { status_callback_ = std::move(cb); }

    // Get current room
    const std::string& current_room() const { return current_room_; }
    void set_current_room(const std::string& room) { current_room_ = room; }

    // State
    bool is_authenticated() const { return authenticated_; }
    void set_authenticated(bool auth) { authenticated_ = auth; }

    const std::string& username() const { return username_; }
    void set_username(const std::string& name) { username_ = name; }

    // Available commands for tab completion
    static const std::vector<std::string>& available_commands();

    // Sent-DM echo handling: remember what we sent so the server echo (which
    // carries the ciphertext) can be displayed as the original plaintext.
    std::optional<std::string> take_sent_dm_plaintext(const std::string& ciphertext);

private:
    void handle_command(const std::string& cmd, const std::vector<std::string>& args);
    std::vector<std::string> split_args(const std::string& input);
    void report_error(const std::string& error);
    void report_status(const std::string& status);

    // Send a chat message to the current room, end-to-end encrypted.
    void send_room_message(const std::string& text);

    // /verify and /roomkeys implementations (client-side only).
    void handle_verify(const std::vector<std::string>& args);
    void handle_roomkeys(const std::vector<std::string>& args);

    void remember_sent_dm(const std::string& ciphertext, const std::string& plaintext);

    TlsClient& client_;
    KeyManager& key_manager_;
    GroupManager& group_manager_;

    std::string current_room_;
    std::string username_;
    bool authenticated_ = false;

    QuitCallback quit_callback_;
    ErrorCallback error_callback_;
    StatusCallback status_callback_;

    // ciphertext -> plaintext for recently sent encrypted DMs (network thread
    // reads it, UI thread writes it).
    mutable std::mutex dm_echo_mutex_;
    std::map<std::string, std::string> sent_dm_plaintext_;
    std::deque<std::string> sent_dm_order_;
};

} // namespace chat::client
