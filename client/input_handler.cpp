#include "input_handler.hpp"
#include <algorithm>

namespace chat::client {

InputHandler::InputHandler(TlsClient& client, KeyManager& key_manager, GroupManager& group_manager)
    : client_(client), key_manager_(key_manager), group_manager_(group_manager) {}

const std::vector<std::string>& InputHandler::available_commands() {
    static const std::vector<std::string> commands = {
        "/login", "/register", "/join", "/leave", "/rooms",
        "/users", "/dm", "/rename", "/quit", "/exit", "/help",
        "/verify", "/roomkeys"
    };
    return commands;
}

void InputHandler::report_error(const std::string& error) {
    if (error_callback_) {
        error_callback_(error);
    }
}

void InputHandler::report_status(const std::string& status) {
    if (status_callback_) {
        status_callback_(status);
    }
}

void InputHandler::process_line(const std::string& line) {
    if (line.empty()) return;

    // Check if it's a command
    if (line[0] == '/') {
        auto args = split_args(line.substr(1));
        if (!args.empty()) {
            std::string cmd = args[0];
            std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);
            args.erase(args.begin());
            handle_command(cmd, args);
        }
    } else {
        // Regular chat message — room messages are end-to-end encrypted.
        send_room_message(line);
    }
}

void InputHandler::send_room_message(const std::string& text) {
    if (!authenticated_) {
        report_error("Please login first. Use /login <username> <password>");
        return;
    }
    if (current_room_.empty()) {
        report_error("Not in any room. Use /join <room> first.");
        return;
    }

    auto msg = protocol::Message::create(protocol::MessageType::ENCRYPTED_MESSAGE,
                                         username_, current_room_);
    std::string err;
    auto payload = group_manager_.seal(current_room_, msg.id, text, err);
    if (!payload.has_value()) {
        report_error("Cannot encrypt for #" + current_room_ + ": " + err +
                     ". Room keys are still being exchanged — try again shortly.");
        return;
    }

    msg.payload = *payload;
    client_.send(msg);
}

void InputHandler::handle_command(const std::string& cmd,
                                  const std::vector<std::string>& args) {
    if (cmd == "quit" || cmd == "exit" || cmd == "q") {
        client_.send_command("quit");
        if (quit_callback_) {
            quit_callback_();
        }
        return;
    }

    if (cmd == "help" || cmd == "h" || cmd == "?") {
        client_.send_command("help");
        return;
    }

    if (cmd == "login") {
        if (args.size() < 2) {
            report_error("Usage: /login <username> <password>");
            return;
        }
        nlohmann::json payload = {
            {"username", args[0]},
            {"password", args[1]},
            {"protocol_version", protocol::PROTOCOL_VERSION}
        };
        client_.send_command("login", payload);
        return;
    }

    if (cmd == "register") {
        if (args.size() < 2) {
            report_error("Usage: /register <username> <password>");
            return;
        }
        nlohmann::json payload = {
            {"username", args[0]},
            {"password", args[1]},
            {"protocol_version", protocol::PROTOCOL_VERSION}
        };
        client_.send_command("register", payload);
        return;
    }

    if (cmd == "join" || cmd == "j") {
        if (args.empty()) {
            report_error("Usage: /join <room>");
            return;
        }
        nlohmann::json payload = {{"room", args[0]}};
        client_.send_command("join", payload);
        return;
    }

    if (cmd == "leave" || cmd == "part") {
        nlohmann::json payload;
        if (!args.empty()) {
            payload["room"] = args[0];
        }
        client_.send_command("leave", payload);
        return;
    }

    if (cmd == "users" || cmd == "who") {
        nlohmann::json payload;
        if (!args.empty()) {
            payload["room"] = args[0];
        }
        client_.send_command("users", payload);
        return;
    }

    if (cmd == "dm" || cmd == "msg" || cmd == "pm") {
        if (args.size() < 2) {
            report_error("Usage: /dm <user> <message>");
            return;
        }
        std::string recipient = args[0];
        std::string message;
        for (size_t i = 1; i < args.size(); ++i) {
            if (i > 1) message += " ";
            message += args[i];
        }
        nlohmann::json payload = {
            {"to", recipient},
            {"message", message}
        };

        // Attempt E2EE encryption if we have the recipient's key
        auto cached_key = key_manager_.get_cached_key(recipient);
        if (cached_key.has_value() && key_manager_.is_initialized()) {
            auto encrypted = key_manager_.encrypt(message, cached_key.value());
            if (encrypted.has_value()) {
                payload["message"] = encrypted->ciphertext;
                payload["nonce"] = encrypted->nonce;
                payload["sender_public_key"] = key_manager_.get_public_key();
                payload["encrypted"] = true;
                remember_sent_dm(encrypted->ciphertext, message);
            }
        } else if (key_manager_.is_initialized()) {
            // Request the recipient's key for next time
            auto key_req = protocol::Message::create(protocol::MessageType::KEY_EXCHANGE, username_);
            key_req.payload = {
                {"action", "request_key"},
                {"username", recipient}
            };
            client_.send(key_req);
        }

        client_.send_command("dm", payload);
        return;
    }

    if (cmd == "rename" || cmd == "nick") {
        if (args.empty()) {
            report_error("Usage: /rename <newname>");
            return;
        }
        nlohmann::json payload = {{"newname", args[0]}};
        client_.send_command("rename", payload);
        return;
    }

    if (cmd == "rooms") {
        client_.send_command("rooms");
        return;
    }

    if (cmd == "verify") {
        handle_verify(args);
        return;
    }

    if (cmd == "roomkeys") {
        handle_roomkeys(args);
        return;
    }

    // Unknown command - send to server anyway
    nlohmann::json payload;
    if (!args.empty()) {
        payload["args"] = args;
    }
    client_.send_command(cmd, payload);
}

void InputHandler::handle_verify(const std::vector<std::string>& args) {
    // /verify                -> show our own identity fingerprint
    // /verify <user>         -> show a peer's fingerprint + trust status
    // /verify <user> ok      -> mark peer as verified (compared out-of-band)
    // /verify <user> accept  -> accept a changed identity key

    if (args.empty()) {
        report_status("Your identity fingerprint:\n  " +
                      key_manager_.get_identity_fingerprint() +
                      "\nShare it out-of-band; others can confirm with /verify <you> ok");
        return;
    }

    std::string user = args[0];
    std::string action = args.size() > 1 ? args[1] : "";
    std::transform(action.begin(), action.end(), action.begin(), ::tolower);

    if (action == "ok" || action == "confirm" || action == "verified") {
        if (group_manager_.mark_peer_verified(user, true)) {
            report_status("Marked '" + user + "' as verified.");
        } else {
            report_error("No pinned key for '" + user +
                         "' yet — wait for a room key or DM from them.");
        }
        return;
    }

    if (action == "accept") {
        if (group_manager_.accept_peer_key(user)) {
            report_status("Accepted the new identity key for '" + user +
                          "' (unverified — compare fingerprints out-of-band).");
        } else {
            report_error("No changed key pending for '" + user + "'.");
        }
        return;
    }

    if (!action.empty()) {
        report_error("Usage: /verify <user> [ok|accept]");
        return;
    }

    auto info = group_manager_.peer_info(user);
    if (!info.has_value()) {
        report_error("Could not read trust state for '" + user + "'.");
        return;
    }

    if (!info->pin_known) {
        report_status("No keys seen for '" + user +
                      "' yet (a TOFU pin is created on first contact).");
        return;
    }

    std::string out = "Identity for '" + user + "':";
    out += "\n  Fingerprint: " + info->fingerprint;
    if (info->ed25519_b64.empty()) {
        out += "  (signing key not seen yet — covers box key only)";
    }
    out += "\n  Box key:     " + info->x25519_b64;
    out += "\n  Signing key: " +
           (info->ed25519_b64.empty() ? std::string("(not seen yet)") : info->ed25519_b64);

    if (info->pending_change) {
        out += "\n  Status:      ! IDENTITY KEY CHANGED — investigate, then /verify " +
               user + " accept to re-pin";
    } else if (info->verified) {
        out += "\n  Status:      OK verified";
    } else {
        out += "\n  Status:      pinned (trust on first use)";
    }
    report_status(out);
}

void InputHandler::handle_roomkeys(const std::vector<std::string>& args) {
    std::string room = args.empty() ? current_room_ : args[0];
    if (room.empty()) {
        report_error("Usage: /roomkeys [room]");
        return;
    }

    auto summary = group_manager_.room_summary(room);
    if (!summary.has_value()) {
        report_error("No encryption state for #" + room +
                     " yet (join the room first).");
        return;
    }

    std::string out = "Room key state for #" + room + ":";
    out += "\n  Epoch:    " + std::to_string(summary->epoch);
    out += "\n  Our key:  " +
           (summary->own_key_set ? summary->own_key_id : std::string("(not generated yet)"));

    out += "\n  Members with keys:";
    if (summary->peers_with_keys.empty()) {
        out += " (none)";
    }
    for (const auto& peer : summary->peers_with_keys) {
        std::string status = "pinned";
        auto info = group_manager_.peer_info(peer);
        if (info.has_value()) {
            if (info->pending_change) {
                status = "! KEY CHANGED";
            } else if (info->verified) {
                status = "OK verified";
            }
        }
        out += "\n    - " + peer + " (" + status + ")";
    }

    if (!summary->members_without_keys.empty()) {
        out += "\n  Awaiting keys from:";
        for (const auto& peer : summary->members_without_keys) {
            out += "\n    - " + peer;
        }
    }

    report_status(out);
}

std::vector<std::string> InputHandler::split_args(const std::string& input) {
    std::vector<std::string> args;
    std::istringstream iss(input);
    std::string arg;

    while (iss >> arg) {
        args.push_back(arg);
    }

    return args;
}

void InputHandler::remember_sent_dm(const std::string& ciphertext,
                                    const std::string& plaintext) {
    std::lock_guard<std::mutex> lock(dm_echo_mutex_);
    sent_dm_plaintext_[ciphertext] = plaintext;
    sent_dm_order_.push_back(ciphertext);
    while (sent_dm_order_.size() > 100) {
        sent_dm_plaintext_.erase(sent_dm_order_.front());
        sent_dm_order_.pop_front();
    }
}

std::optional<std::string> InputHandler::take_sent_dm_plaintext(
    const std::string& ciphertext) {
    std::lock_guard<std::mutex> lock(dm_echo_mutex_);
    auto it = sent_dm_plaintext_.find(ciphertext);
    if (it == sent_dm_plaintext_.end()) {
        return std::nullopt;
    }
    std::string plaintext = it->second;
    sent_dm_plaintext_.erase(it);
    return plaintext;
}

} // namespace chat::client
