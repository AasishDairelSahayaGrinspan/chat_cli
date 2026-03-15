#include "ftxui_app.hpp"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

#include <algorithm>
#include <thread>
#include <chrono>
#include <cstdlib>

namespace chat::client {

FtxuiApp::FtxuiApp(const std::string& host, uint16_t port, bool verify_ssl, const std::string& ca_cert_path)
    : host_(host)
    , port_(port)
    , verify_ssl_(verify_ssl)
    , ca_cert_path_(ca_cert_path)
    , client_(host, port, verify_ssl, ca_cert_path)
    , key_manager_()
    , input_handler_(client_, key_manager_) {
}

FtxuiApp::~FtxuiApp() {
    running_ = false;
    client_.stop();
}

int FtxuiApp::run() {
    // Initialize E2EE key manager
    std::string home_dir;
    if (const char* home = std::getenv("HOME")) {
        home_dir = home;
    } else {
        home_dir = ".";
    }
    key_manager_.load_or_create(home_dir + "/.chat_cli/keys");

    // Setup quit handler
    input_handler_.set_quit_callback([this]() {
        running_ = false;
        screen_.Exit();
    });

    // Setup error callback — route to message store
    input_handler_.set_error_callback([this](const std::string& error) {
        message_store_.push_local(DisplayItem::Kind::ERROR, error);
        screen_.PostEvent(ftxui::Event::Custom);
    });

    // Setup message handler — called from network thread
    client_.set_message_handler([this](const protocol::Message& msg) {
        on_network_message(msg);
    });

    // Setup disconnect handler
    client_.set_disconnect_handler([this](const std::string& reason) {
        on_disconnect(reason);
    });

    // Enable reconnection
    client_.enable_reconnect(5, 3000);

    // Start the network I/O thread
    client_.run();

    // Begin connecting
    state_ = AppState::CONNECTING;
    connect_time_ = std::chrono::steady_clock::now();

    std::atomic<bool> connected{false};
    std::string connect_error;

    client_.connect([this, &connected, &connect_error](bool success, const std::string& error) {
        if (success) {
            connected = true;
            banner_start_ = std::chrono::steady_clock::now();
            state_ = AppState::BANNER;
            screen_.PostEvent(ftxui::Event::Custom);
        } else {
            connect_error = error;
            message_store_.push_local(DisplayItem::Kind::ERROR, "Connection failed: " + error);
            screen_.PostEvent(ftxui::Event::Custom);
        }
    });

    // Build and run the FTXUI UI
    auto main_ui = build_main_ui();
    screen_.Loop(main_ui);

    // Cleanup
    client_.stop();
    return 0;
}

ftxui::Component FtxuiApp::build_main_ui() {
    // Input component
    auto input_option = ftxui::InputOption();
    input_option.multiline = false;
    input_option.placeholder = "Type a message or /command...";
    input_option.on_enter = [this] {
        handle_input_submit();
    };
    input_option.on_change = [this] {
        update_completions();
    };

    auto input_component = ftxui::Input(&input_content_, input_option);

    // Wrap everything in a renderer with event catching
    auto main_component = ftxui::CatchEvent(input_component, [this](ftxui::Event event) {
        // Tab completion
        if (event == ftxui::Event::Tab) {
            if (!completion_hints_.empty()) {
                input_content_ = completion_hints_[0] + " ";
                completion_hints_.clear();
                return true;
            }
        }

        // Escape to clear input
        if (event == ftxui::Event::Escape) {
            input_content_.clear();
            completion_hints_.clear();
            return true;
        }

        // Ctrl+C to quit
        if (event.is_character() && event.character() == "\x03") {
            running_ = false;
            screen_.Exit();
            return true;
        }

        return false;
    });

    // Main renderer wrapping the input component
    auto renderer = ftxui::Renderer(main_component, [this, main_component] {
        // Drain any pending network messages
        size_t new_count = message_store_.drain();

        // Check if banner animation is done
        if (state_ == AppState::BANNER) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - banner_start_).count();
            if (elapsed > Banner::ANIMATION_DURATION_MS + 500) {
                state_ = AppState::CHAT;
                show_banner_ = false;
                message_store_.push_local(DisplayItem::Kind::STATUS,
                    "Connected! Type /help for available commands.");
            }
        }

        // Mark messages as seen after one render cycle
        // (this implements the fade-in: first render is dim, subsequent renders are normal)
        static bool should_mark = false;
        if (should_mark) {
            message_store_.mark_all_seen();
            should_mark = false;
        }
        if (new_count > 0) {
            should_mark = true;
        }

        // Build the full screen layout
        ftxui::Elements layout;

        // Banner or compact title
        if (state_ == AppState::BANNER || show_banner_) {
            layout.push_back(render_banner_area());
            layout.push_back(ftxui::separator() | ftxui::color(theme_.border_color));
        } else {
            layout.push_back(
                Banner::render_compact(theme_.banner)
                | ftxui::borderLight
                | ftxui::color(theme_.border_color)
            );
        }

        // Connecting state
        if (state_ == AppState::CONNECTING) {
            layout.push_back(ftxui::filler());
            layout.push_back(render_spinner("Connecting to " + host_ + "...") | ftxui::center);
            layout.push_back(ftxui::filler());
        } else {
            // Message area (flex-grow)
            layout.push_back(render_message_area() | ftxui::flex);
            layout.push_back(ftxui::separator() | ftxui::color(theme_.border_color));

            // Completion hints (conditional)
            if (!completion_hints_.empty()) {
                layout.push_back(render_completion_hints());
            }

            // Input area
            layout.push_back(render_input_area());
        }

        // Status bar (always at bottom)
        layout.push_back(render_status_bar());

        return ftxui::vbox(layout) | ftxui::borderRounded | ftxui::color(theme_.border_color);
    });

    // Request animation frames for spinners and banner
    auto animated = ftxui::Renderer(renderer, [this, renderer] {
        // Schedule periodic redraws when animating
        if (state_ == AppState::CONNECTING || state_ == AppState::BANNER) {
            std::thread([this] {
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
                if (running_) {
                    screen_.PostEvent(ftxui::Event::Custom);
                }
            }).detach();
        }
        return renderer->Render();
    });

    return animated;
}

ftxui::Element FtxuiApp::render_banner_area() {
    bool animate = (state_ == AppState::BANNER);
    return Banner::render(theme_.banner, animate);
}

ftxui::Element FtxuiApp::render_message_area() {
    const auto& items = message_store_.items();

    if (items.empty()) {
        return ftxui::vbox({
            ftxui::filler(),
            ftxui::text("No messages yet. Join a room to start chatting.")
                | ftxui::dim | ftxui::center,
            ftxui::filler(),
        });
    }

    ftxui::Elements messages;
    for (const auto& item : items) {
        messages.push_back(UiRenderer::render(item, theme_));
    }

    return ftxui::vbox(messages)
        | ftxui::yframe
        | ftxui::focusPositionRelative(0, 1);
}

ftxui::Element FtxuiApp::render_completion_hints() {
    ftxui::Elements hints;
    hints.push_back(ftxui::text(" ") | ftxui::dim);

    for (size_t i = 0; i < completion_hints_.size() && i < 8; ++i) {
        if (i > 0) {
            hints.push_back(ftxui::text("  ") | ftxui::dim);
        }
        hints.push_back(
            ftxui::text(completion_hints_[i])
            | ftxui::color(theme_.accent)
            | (i == 0 ? ftxui::bold : ftxui::dim)
        );
    }

    if (completion_hints_.size() > 8) {
        hints.push_back(ftxui::text("  +" + std::to_string(completion_hints_.size() - 8) + " more") | ftxui::dim);
    }

    return ftxui::hbox(hints)
        | ftxui::bgcolor(ftxui::Color::RGB(40, 40, 40));
}

ftxui::Element FtxuiApp::render_input_area() {
    ftxui::Elements parts;

    // Prompt indicator
    if (!input_handler_.username().empty()) {
        parts.push_back(
            ftxui::text(input_handler_.username())
            | ftxui::bold
            | ftxui::color(theme_.username)
        );
        parts.push_back(ftxui::text(" "));
    }

    if (!input_handler_.current_room().empty()) {
        parts.push_back(
            ftxui::text("#" + input_handler_.current_room())
            | ftxui::color(theme_.room_name)
        );
        parts.push_back(ftxui::text(" "));
    }

    parts.push_back(
        ftxui::text("❯ ") | ftxui::bold | ftxui::color(theme_.accent)
    );

    // The input content (rendered by FTXUI's Input component, but we show the
    // prompt prefix here; the actual input is rendered by the component framework)
    parts.push_back(
        ftxui::text(input_content_) | ftxui::flex
    );

    return ftxui::hbox(parts);
}

ftxui::Element FtxuiApp::render_status_bar() {
    ftxui::Elements parts;

    // User info
    std::string user_text = input_handler_.username().empty()
        ? "not logged in" : input_handler_.username();
    parts.push_back(
        ftxui::text(" " + user_text + " ")
        | ftxui::color(theme_.status_bar_fg)
    );

    parts.push_back(ftxui::text("│") | ftxui::color(theme_.border_color));

    // Room info
    std::string room_text = input_handler_.current_room().empty()
        ? "no room" : "#" + input_handler_.current_room();
    parts.push_back(
        ftxui::text(" " + room_text + " ")
        | ftxui::color(theme_.status_bar_fg)
    );

    parts.push_back(ftxui::text("│") | ftxui::color(theme_.border_color));

    // Spacer
    parts.push_back(ftxui::filler());

    // Connection status
    bool connected = client_.is_connected();
    parts.push_back(
        ftxui::text(connected ? " ● connected " : " ○ disconnected ")
        | ftxui::color(connected ? theme_.success : theme_.error)
    );

    return ftxui::hbox(parts)
        | ftxui::bgcolor(theme_.status_bar_bg);
}

ftxui::Element FtxuiApp::render_spinner(const std::string& label) {
    static const std::vector<std::string> frames = {
        "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"
    };

    auto now = std::chrono::steady_clock::now().time_since_epoch();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    size_t idx = (ms / 80) % frames.size();

    return ftxui::hbox({
        ftxui::text(frames[idx]) | ftxui::bold | ftxui::color(theme_.accent),
        ftxui::text(" " + label) | ftxui::dim,
    });
}

bool FtxuiApp::handle_input_submit() {
    if (input_content_.empty()) return false;

    std::string line = input_content_;
    input_content_.clear();
    completion_hints_.clear();

    // Process through input handler (which dispatches to TlsClient)
    input_handler_.process_line(line);

    // Handle state updates from input handler
    // (auth response and room events come back via network messages)

    return true;
}

void FtxuiApp::update_completions() {
    completion_hints_.clear();

    if (input_content_.empty() || input_content_[0] != '/') {
        return;
    }

    const auto& commands = InputHandler::available_commands();
    for (const auto& cmd : commands) {
        if (cmd.find(input_content_) == 0 && cmd != input_content_) {
            completion_hints_.push_back(cmd);
        }
    }
}

void FtxuiApp::on_network_message(const protocol::Message& msg) {
    // Push to thread-safe message store
    message_store_.push(msg);

    // Update input handler state based on message type
    if (msg.type == protocol::MessageType::AUTH_RESPONSE) {
        if (msg.payload.value("success", false)) {
            input_handler_.set_authenticated(true);
            if (msg.payload.contains("username")) {
                input_handler_.set_username(msg.payload["username"]);
            }

            // Publish our E2EE public key to server on successful login
            if (key_manager_.is_initialized()) {
                auto key_msg = protocol::Message::create(protocol::MessageType::KEY_EXCHANGE,
                    msg.payload.value("username", ""));
                key_msg.payload = {
                    {"action", "publish_key"},
                    {"public_key", key_manager_.get_public_key()}
                };
                client_.send(key_msg);
            }
        }
    } else if (msg.type == protocol::MessageType::KEY_EXCHANGE) {
        std::string action = msg.payload.value("action", "");
        if (action == "key_response" && msg.payload.value("found", false)) {
            std::string username = msg.payload.value("username", "");
            std::string public_key = msg.payload.value("public_key", "");
            if (!username.empty() && !public_key.empty()) {
                key_manager_.cache_key(username, public_key);
            }
        }
        // Don't push KEY_EXCHANGE messages to display
        screen_.PostEvent(ftxui::Event::Custom);
        return;
    } else if (msg.type == protocol::MessageType::DIRECT_MESSAGE) {
        // Decrypt incoming encrypted DMs before they reach the message store
        if (msg.payload.value("encrypted", false) && key_manager_.is_initialized()) {
            std::string ciphertext = msg.payload.value("message", "");
            std::string nonce = msg.payload.value("nonce", "");
            std::string sender_pk = msg.payload.value("sender_public_key", "");
            bool is_sent = msg.payload.value("sent", false);

            if (!ciphertext.empty() && !nonce.empty() && !sender_pk.empty() && !is_sent) {
                auto plaintext = key_manager_.decrypt(ciphertext, nonce, sender_pk);
                if (plaintext.has_value()) {
                    // Create a modified copy with decrypted content
                    protocol::Message decrypted_msg = msg;
                    decrypted_msg.payload["message"] = plaintext.value();
                    decrypted_msg.payload["e2ee"] = true;
                    message_store_.push(decrypted_msg);

                    // Cache the sender's public key
                    std::string from_user = msg.payload.value("from", msg.from);
                    if (!from_user.empty()) {
                        key_manager_.cache_key(from_user, sender_pk);
                    }

                    screen_.PostEvent(ftxui::Event::Custom);
                    return;
                }
            }
        }
    } else if (msg.type == protocol::MessageType::ROOM_EVENT) {
        std::string action = msg.payload.value("action", "");
        if (action == "joined") {
            input_handler_.set_current_room(msg.payload.value("room", ""));
        } else if (action == "left" &&
                   msg.payload.value("room", "") == input_handler_.current_room()) {
            input_handler_.set_current_room("");
        }
    } else if (msg.type == protocol::MessageType::PRESENCE) {
        std::string action = msg.payload.value("action", "");
        if (action == "rename" &&
            msg.payload.value("old_name", "") == input_handler_.username()) {
            input_handler_.set_username(msg.payload.value("new_name", ""));
        }
    }

    // Wake the FTXUI event loop to re-render
    screen_.PostEvent(ftxui::Event::Custom);
}

void FtxuiApp::on_disconnect(const std::string& reason) {
    message_store_.push_local(DisplayItem::Kind::ERROR, "Disconnected: " + reason);

    if (!client_.is_connected()) {
        message_store_.push_local(DisplayItem::Kind::STATUS, "Attempting to reconnect...");
    }

    screen_.PostEvent(ftxui::Event::Custom);
}

} // namespace chat::client
