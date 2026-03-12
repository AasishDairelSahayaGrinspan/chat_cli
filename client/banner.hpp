#pragma once

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <string>
#include <vector>

namespace chat::client {

class Banner {
public:
    // Get the banner as an ftxui::Element
    static ftxui::Element render(ftxui::Color color, bool animate = true);

    // Get a compact single-line title
    static ftxui::Element render_compact(ftxui::Color color);

    // Number of animation frames
    static constexpr int FRAME_COUNT = 6;

    // Duration of the full animation in milliseconds
    static constexpr int ANIMATION_DURATION_MS = 2500;

private:
    static const std::vector<std::string>& get_banner_lines();
    static int current_frame();
};

} // namespace chat::client
