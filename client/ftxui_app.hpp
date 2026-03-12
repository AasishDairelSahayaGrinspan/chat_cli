#pragma once

#include "tls_client.hpp"
#include "input_handler.hpp"
#include "message_store.hpp"
#include "theme.hpp"
#include "banner.hpp"
#include "ui_renderer.hpp"

#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>

#include <atomic>
#include <string>
#include <vector>
#include <chrono>

namespace chat::client {

class FtxuiApp {
public:
    FtxuiApp(const std::string& host, uint16_t port);
    ~FtxuiApp();

    // Blocking — runs the FTXUI event loop on the calling thread.
    int run();

private:
    // Component builders
    ftxui::Component build_main_ui();

    // Rendering helpers
    ftxui::Element render_banner_area();
    ftxui::Element render_message_area();
    ftxui::Element render_completion_hints();
    ftxui::Element render_input_area();
    ftxui::Element render_status_bar();
    ftxui::Element render_spinner(const std::string& label);

    // Event handlers
    bool handle_input_submit();
    void update_completions();
    void on_network_message(const protocol::Message& msg);
    void on_disconnect(const std::string& reason);

    // Application state
    enum class AppState { CONNECTING, BANNER, CHAT };
    AppState state_ = AppState::CONNECTING;

    std::string host_;
    uint16_t port_;
    std::string input_content_;
    std::vector<std::string> completion_hints_;
    bool show_banner_ = true;
    std::chrono::steady_clock::time_point connect_time_;
    std::chrono::steady_clock::time_point banner_start_;

    // Owned objects
    TlsClient client_;
    InputHandler input_handler_;
    MessageStore message_store_;
    Theme theme_ = Theme::copilot();

    ftxui::ScreenInteractive screen_ = ftxui::ScreenInteractive::Fullscreen();
    std::atomic<bool> running_{true};
};

} // namespace chat::client
