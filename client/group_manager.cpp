#include "group_manager.hpp"
#include "logging/logger.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace chat::client {

using crypto::Bytes;
using crypto::from_b64;
using crypto::to_b64;

GroupManager::GroupManager(KeyManager& keys, std::string state_dir)
    : keys_(keys), state_dir_(std::move(state_dir)) {}

void GroupManager::set_username(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    username_ = username;
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

bool GroupManager::load() {
    std::lock_guard<std::mutex> lock(mutex_);

    trust_.load(state_dir_ + "/trust.json");

    std::ifstream file(state_dir_ + "/group_state.json");
    if (!file.is_open()) {
        return true;  // nothing saved yet
    }

    auto j = nlohmann::json::parse(file, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("rooms")) {
        return false;
    }

    try {
        for (auto it = j.at("rooms").begin(); it != j.at("rooms").end(); ++it) {
            const auto& rj = it.value();
            RoomState state;
            state.epoch = rj.value("epoch", uint64_t(0));
            state.own_epoch = rj.value("own_epoch", uint64_t(0));

            if (rj.contains("members") && rj.at("members").is_array()) {
                state.members = rj.at("members").get<std::vector<std::string>>();
            }
            if (rj.contains("distributed") && rj.at("distributed").is_array()) {
                for (const auto& m : rj.at("distributed")) {
                    state.distributed.insert(m.get<std::string>());
                }
            }
            if (rj.contains("own") && rj.at("own").is_object()) {
                state.own = crypto::OutboundSenderKey::deserialize_state(rj.at("own").dump());
            }
            if (rj.contains("peers") && rj.at("peers").is_object()) {
                for (auto pit = rj.at("peers").begin(); pit != rj.at("peers").end(); ++pit) {
                    if (!pit.value().is_array()) {
                        continue;
                    }
                    for (const auto& cj : pit.value()) {
                        if (!cj.is_object() || !cj.contains("state")) {
                            continue;
                        }
                        auto chain = crypto::InboundSenderKey::deserialize_state(cj.at("state").dump());
                        if (!chain.has_value()) {
                            continue;
                        }
                        state.peers[pit.key()].emplace_back(
                            cj.value("epoch", uint64_t(0)),
                            cj.value("key_id", ""),
                            std::move(*chain));
                    }
                }
            }

            rooms_[it.key()] = std::move(state);
        }
    } catch (const std::exception& e) {
        LOG_WARN("Failed to load group state: {}", e.what());
        return false;
    }

    LOG_DEBUG("Loaded room E2EE state for {} room(s)", rooms_.size());
    return true;
}

void GroupManager::save() const {
    std::lock_guard<std::mutex> lock(mutex_);

    trust_.save(state_dir_ + "/trust.json");

    nlohmann::json rooms = nlohmann::json::object();
    for (const auto& [room, state] : rooms_) {
        nlohmann::json peers = nlohmann::json::object();
        for (const auto& [user, chains] : state.peers) {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& pc : chains) {
                arr.push_back({
                    {"epoch", pc.epoch},
                    {"key_id", pc.key_id},
                    {"state", nlohmann::json::parse(pc.chain.serialize_state())},
                });
            }
            peers[user] = std::move(arr);
        }

        rooms[room] = {
            {"epoch", state.epoch},
            {"own_epoch", state.own_epoch},
            {"members", state.members},
            {"distributed", std::vector<std::string>(state.distributed.begin(),
                                                     state.distributed.end())},
            {"own", state.own.has_value()
                        ? nlohmann::json::parse(state.own->serialize_state())
                        : nlohmann::json(nullptr)},
            {"peers", std::move(peers)},
        };
    }

    nlohmann::json j{
        {"version", 2},
        {"rooms", std::move(rooms)},
    };

    // The state file contains live chain keys: write it 0600, atomically.
    const std::string path = state_dir_ + "/group_state.json";
    const std::string tmp = path + ".tmp";
    try {
        std::filesystem::create_directories(state_dir_);

        std::ofstream file(tmp, std::ios::trunc);
        if (!file.is_open()) {
            return;
        }
        file << j.dump(2);
        file.close();

        std::filesystem::permissions(
            tmp,
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace);
        std::filesystem::rename(tmp, path);
    } catch (const std::exception& e) {
        LOG_WARN("Failed to save group state: {}", e.what());
    }
}

// ---------------------------------------------------------------------------
// Membership / key distribution
// ---------------------------------------------------------------------------

std::vector<protocol::Message> GroupManager::sync_room(
    const std::string& room, uint64_t epoch,
    const std::vector<std::string>& members) {
    std::lock_guard<std::mutex> lock(mutex_);

    RoomState& state = rooms_[room];
    bool rotated = false;
    bool membership_changed = false;

    // Epoch advanced (join/leave): rotate our sender key so the new/removed
    // member can't read future (respectively past) traffic.
    if (!state.own.has_value() || state.own_epoch != epoch) {
        state.own = crypto::OutboundSenderKey::generate();
        state.own_epoch = epoch;
        state.distributed.clear();
        rotated = true;
        LOG_INFO("Rotated room key for #{} (epoch {})", room, epoch);
    }

    if (state.members != members) {
        membership_changed = true;
        state.members = members;
        if (!rotated) {
            // Defensive: membership changed without an epoch bump. Make sure
            // everyone gets our key anyway.
            state.distributed.clear();
        }
    }

    state.epoch = epoch;
    prune_peer_chains(state);

    if (!rotated && !membership_changed) {
        return {};
    }

    auto envelopes = pending_envelopes(room, state);
    if (!envelopes.empty()) {
        LOG_DEBUG("Distributing room key for #{} (epoch {}) to {} member(s)",
                  room, epoch, envelopes.size());
    }
    return envelopes;
}

std::vector<std::string> GroupManager::missing_keys(const std::string& room) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> out;
    auto it = rooms_.find(room);
    if (it == rooms_.end()) {
        return out;
    }

    for (const auto& member : it->second.members) {
        if (member == username_) {
            continue;
        }
        if (!keys_.has_cached_key(member)) {
            out.push_back(member);
        }
    }
    return out;
}

std::vector<GroupManager::KeyRequest> GroupManager::outstanding_key_requests(
    size_t max_attempts) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<KeyRequest> out;
    for (const auto& [room, state] : rooms_) {
        if (!state.own.has_value()) {
            continue;
        }

        for (const auto& member : state.members) {
            if (member == username_) {
                continue;
            }
            if (keys_.has_cached_key(member)) {
                continue;
            }

            size_t attempts = 0;
            auto room_it = key_request_attempts_.find(room);
            if (room_it != key_request_attempts_.end()) {
                auto user_it = room_it->second.find(member);
                if (user_it != room_it->second.end()) {
                    attempts = user_it->second;
                }
            }
            if (attempts >= max_attempts) {
                continue;  // peer never published a key; stop asking
            }

            out.push_back(KeyRequest{room, member});
        }
    }
    return out;
}

void GroupManager::note_key_request_sent(const std::string& room,
                                         const std::string& user) {
    std::lock_guard<std::mutex> lock(mutex_);
    key_request_attempts_[room][user]++;
}

void GroupManager::note_key_available(const std::string& user) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [room, users] : key_request_attempts_) {
        users.erase(user);
    }
}

std::vector<protocol::Message> GroupManager::distribute_new_keys() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<protocol::Message> out;
    for (auto& [room, state] : rooms_) {
        if (!state.own.has_value()) {
            continue;
        }
        for (auto& env : pending_envelopes(room, state)) {
            out.push_back(std::move(env));
        }
    }
    return out;
}

void GroupManager::leave_room(const std::string& room) {
    std::lock_guard<std::mutex> lock(mutex_);
    rooms_.erase(room);
    key_request_attempts_.erase(room);
}

std::vector<protocol::Message> GroupManager::on_key_available(const std::string& user) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<protocol::Message> out;
    for (auto& [room, state] : rooms_) {
        if (!state.own.has_value()) {
            continue;
        }
        if (std::find(state.members.begin(), state.members.end(), user) ==
            state.members.end()) {
            continue;
        }
        if (state.distributed.count(user) > 0) {
            continue;
        }

        auto env = make_envelope_for(room, user);
        if (env.has_value()) {
            state.distributed.insert(user);
            out.push_back(std::move(*env));
        }
    }
    return out;
}

std::optional<protocol::Message> GroupManager::make_envelope_for(
    const std::string& room, const std::string& recipient) {
    auto state_it = rooms_.find(room);
    if (state_it == rooms_.end() || !state_it->second.own.has_value()) {
        return std::nullopt;
    }
    RoomState& state = state_it->second;

    auto recipient_key_b64 = keys_.get_cached_key(recipient);
    if (!recipient_key_b64.has_value()) {
        return std::nullopt;  // key not fetched yet; retried via on_key_available
    }
    Bytes recipient_pk = from_b64(*recipient_key_b64);
    if (recipient_pk.size() != crypto_box_PUBLICKEYBYTES) {
        return std::nullopt;
    }

    Bytes own_x_pk(keys_.x25519_public_key(),
                   keys_.x25519_public_key() + crypto_box_PUBLICKEYBYTES);
    Bytes own_x_sk(keys_.x25519_secret_key(),
                   keys_.x25519_secret_key() + crypto_box_SECRETKEYBYTES);
    Bytes own_ed_sk(keys_.ed25519_secret_key(),
                    keys_.ed25519_secret_key() + crypto_sign_SECRETKEYBYTES);

    auto wrapped = crypto::wrap_sender_key_envelope(
        room, username_, recipient, state.own_epoch, state.own->key_id(),
        state.own->chain_key(), own_x_pk, own_x_sk, own_ed_sk, recipient_pk);
    if (!wrapped.has_value()) {
        LOG_WARN("Failed to wrap room key for '{}' in #{}", recipient, room);
        return std::nullopt;
    }

    auto msg = protocol::Message::create(protocol::MessageType::ROOM_KEY,
                                         username_, room);
    msg.payload = {
        {"recipient", recipient},
        {"epoch", state.own_epoch},
        {"key_id", state.own->key_id()},
        {"nonce", wrapped->nonce_b64},
        {"box", wrapped->box_b64},
        {"signature", wrapped->signature_b64},
        {"sender_x25519", wrapped->sender_x25519_b64},
        {"sender_ed25519", wrapped->sender_ed25519_b64},
    };
    return msg;
}

std::vector<protocol::Message> GroupManager::pending_envelopes(
    const std::string& room, RoomState& state) {
    std::vector<protocol::Message> out;
    for (const auto& member : state.members) {
        if (member == username_ || state.distributed.count(member) > 0) {
            continue;
        }
        auto env = make_envelope_for(room, member);
        if (env.has_value()) {
            state.distributed.insert(member);
            out.push_back(std::move(*env));
        }
    }
    return out;
}

void GroupManager::prune_peer_chains(RoomState& state) {
    const uint64_t epoch = state.epoch;
    for (auto it = state.peers.begin(); it != state.peers.end();) {
        auto& chains = it->second;

        // Keep the current and two previous epochs so in-flight messages
        // around a rotation still decrypt; drop everything older.
        chains.erase(std::remove_if(chains.begin(), chains.end(),
                                    [epoch](const PeerChain& c) {
                                        return epoch > c.epoch + 2;
                                    }),
                     chains.end());

        while (chains.size() > MAX_CHAINS_PER_PEER) {
            chains.erase(chains.begin());
        }

        if (chains.empty()) {
            it = state.peers.erase(it);
        } else {
            ++it;
        }
    }
}

// ---------------------------------------------------------------------------
// Inbound envelopes
// ---------------------------------------------------------------------------

GroupManager::EnvelopeStatus GroupManager::process_envelope(
    const protocol::Message& msg, std::string& detail) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto& p = msg.payload;
    if (!p.is_object() ||
        !p.contains("recipient") || !p.contains("epoch") ||
        !p.contains("key_id") || !p.contains("nonce") ||
        !p.contains("box") || !p.contains("signature") ||
        !p.contains("sender_x25519") || !p.contains("sender_ed25519")) {
        detail = "malformed envelope";
        return EnvelopeStatus::INVALID;
    }

    const std::string sender = msg.from;
    if (sender.empty() || sender == username_) {
        detail = "invalid sender";
        return EnvelopeStatus::INVALID;
    }

    const std::string recipient = p.value("recipient", "");
    if (recipient != username_) {
        detail = "envelope not addressed to us";
        return EnvelopeStatus::INVALID;
    }

    const uint64_t epoch = p.value("epoch", uint64_t(0));
    const std::string key_id = p.value("key_id", "");
    const std::string nonce_b64 = p.value("nonce", "");
    const std::string box_b64 = p.value("box", "");
    const std::string sig_b64 = p.value("signature", "");
    const std::string sx_b64 = p.value("sender_x25519", "");
    const std::string se_b64 = p.value("sender_ed25519", "");

    if (epoch == 0 || !crypto::is_valid_key_id(key_id) ||
        nonce_b64.empty() || box_b64.empty() || sig_b64.empty() ||
        sx_b64.empty() || se_b64.empty()) {
        detail = "invalid envelope fields";
        return EnvelopeStatus::INVALID;
    }

    // TOFU: only unwrap with pinned keys. A changed identity is reported to
    // the user instead of silently accepted (protects against a malicious
    // server swapping keys and impersonating members).
    auto status = trust_.check_and_pin(sender, sx_b64, se_b64);
    if (status == crypto::TrustStatus::CHANGED) {
        changed_keys_[sender] = {sx_b64, se_b64};
        detail = "identity key changed for " + sender;
        return EnvelopeStatus::KEY_CHANGED;
    }

    Bytes sender_x_pk = from_b64(sx_b64);
    Bytes sender_ed_pk = from_b64(se_b64);
    if (sender_x_pk.size() != crypto_box_PUBLICKEYBYTES ||
        sender_ed_pk.size() != crypto_sign_PUBLICKEYBYTES) {
        detail = "invalid sender keys";
        return EnvelopeStatus::INVALID;
    }

    Bytes own_x_sk(keys_.x25519_secret_key(),
                   keys_.x25519_secret_key() + crypto_box_SECRETKEYBYTES);

    crypto::WrappedEnvelope wrapped{nonce_b64, box_b64, sig_b64, sx_b64, se_b64};
    auto env = crypto::unwrap_sender_key_envelope(
        msg.room, sender, username_, epoch, key_id, wrapped,
        own_x_sk, sender_x_pk, sender_ed_pk);
    if (!env.has_value()) {
        detail = "envelope signature or decryption failed";
        return EnvelopeStatus::INVALID;
    }

    RoomState& state = rooms_[msg.room];
    auto& chains = state.peers[sender];

    for (const auto& c : chains) {
        if (c.epoch == epoch && c.key_id == key_id) {
            detail = "duplicate envelope";
            return EnvelopeStatus::OK;
        }
    }

    while (chains.size() >= MAX_CHAINS_PER_PEER) {
        chains.erase(chains.begin());  // drop the oldest chains
    }
    chains.push_back(PeerChain{epoch, key_id, crypto::InboundSenderKey(env->chain_key)});

    // Cache the (trusted) X25519 key for DMs and future envelopes.
    keys_.cache_key(sender, sx_b64);

    detail.clear();
    return EnvelopeStatus::OK;
}

// ---------------------------------------------------------------------------
// Seal / open room messages
// ---------------------------------------------------------------------------

std::optional<nlohmann::json> GroupManager::seal(const std::string& room,
                                                 const std::string& message_id,
                                                 const std::string& plaintext,
                                                 std::string& err) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = rooms_.find(room);
    if (it == rooms_.end() || !it->second.own.has_value()) {
        err = "no room key yet";
        return std::nullopt;
    }

    RoomState& state = it->second;
    if (state.own_epoch != state.epoch) {
        err = "room key out of date";
        return std::nullopt;
    }

    Bytes own_ed_sk(keys_.ed25519_secret_key(),
                    keys_.ed25519_secret_key() + crypto_sign_SECRETKEYBYTES);

    auto sealed = crypto::seal_room_message(*state.own, state.epoch, room,
                                            username_, plaintext, own_ed_sk);
    if (!sealed.has_value()) {
        err = "encryption failed";
        return std::nullopt;
    }

    if (!message_id.empty()) {
        own_echo_[message_id] = plaintext;
        own_echo_order_.push_back(message_id);
        while (own_echo_order_.size() > MAX_OWN_ECHO) {
            own_echo_.erase(own_echo_order_.front());
            own_echo_order_.pop_front();
        }
    }

    return nlohmann::json{
        {"epoch", sealed->epoch},
        {"key_id", sealed->key_id},
        {"index", sealed->index},
        {"nonce", sealed->nonce_b64},
        {"ciphertext", sealed->ciphertext_b64},
        {"signature", sealed->signature_b64},
    };
}

std::optional<std::string> GroupManager::open(const std::string& room,
                                              const std::string& sender,
                                              const nlohmann::json& payload,
                                              std::string& err) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!payload.is_object() ||
        !payload.contains("epoch") || !payload.contains("key_id") ||
        !payload.contains("index") || !payload.contains("nonce") ||
        !payload.contains("ciphertext") || !payload.contains("signature")) {
        err = "malformed message";
        return std::nullopt;
    }

    const uint64_t epoch = payload.value("epoch", uint64_t(0));
    const std::string key_id = payload.value("key_id", "");
    const int64_t index = payload.value("index", int64_t(-1));
    if (epoch == 0 || index < 0 || !crypto::is_valid_key_id(key_id)) {
        err = "malformed message metadata";
        return std::nullopt;
    }

    auto it = rooms_.find(room);
    if (it == rooms_.end()) {
        err = "no room key";
        return std::nullopt;
    }

    auto pit = it->second.peers.find(sender);
    if (pit == it->second.peers.end()) {
        err = "no sender key";
        return std::nullopt;
    }

    PeerChain* chain = nullptr;
    for (auto& c : pit->second) {
        if (c.epoch == epoch && c.key_id == key_id) {
            chain = &c;
            break;
        }
    }
    if (chain == nullptr) {
        err = "no sender key for this epoch";
        return std::nullopt;
    }

    // Signature must verify against the pinned Ed25519 key, never the wire
    // copy.
    auto pin = trust_.get(sender);
    if (!pin.has_value() || pin->ed25519_b64.empty()) {
        err = "sender identity not pinned";
        return std::nullopt;
    }
    Bytes sender_ed_pk = from_b64(pin->ed25519_b64);
    if (sender_ed_pk.size() != crypto_sign_PUBLICKEYBYTES) {
        err = "bad sender identity";
        return std::nullopt;
    }

    crypto::SealedRoomMessage sealed;
    sealed.epoch = epoch;
    sealed.key_id = key_id;
    sealed.index = static_cast<uint32_t>(index);
    sealed.nonce_b64 = payload.value("nonce", "");
    sealed.ciphertext_b64 = payload.value("ciphertext", "");
    sealed.signature_b64 = payload.value("signature", "");

    auto plaintext = crypto::open_room_message(chain->chain, sealed, room,
                                               sender, sender_ed_pk);
    if (!plaintext.has_value()) {
        err = "decryption failed";
        return std::nullopt;
    }
    return plaintext;
}

std::optional<std::string> GroupManager::take_own_message(
    const std::string& message_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = own_echo_.find(message_id);
    if (it == own_echo_.end()) {
        return std::nullopt;
    }
    std::string plaintext = it->second;
    own_echo_.erase(it);
    return plaintext;
}

// ---------------------------------------------------------------------------
// Trust helpers
// ---------------------------------------------------------------------------

chat::crypto::TrustStatus GroupManager::check_dm_key(
    const std::string& user, const std::string& x25519_b64) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto status = trust_.check_and_pin(user, x25519_b64, "");
    if (status == crypto::TrustStatus::CHANGED) {
        changed_keys_[user] = {x25519_b64, ""};
    }
    return status;
}

std::optional<GroupManager::PeerInfo> GroupManager::peer_info(
    const std::string& user) const {
    std::lock_guard<std::mutex> lock(mutex_);

    PeerInfo info;
    info.user = user;
    info.pending_change = trust_.has_pending_change(user);

    auto pin = trust_.get(user);
    if (!pin.has_value()) {
        return info;  // pin_known = false
    }

    info.pin_known = true;
    info.x25519_b64 = pin->x25519_b64;
    info.ed25519_b64 = pin->ed25519_b64;
    info.verified = pin->verified;

    Bytes x_pk = from_b64(pin->x25519_b64);
    Bytes ed_pk = from_b64(pin->ed25519_b64);
    if (!x_pk.empty()) {
        info.fingerprint = crypto::identity_fingerprint(x_pk, ed_pk);
    }
    return info;
}

bool GroupManager::accept_peer_key(const std::string& user) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = changed_keys_.find(user);
    if (it == changed_keys_.end()) {
        return false;
    }

    bool ok = trust_.accept_new_key(user, it->second.first, it->second.second);
    if (ok) {
        changed_keys_.erase(it);
    }
    return ok;
}

bool GroupManager::mark_peer_verified(const std::string& user, bool verified) {
    std::lock_guard<std::mutex> lock(mutex_);
    return trust_.mark_verified(user, verified);
}

std::optional<GroupManager::RoomSummary> GroupManager::room_summary(
    const std::string& room) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = rooms_.find(room);
    if (it == rooms_.end()) {
        return std::nullopt;
    }
    const RoomState& state = it->second;

    RoomSummary summary;
    summary.room = room;
    summary.epoch = state.epoch;
    summary.own_key_set = state.own.has_value() && state.own_epoch == state.epoch;
    if (state.own.has_value()) {
        summary.own_key_id = state.own->key_id();
    }

    for (const auto& member : state.members) {
        if (member == username_) {
            continue;
        }

        bool has_current_key = false;
        auto pit = state.peers.find(member);
        if (pit != state.peers.end()) {
            for (const auto& c : pit->second) {
                if (c.epoch == state.epoch) {
                    has_current_key = true;
                    break;
                }
            }
        }

        if (has_current_key) {
            summary.peers_with_keys.push_back(member);
        } else {
            summary.members_without_keys.push_back(member);
        }
    }

    return summary;
}

std::vector<std::string> GroupManager::rooms() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> out;
    out.reserve(rooms_.size());
    for (const auto& [room, _] : rooms_) {
        out.push_back(room);
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace chat::client
