#pragma once

// Room (group) end-to-end encryption primitives.
//
// Design: "Sender Keys" with a symmetric ratchet (Signal-style):
//   - Every room member owns one sender chain per room epoch.
//   - The chain key is distributed pairwise, wrapped with the member's
//     X25519 identity key (crypto_box) and signed with their Ed25519 key.
//   - Each room message uses XChaCha20-Poly1305 under a one-time message
//     key derived from the chain; the chain then ratchets forward, so a
//     stolen chain key cannot reopen messages that were already used.
//   - Room, sender, epoch, key id and message index are bound into the
//     AEAD associated data, and the whole payload is Ed25519-signed, so
//     the relay (server) cannot reorder, replay or splice messages.
//
// Nothing in this file talks to the network or the UI: it is pure crypto
// state so it can be unit tested in isolation (see tests/).

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace chat::crypto {

using Bytes = std::vector<unsigned char>;

inline constexpr size_t ROOM_KEY_BYTES = 32;
// Maximum number of out-of-order message keys we are willing to cache for
// a single sender chain before refusing to jump further ahead.
inline constexpr size_t MAX_SKIPPED_MESSAGE_KEYS = 200;

// ---------------------------------------------------------------------------
// Encoding helpers
// ---------------------------------------------------------------------------

std::string to_b64(const unsigned char* data, size_t len);
std::string to_b64(const Bytes& data);

// Returns an empty vector when the input is not valid base64.
Bytes from_b64(const std::string& b64);

// Random 4-byte key id, hex encoded (8 chars).
std::string new_key_id();

// Whether a string is a valid 8-char hex key id.
bool is_valid_key_id(const std::string& key_id);

// ---------------------------------------------------------------------------
// Ratchet
// ---------------------------------------------------------------------------

struct RatchetStep {
    Bytes message_key;  // 32 bytes, one-time AEAD key
    Bytes next_chain;   // 32 bytes, next chain key
};

// BLAKE2b-256 chain KDF:
//   message_key = H(0x01 || chain_key)
//   next_chain  = H(0x02 || chain_key)
RatchetStep derive_ratchet_step(const Bytes& chain_key);

// ---------------------------------------------------------------------------
// Identity fingerprints (for out-of-band verification UX)
// ---------------------------------------------------------------------------

// Human-comparable fingerprint of a user's long-term keys:
// BLAKE2b-128(x25519 || ed25519) rendered as hex groups.
std::string identity_fingerprint(const Bytes& x25519_pk, const Bytes& ed25519_pk);

// ---------------------------------------------------------------------------
// AAD + signing inputs
// ---------------------------------------------------------------------------

// Canonical associated-data string for a room message. Room names and
// usernames are restricted to [a-zA-Z0-9_-] by the server, so plain
// concatenation with '|' separators is unambiguous.
std::string build_message_aad(const std::string& room, const std::string& sender,
                              uint64_t epoch, const std::string& key_id,
                              uint32_t index);

// ---------------------------------------------------------------------------
// Outbound sender chain (our own key for one room epoch)
// ---------------------------------------------------------------------------

class OutboundSenderKey {
public:
    // Generates a fresh random chain + key id.
    OutboundSenderKey();
    static OutboundSenderKey generate();

    static std::optional<OutboundSenderKey> from_chain(const Bytes& chain,
                                                       uint32_t index,
                                                       const std::string& key_id);

    const std::string& key_id() const { return key_id_; }
    uint32_t next_index() const { return index_; }
    const Bytes& chain_key() const { return chain_; }

    // Move the chain to the next state (called after a successful seal).
    void advance(const Bytes& next_chain);

    // Persistence (contains the raw chain key material).
    std::string serialize_state() const;
    static std::optional<OutboundSenderKey> deserialize_state(const std::string& json);

private:
    Bytes chain_;
    uint32_t index_ = 0;
    std::string key_id_;
};

struct SealedRoomMessage {
    uint64_t epoch = 0;
    std::string key_id;
    uint32_t index = 0;
    std::string nonce_b64;
    std::string ciphertext_b64;
    std::string signature_b64;
};

// Encrypt + sign one room message; advances the sender chain on success.
std::optional<SealedRoomMessage> seal_room_message(OutboundSenderKey& out,
                                                   uint64_t epoch,
                                                   const std::string& room,
                                                   const std::string& sender,
                                                   const std::string& plaintext,
                                                   const Bytes& sender_ed25519_sk);

// ---------------------------------------------------------------------------
// Inbound sender chain (a peer's chain for one room epoch)
// ---------------------------------------------------------------------------

class InboundSenderKey {
public:
    explicit InboundSenderKey(const Bytes& chain_key);

    // Whether take_message_key() would succeed (does not consume).
    bool has_index(uint32_t index) const;

    // One-time removal of the message key for `index`:
    //  - indexes ahead of the chain advance it and cache skipped keys,
    //  - indexes already cached are returned once and erased (replay safe),
    //  - anything too old / replayed / too far ahead returns nullopt.
    std::optional<Bytes> take_message_key(uint32_t index);

    // Snapshot + restore allow the caller to roll back a failed open
    // (e.g. forged ciphertext) without burning a legitimate chain step.
    InboundSenderKey snapshot() const;
    void restore(const InboundSenderKey& other);

    uint32_t next_index() const { return next_; }
    const Bytes& chain_key() const { return chain_; }

    std::string serialize_state() const;
    static std::optional<InboundSenderKey> deserialize_state(const std::string& json);

private:
    Bytes chain_;
    uint32_t next_ = 0;
    std::map<uint32_t, Bytes> skipped_;
};

// Verify + decrypt a room message. Returns the plaintext or nullopt.
// On failure the chain state is left unchanged (including on MAC failure).
std::optional<std::string> open_room_message(InboundSenderKey& in,
                                             const SealedRoomMessage& sealed,
                                             const std::string& room,
                                             const std::string& sender,
                                             const Bytes& sender_ed25519_pk);

// ---------------------------------------------------------------------------
// Sender key envelopes (pairwise distribution of a room chain key)
// ---------------------------------------------------------------------------

struct WrappedEnvelope {
    std::string nonce_b64;
    std::string box_b64;
    std::string signature_b64;
    std::string sender_x25519_b64;
    std::string sender_ed25519_b64;
};

struct SenderKeyEnvelope {
    std::string room;
    uint64_t epoch = 0;
    std::string key_id;
    Bytes chain_key;
    std::string sender_x25519_b64;
    std::string sender_ed25519_b64;
};

// Wrap `chain_key` for `recipient`, encrypted with their X25519 key and
// signed with our Ed25519 key.
std::optional<WrappedEnvelope> wrap_sender_key_envelope(
    const std::string& room,
    const std::string& sender,
    const std::string& recipient,
    uint64_t epoch,
    const std::string& key_id,
    const Bytes& chain_key,
    const Bytes& sender_x25519_pk,
    const Bytes& sender_x25519_sk,
    const Bytes& sender_ed25519_sk,
    const Bytes& recipient_x25519_pk);

// Unwrap an envelope addressed to us. `sender_x25519_pk` / `sender_ed25519_pk`
// must be the caller's *trusted* copy of the sender's keys (after TOFU pinning);
// the signature and box are verified against those, not the wire values.
std::optional<SenderKeyEnvelope> unwrap_sender_key_envelope(
    const std::string& room,
    const std::string& sender,
    const std::string& recipient,
    uint64_t expected_epoch,
    const std::string& expected_key_id,
    const WrappedEnvelope& wrapped,
    const Bytes& own_x25519_sk,
    const Bytes& sender_x25519_pk,
    const Bytes& sender_ed25519_pk);

// ---------------------------------------------------------------------------
// Hygiene
// ---------------------------------------------------------------------------

void secure_wipe(Bytes& b);
void secure_wipe_str(std::string& s);

} // namespace chat::crypto
