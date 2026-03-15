#pragma once

#include <string>
#include <algorithm>
#include <regex>

namespace chat::sanitize {

// Strip ANSI escape sequences and control characters from message content
inline std::string sanitize_message(const std::string& input) {
    if (input.empty()) return input;

    // Remove ANSI escape sequences: ESC[ ... letter and ESC] ... BEL
    static const std::regex ansi_pattern(
        "\\x1B\\[[0-9;]*[A-Za-z]"  // CSI sequences
        "|\\x1B\\][^\x07]*\x07"     // OSC sequences
        "|\\x1B[\\(\\)][AB012]"     // Character set selection
        "|\\x1B\\[[0-9;]*[ -/]*[@-~]"  // Extended CSI
    );

    std::string cleaned = std::regex_replace(input, ansi_pattern, "");

    // Remove control characters (0x00-0x1F except \t and \n, and 0x7F)
    std::string result;
    result.reserve(cleaned.size());

    for (unsigned char c : cleaned) {
        if (c == '\t' || c == '\n' || (c >= 0x20 && c != 0x7F)) {
            result += static_cast<char>(c);
        }
    }

    return result;
}

} // namespace chat::sanitize
