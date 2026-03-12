#include "theme.hpp"

namespace chat::client {

Theme Theme::dark() {
    Theme t;
    t.accent       = ftxui::Color::Cyan;
    t.username     = ftxui::Color::Green;
    t.room_name    = ftxui::Color::Cyan;
    t.prompt       = ftxui::Color::White;
    t.success      = ftxui::Color::Green;
    t.error        = ftxui::Color::Red;
    t.warning      = ftxui::Color::Yellow;
    t.system_msg   = ftxui::Color::Cyan;
    t.dm_color     = ftxui::Color::Magenta;
    t.presence     = ftxui::Color::Yellow;
    t.dim_text     = ftxui::Color::GrayDark;
    t.status_bar_bg = ftxui::Color::RGB(30, 30, 30);
    t.status_bar_fg = ftxui::Color::GrayLight;
    t.border_color = ftxui::Color::GrayDark;
    t.banner       = ftxui::Color::Cyan;
    return t;
}

Theme Theme::light() {
    Theme t;
    t.accent       = ftxui::Color::Blue;
    t.username     = ftxui::Color::RGB(0, 100, 0);
    t.room_name    = ftxui::Color::Blue;
    t.prompt       = ftxui::Color::Black;
    t.success      = ftxui::Color::RGB(0, 128, 0);
    t.error        = ftxui::Color::RGB(180, 0, 0);
    t.warning      = ftxui::Color::RGB(180, 140, 0);
    t.system_msg   = ftxui::Color::Blue;
    t.dm_color     = ftxui::Color::Magenta;
    t.presence     = ftxui::Color::RGB(180, 140, 0);
    t.dim_text     = ftxui::Color::GrayLight;
    t.status_bar_bg = ftxui::Color::RGB(230, 230, 230);
    t.status_bar_fg = ftxui::Color::Black;
    t.border_color = ftxui::Color::GrayLight;
    t.banner       = ftxui::Color::Blue;
    return t;
}

Theme Theme::copilot() {
    Theme t;
    t.accent       = ftxui::Color::RGB(88, 166, 255);   // GitHub blue
    t.username     = ftxui::Color::RGB(63, 185, 80);     // GitHub green
    t.room_name    = ftxui::Color::RGB(88, 166, 255);
    t.prompt       = ftxui::Color::White;
    t.success      = ftxui::Color::RGB(63, 185, 80);
    t.error        = ftxui::Color::RGB(248, 81, 73);     // GitHub red
    t.warning      = ftxui::Color::RGB(210, 153, 34);    // GitHub yellow
    t.system_msg   = ftxui::Color::RGB(88, 166, 255);
    t.dm_color     = ftxui::Color::RGB(188, 140, 255);   // GitHub purple
    t.presence     = ftxui::Color::RGB(210, 153, 34);
    t.dim_text     = ftxui::Color::RGB(110, 118, 129);   // GitHub gray
    t.status_bar_bg = ftxui::Color::RGB(22, 27, 34);     // GitHub dark bg
    t.status_bar_fg = ftxui::Color::RGB(201, 209, 217);
    t.border_color = ftxui::Color::RGB(48, 54, 61);      // GitHub border
    t.banner       = ftxui::Color::RGB(88, 166, 255);
    return t;
}

} // namespace chat::client
