#pragma once

#include <ftxui/screen/color.hpp>

namespace chat::client {

struct Theme {
    // Semantic color roles
    ftxui::Color accent       = ftxui::Color::Cyan;
    ftxui::Color username     = ftxui::Color::Green;
    ftxui::Color room_name    = ftxui::Color::Cyan;
    ftxui::Color prompt       = ftxui::Color::White;
    ftxui::Color success      = ftxui::Color::Green;
    ftxui::Color error        = ftxui::Color::Red;
    ftxui::Color warning      = ftxui::Color::Yellow;
    ftxui::Color system_msg   = ftxui::Color::Cyan;
    ftxui::Color dm_color     = ftxui::Color::Magenta;
    ftxui::Color presence     = ftxui::Color::Yellow;
    ftxui::Color dim_text     = ftxui::Color::GrayDark;
    ftxui::Color status_bar_bg = ftxui::Color::GrayDark;
    ftxui::Color status_bar_fg = ftxui::Color::White;
    ftxui::Color border_color = ftxui::Color::GrayLight;
    ftxui::Color banner       = ftxui::Color::Cyan;

    // Named themes
    static Theme dark();
    static Theme light();
    static Theme copilot();
};

} // namespace chat::client
