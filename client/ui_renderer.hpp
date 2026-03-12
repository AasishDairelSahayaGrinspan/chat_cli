#pragma once

#include "message_store.hpp"
#include "theme.hpp"
#include <ftxui/dom/elements.hpp>

namespace chat::client {

class UiRenderer {
public:
    // Convert a DisplayItem into an ftxui::Element for rendering
    static ftxui::Element render(const DisplayItem& item, const Theme& theme);

private:
    static ftxui::Element render_chat(const DisplayItem& item, const Theme& theme);
    static ftxui::Element render_dm(const DisplayItem& item, const Theme& theme);
    static ftxui::Element render_system(const DisplayItem& item, const Theme& theme);
    static ftxui::Element render_error(const DisplayItem& item, const Theme& theme);
    static ftxui::Element render_presence(const DisplayItem& item, const Theme& theme);
    static ftxui::Element render_auth(const DisplayItem& item, const Theme& theme);
    static ftxui::Element render_room_event(const DisplayItem& item, const Theme& theme);
    static ftxui::Element render_status(const DisplayItem& item, const Theme& theme);
};

} // namespace chat::client
