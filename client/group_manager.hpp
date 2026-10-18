#pragma once

// Client-side room E2EE orchestration.
//
// Owns per-room encryption state: our sender key for the current epoch,
// peers' sender chains, TOFU trust pins, and persistence. The UI layer
// feeds it room membership events and inbound envelope/message frames,
// and asks it to seal outgoing room messages.

#include "crypto.hpp"
#include "crypto/room_crypto.hpp"
#include "crypto/trust_store.hpp"
#include "protocol/message.hpp"

#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace chat::client {

// A peer's sender chain for one room epoch.
struct PeerChain {
    uint64_t epoch = 0;
    std::string key_id;
    chat::crypto::InboundSenderKey chain;

    PeerChain()
        : chain(chat::crypto::Bytes(chat::crypto::ROOM_KEY_BYTES, 0)) {}

    PeerChain(uint64_t e, std::string k, chat::crypto::InboundSenderKey c)
        : epoch(e), key_id(std::move(k)), chain(std::move(c)) {}
};

struct RoomState {
    uint64_t epoch = 0;
    std::vector<std::string> members;
    std::optional<chat::crypto::OutboundSenderKey> own;
    uint64_t own_epoch = 0;              // epoch our sender key belongs to
    std::set<std::string> distributed;   // members we sent our key to (this epoch)
    std::unordered_map<std::string, std::vector<PeerChain>> peers;
};

class GroupManager {
public:
    GroupManager(KeyManager& keys, std::string state_dir);

    void set_username(const std::string& username);
    const std::string& username() const { return username_; }

    // Persistence of trust pins and group state (0600 on the state file).
    bool load();
    void save() const;

    // Membership sync: (re)initialize room epoch + member list. Rotates our
    // sender key when the epoch advances. Returns any ROOM_KEY envelopes
    // that should be sent now.
    std::vector<protocol::Message> sync_room(const std::string& room,
                                             uint64_t epoch,
                                             const std::vector<std::string>& members);

    // Members of `room` whose X25519 key we don't have cached yet.
    std::vector<std::string> missing_keys(const std::string& room) const;

    // ---- Missing key requests -------------------------------------------
    //
    // A peer may publish its X25519 key just after we asked for it (login
    // races), so the UI keeps asking with a bounded number of attempts.

    struct KeyRequest {
        std::string room;
        std::string user;
    };

    // Members of known rooms whose key is still missing. Attempt counts are
    // tracked here; `max_attempts` caps them so a peer that never publishes
    // a key does not cause endless requests.
    std::vector<KeyRequest> outstanding_key_requests(size_t max_attempts = 5) const;

    // Record that a key request was sent for `user` in `room`.
    void note_key_request_sent(const std::string& room, const std::string& user);

    // Clear attempt counters for `user` (key arrived or no longer needed).
    void note_key_available(const std::string& user);

    // Called after a KEY_EXCHANGE key_response for `user`; returns any
    // envelopes that were waiting on that key.
    std::vector<protocol::Message> on_key_available(const std::string& user);

    // Produce sender-key envelopes for every member we can encrypt to but
    // haven't distributed to yet. Covers keys learned outside a
    // key_response (e.g. from a member's own envelope), so delivery is
    // self-healing rather than dependent on one code path.
    std::vector<protocol::Message> distribute_new_keys();

    // Drop all state for a room we have left.
    void leave_room(const std::string& room);

    enum class EnvelopeStatus { OK, KEY_CHANGED, TRUST_REJECTED, INVALID };

    // Process an inbound ROOM_KEY envelope addressed to us.
    EnvelopeStatus process_envelope(const protocol::Message& msg, std::string& detail);

    // Seal a room message. Returns the payload for an ENCRYPTED_MESSAGE and
    // remembers the plaintext under `message_id` for our own server echo.
    std::optional<nlohmann::json> seal(const std::string& room,
                                       const std::string& message_id,
                                       const std::string& plaintext,
                                       std::string& err);

    // Try to open an inbound ENCRYPTED_MESSAGE payload.
    std::optional<std::string> open(const std::string& room,
                                    const std::string& sender,
                                    const nlohmann::json& payload,
                                    std::string& err);

    // Sender echo: consume the remembered plaintext for a message we sent.
    std::optional<std::string> take_own_message(const std::string& message_id);

    // DM key trust check (TOFU over the X25519 key; Ed25519 may be unknown).
    chat::crypto::TrustStatus check_dm_key(const std::string& user,
                                           const std::string& x25519_b64);

    // ---- /verify + /roomkeys support ------------------------------------

    struct PeerInfo {
        std::string user;
        std::string fingerprint;     // empty when no pin yet
        std::string x25519_b64;
        std::string ed25519_b64;
        bool pin_known = false;
        bool verified = false;
        bool pending_change = false;
    };

    std::optional<PeerInfo> peer_info(const std::string& user) const;

    // Accept the most recently seen (changed) key for `user`.
    bool accept_peer_key(const std::string& user);

    // Mark a peer as out-of-band verified (fingerprint compared manually).
    bool mark_peer_verified(const std::string& user, bool verified);

    struct RoomSummary {
        std::string room;
        uint64_t epoch = 0;
        bool own_key_set = false;
        std::string own_key_id;
        std::vector<std::string> peers_with_keys;
        std::vector<std::string> members_without_keys;
    };

    std::optional<RoomSummary> room_summary(const std::string& room) const;
    std::vector<std::string> rooms() const;

private:
    // Builds a ROOM_KEY envelope for `recipient` if we can (key cached).
    std::optional<protocol::Message> make_envelope_for(const std::string& room,
                                                       const std::string& recipient);

    // Envelopes we still owe to members of this room (for the current epoch).
    std::vector<protocol::Message> pending_envelopes(const std::string& room,
                                                     RoomState& state);

    void prune_peer_chains(RoomState& state);

    KeyManager& keys_;
    chat::crypto::TrustStore trust_;
    std::string state_dir_;
    std::string username_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, RoomState> rooms_;

    // room -> user -> attempts, for missing-key retries (session only).
    std::map<std::string, std::map<std::string, size_t>> key_request_attempts_;

    // Most recently seen keys for users whose pin changed (for /verify accept).
    std::unordered_map<std::string, std::pair<std::string, std::string>> changed_keys_;

    // Plaintext of messages we sent, keyed by message id (bounded).
    std::map<std::string, std::string> own_echo_;
    std::deque<std::string> own_echo_order_;
    static constexpr size_t MAX_OWN_ECHO = 200;

    // Cap on remembered chains per peer.
    static constexpr size_t MAX_CHAINS_PER_PEER = 6;
};

} // namespace chat::client
