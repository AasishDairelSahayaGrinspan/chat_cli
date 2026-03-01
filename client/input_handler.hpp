#pragma once

#include "tls_client.hpp"
#include <functional>
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

    InputHandler(TlsClient& client);

    // Process a line of input
    void process_line(const std::string& line);

    // Set callbacks
    void set_quit_callback(QuitCallback cb) { quit_callback_ = std::move(cb); }

    // Get current room
    const std::string& current_room() const { return current_room_; }
    void set_current_room(const std::string& room) { current_room_ = room; }

    // State
    bool is_authenticated() const { return authenticated_; }
    void set_authenticated(bool auth) { authenticated_ = auth; }

    const std::string& username() const { return username_; }
    void set_username(const std::string& name) { username_ = name; }

private:
    void handle_command(const std::string& cmd, const std::vector<std::string>& args);
    std::vector<std::string> split_args(const std::string& input);

    TlsClient& client_;

    std::string current_room_;
    std::string username_;
    bool authenticated_ = false;

    QuitCallback quit_callback_;
};

} // namespace chat::client

