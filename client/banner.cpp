#include "banner.hpp"
#include <chrono>
#include <algorithm>

namespace chat::client {

static auto start_time = std::chrono::steady_clock::now();

const std::vector<std::string>& Banner::get_banner_lines() {
    static const std::vector<std::string> lines = {
        R"(   _____ _           _      _____ _      _____  )",
        R"(  / ____| |         | |    / ____| |    |_   _| )",
        R"( | |    | |__   __ _| |_  | |    | |      | |   )",
        R"( | |    | '_ \ / _` | __| | |    | |      | |   )",
        R"( | |____| | | | (_| | |_  | |____| |____ _| |_  )",
        R"(  \_____|_| |_|\__,_|\__|  \_____|______|_____| )",
    };
    return lines;
}

int Banner::current_frame() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
    int frame = static_cast<int>((elapsed * FRAME_COUNT) / ANIMATION_DURATION_MS);
    return std::min(frame, FRAME_COUNT - 1);
}

ftxui::Element Banner::render(ftxui::Color color, bool animate) {
    const auto& lines = get_banner_lines();
    int frame = animate ? current_frame() : FRAME_COUNT - 1;

    // Number of lines to show based on animation progress
    int visible_lines = static_cast<int>((static_cast<float>(frame + 1) / FRAME_COUNT) * lines.size());
    visible_lines = std::max(1, std::min(visible_lines, static_cast<int>(lines.size())));

    ftxui::Elements elements;

    // Add empty lines for the "drop-in" effect
    if (animate) {
        int padding = static_cast<int>(lines.size()) - visible_lines;
        for (int i = 0; i < padding; ++i) {
            elements.push_back(ftxui::text(""));
        }
    }

    for (int i = 0; i < visible_lines; ++i) {
        auto line_el = ftxui::text(lines[i]) | ftxui::bold;

        // Apply color with a gradient effect based on line position
        if (i == 0 || i == static_cast<int>(lines.size()) - 1) {
            line_el = line_el | ftxui::color(color);
        } else {
            line_el = line_el | ftxui::color(color);
        }

        // Dim effect for lines that just appeared
        if (animate && i == visible_lines - 1 && frame < FRAME_COUNT - 1) {
            line_el = line_el | ftxui::dim;
        }

        elements.push_back(line_el);
    }

    // Subtitle
    if (frame >= FRAME_COUNT - 2 || !animate) {
        elements.push_back(ftxui::text(""));
        elements.push_back(
            ftxui::text("  Secure Terminal Chat") | ftxui::dim | ftxui::color(color)
        );
    }

    return ftxui::vbox(elements) | ftxui::center;
}

ftxui::Element Banner::render_compact(ftxui::Color color) {
    return ftxui::hbox({
        ftxui::text(" Chat CLI ") | ftxui::bold | ftxui::color(color),
        ftxui::text(" ") | ftxui::dim,
        ftxui::text("Secure Terminal Chat") | ftxui::dim,
    });
}

} // namespace chat::client
