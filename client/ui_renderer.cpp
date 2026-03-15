#include "ui_renderer.hpp"

namespace chat::client {

ftxui::Element UiRenderer::render(const DisplayItem& item, const Theme& theme) {
    ftxui::Element el;

    switch (item.kind) {
        case DisplayItem::Kind::CHAT:
            el = render_chat(item, theme);
            break;
        case DisplayItem::Kind::DM:
            el = render_dm(item, theme);
            break;
        case DisplayItem::Kind::SYSTEM:
            el = render_system(item, theme);
            break;
        case DisplayItem::Kind::ERROR:
            el = render_error(item, theme);
            break;
        case DisplayItem::Kind::PRESENCE:
            el = render_presence(item, theme);
            break;
        case DisplayItem::Kind::AUTH:
            el = render_auth(item, theme);
            break;
        case DisplayItem::Kind::ROOM_EVENT:
            el = render_room_event(item, theme);
            break;
        case DisplayItem::Kind::STATUS:
            el = render_status(item, theme);
            break;
    }

    // Fade-in effect for new messages
    if (item.is_new) {
        el = el | ftxui::dim;
    }

    return el;
}

ftxui::Element UiRenderer::render_chat(const DisplayItem& item, const Theme& theme) {
    ftxui::Elements parts;

    parts.push_back(
        ftxui::text("[" + item.timestamp + "] ") | ftxui::color(theme.dim_text)
    );

    if (!item.room.empty()) {
        parts.push_back(
            ftxui::text("#" + item.room + " ") | ftxui::color(theme.room_name)
        );
    }

    parts.push_back(
        ftxui::text(item.sender) | ftxui::bold | ftxui::color(theme.username)
    );
    parts.push_back(ftxui::text(": " + item.content));

    return ftxui::hbox(parts);
}

ftxui::Element UiRenderer::render_dm(const DisplayItem& item, const Theme& theme) {
    ftxui::Elements parts;

    parts.push_back(
        ftxui::text("[" + item.timestamp + "] ") | ftxui::color(theme.dim_text)
    );

    if (item.encrypted) {
        parts.push_back(
            ftxui::text("\xF0\x9F\x94\x92 ") | ftxui::color(theme.success)
        );
    }

    parts.push_back(
        ftxui::text(item.content) | ftxui::color(theme.dm_color)
    );

    return ftxui::hbox(parts);
}

ftxui::Element UiRenderer::render_system(const DisplayItem& item, const Theme& theme) {
    return ftxui::hbox({
        ftxui::text("* ") | ftxui::color(theme.system_msg),
        ftxui::text(item.content) | ftxui::color(theme.system_msg),
    });
}

ftxui::Element UiRenderer::render_error(const DisplayItem& item, const Theme& theme) {
    return ftxui::hbox({
        ftxui::text("✗ Error: ") | ftxui::bold | ftxui::color(theme.error),
        ftxui::text(item.content) | ftxui::color(theme.error),
    });
}

ftxui::Element UiRenderer::render_presence(const DisplayItem& item, const Theme& theme) {
    return ftxui::hbox({
        ftxui::text("● ") | ftxui::color(theme.presence),
        ftxui::text(item.content) | ftxui::color(theme.presence),
    });
}

ftxui::Element UiRenderer::render_auth(const DisplayItem& item, const Theme& theme) {
    bool success = item.content.size() >= 2 && item.content[0] == '\xe2'; // ✓ starts with UTF-8 ✓
    // Check first char for success/failure marker
    if (item.original.payload.value("success", false)) {
        return ftxui::text(item.content) | ftxui::color(theme.success);
    } else {
        return ftxui::text(item.content) | ftxui::color(theme.error);
    }
}

ftxui::Element UiRenderer::render_room_event(const DisplayItem& item, const Theme& theme) {
    return ftxui::hbox({
        ftxui::text("→ ") | ftxui::color(theme.system_msg),
        ftxui::text(item.content) | ftxui::color(theme.system_msg),
    });
}

ftxui::Element UiRenderer::render_status(const DisplayItem& item, const Theme& theme) {
    return ftxui::hbox({
        ftxui::text("* ") | ftxui::color(theme.system_msg),
        ftxui::text(item.content) | ftxui::dim,
    });
}

} // namespace chat::client
