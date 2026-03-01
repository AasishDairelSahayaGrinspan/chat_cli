#pragma once

#include "protocol/message.hpp"
#include <string>
#include <iostream>
#include <iomanip>
#include <ctime>

namespace chat::client {

class UiRenderer {
public:
    // Render a message to the console
    void render(const protocol::Message& msg);

    // Print status line
    void print_status(const std::string& status);

    // Print error
    void print_error(const std::string& error);

    // Print prompt
    void print_prompt(const std::string& username, const std::string& room);

    // Clear current line
    void clear_line();

    // Enable/disable colors
    void set_colors(bool enabled) { colors_enabled_ = enabled; }

private:
    std::string format_timestamp(int64_t timestamp);
    std::string color(const std::string& code);

    static constexpr const char* RESET = "\033[0m";
    static constexpr const char* BOLD = "\033[1m";
    static constexpr const char* RED = "\033[31m";
    static constexpr const char* GREEN = "\033[32m";
    static constexpr const char* YELLOW = "\033[33m";
    static constexpr const char* BLUE = "\033[34m";
    static constexpr const char* MAGENTA = "\033[35m";
    static constexpr const char* CYAN = "\033[36m";
    static constexpr const char* GRAY = "\033[90m";

    bool colors_enabled_ = true;
};

} // namespace chat::client

