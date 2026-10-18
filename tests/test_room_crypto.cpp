// Unit tests for room (group) E2EE primitives.

#include "crypto/room_crypto.hpp"
#include "test_framework.hpp"

#include <sodium.h>

#include <cstring>

using namespace chat::crypto;

namespace {

struct Identity {
    Bytes x_pk, x_sk, ed_pk, ed_sk;
};

Identity make_identity() {
    if (sodium_init() < 0) {
        throw std::runtime_error("sodium_init failed");
    }
    unsigned char xpk[crypto_box_PUBLICKEYBYTES];
    unsigned char xsk[crypto_box_SECRETKEYBYTES];
    unsigned char edpk[crypto_sign_PUBLICKEYBYTES];
    unsigned char edsk[crypto_sign_SECRETKEYBYTES];
    crypto_box_keypair(xpk, xsk);
    crypto_sign_keypair(edpk, edsk);
    return Identity{
        Bytes(xpk, xpk + sizeof(xpk)),
        Bytes(xsk, xsk + sizeof(xsk)),
        Bytes(edpk, edpk + sizeof(edpk)),
        Bytes(edsk, edsk + sizeof(edsk)),
    };
}

constexpr const char* ROOM = "general";
constexpr const char* ALICE = "alice";
constexpr const char* BOB = "bob";
constexpr uint64_t EPOCH = 3;

// Convenience: seal `n` messages from the same chain, returning them all.
std::vector<SealedRoomMessage> seal_n(OutboundSenderKey& out, const Identity& sender,
                                      int n) {
    std::vector<SealedRoomMessage> out_msgs;
    for (int i = 0; i < n; ++i) {
        auto sealed = seal_room_message(out, EPOCH, ROOM, ALICE,
                                        "message-" + std::to_string(i), sender.ed_sk);
        if (!sealed.has_value()) {
            throw std::runtime_error("seal failed");
        }
        out_msgs.push_back(*sealed);
    }
    return out_msgs;
}

} // namespace

// ---------------------------------------------------------------------------
// Encodings
// ---------------------------------------------------------------------------

TEST(b64_roundtrip) {
    Bytes data(64);
    randombytes_buf(data.data(), data.size());
    Bytes back = from_b64(to_b64(data));
    CHECK(back == data);

    CHECK_EQ(to_b64(Bytes{}), "");
    CHECK(from_b64("").empty());
    CHECK(from_b64("!!!not-base64!!!").empty());
    CHECK(from_b64("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA").empty() == false);
}

TEST(key_ids) {
    std::string id = new_key_id();
    CHECK_EQ(id.size(), size_t(8));
    CHECK(is_valid_key_id(id));
    CHECK_FALSE(is_valid_key_id("XYZ"));
    CHECK_FALSE(is_valid_key_id("abcdefg"));       // 7 chars
    CHECK_FALSE(is_valid_key_id("ABCDEF12"));      // uppercase not valid
    CHECK(is_valid_key_id("0123abcd"));
}

// ---------------------------------------------------------------------------
// Ratchet
// ---------------------------------------------------------------------------

TEST(ratchet_deterministic_and_forward) {
    Bytes chain(ROOM_KEY_BYTES);
    randombytes_buf(chain.data(), chain.size());

    auto a = derive_ratchet_step(chain);
    auto b = derive_ratchet_step(chain);
    CHECK_EQ(a.message_key, b.message_key);
    CHECK_EQ(a.next_chain, b.next_chain);
    CHECK(a.message_key != chain);
    CHECK(a.next_chain != chain);
    CHECK(a.message_key != a.next_chain);
    CHECK_EQ(a.message_key.size(), size_t(32));

    auto c = derive_ratchet_step(a.next_chain);
    CHECK(c.message_key != a.message_key);

    // Wrong-size inputs yield empty output (fail closed).
    auto bad = derive_ratchet_step(Bytes(8, 0x42));
    CHECK(bad.message_key.empty());
}

TEST(identity_fingerprint_is_stable_and_sensitive) {
    auto alice = make_identity();
    auto bob = make_identity();

    std::string fp1 = identity_fingerprint(alice.x_pk, alice.ed_pk);
    std::string fp2 = identity_fingerprint(alice.x_pk, alice.ed_pk);
    CHECK_EQ(fp1, fp2);
    CHECK_EQ(fp1.size(), size_t(39));  // 32 hex chars + 7 spaces

    CHECK(identity_fingerprint(bob.x_pk, bob.ed_pk) != fp1);
    CHECK(identity_fingerprint(alice.x_pk, bob.ed_pk) != fp1);
}

// ---------------------------------------------------------------------------
// Message seal / open
// ---------------------------------------------------------------------------

TEST(seal_open_roundtrip_in_order) {
    auto alice = make_identity();
    auto bob = make_identity();

    OutboundSenderKey out = OutboundSenderKey::generate();
    InboundSenderKey in(out.chain_key());

    auto sealed = seal_n(out, alice, 3);
    for (int i = 0; i < 3; ++i) {
        auto pt = open_room_message(in, sealed[i], ROOM, ALICE, alice.ed_pk);
        CHECK(pt.has_value());
        CHECK_EQ(pt.value(), "message-" + std::to_string(i));
    }
}

TEST(seal_open_out_of_order) {
    auto alice = make_identity();
    OutboundSenderKey out = OutboundSenderKey::generate();
    InboundSenderKey in(out.chain_key());

    auto sealed = seal_n(out, alice, 5);

    // Open 3 first (caches 0..2), then 1, then 4, then 0.
    auto m3 = open_room_message(in, sealed[3], ROOM, ALICE, alice.ed_pk);
    CHECK(m3.has_value());
    CHECK_EQ(m3.value(), "message-3");

    auto m1 = open_room_message(in, sealed[1], ROOM, ALICE, alice.ed_pk);
    CHECK(m1.has_value());
    CHECK_EQ(m1.value(), "message-1");

    auto m4 = open_room_message(in, sealed[4], ROOM, ALICE, alice.ed_pk);
    CHECK(m4.has_value());

    auto m0 = open_room_message(in, sealed[0], ROOM, ALICE, alice.ed_pk);
    CHECK(m0.has_value());
    CHECK_EQ(m0.value(), "message-0");
}

TEST(replay_rejected) {
    auto alice = make_identity();
    OutboundSenderKey out = OutboundSenderKey::generate();
    InboundSenderKey in(out.chain_key());

    auto sealed = seal_n(out, alice, 3);

    CHECK(open_room_message(in, sealed[2], ROOM, ALICE, alice.ed_pk).has_value());
    CHECK(open_room_message(in, sealed[0], ROOM, ALICE, alice.ed_pk).has_value());

    // Replays: both the directly-consumed and the cached key are one-time.
    CHECK_FALSE(open_room_message(in, sealed[2], ROOM, ALICE, alice.ed_pk).has_value());
    CHECK_FALSE(open_room_message(in, sealed[0], ROOM, ALICE, alice.ed_pk).has_value());
}

TEST(tampered_ciphertext_rejected_without_burning_chain) {
    auto alice = make_identity();
    OutboundSenderKey out = OutboundSenderKey::generate();
    InboundSenderKey in(out.chain_key());

    auto sealed = seal_n(out, alice, 1);

    SealedRoomMessage tampered = sealed[0];
    tampered.ciphertext_b64[0] =
        (tampered.ciphertext_b64[0] == 'A') ? 'B' : 'A';
    CHECK_FALSE(open_room_message(in, tampered, ROOM, ALICE, alice.ed_pk).has_value());

    // The legitimate message must still open afterwards.
    auto pt = open_room_message(in, sealed[0], ROOM, ALICE, alice.ed_pk);
    CHECK(pt.has_value());
    CHECK_EQ(pt.value(), "message-0");
}

TEST(wrong_room_or_sender_rejected) {
    auto alice = make_identity();
    OutboundSenderKey out = OutboundSenderKey::generate();
    InboundSenderKey in(out.chain_key());
    auto sealed = seal_n(out, alice, 1);

    // Different room -> AAD mismatch -> signature failure.
    CHECK_FALSE(open_room_message(in, sealed[0], "other_room", ALICE, alice.ed_pk).has_value());
    // Different claimed sender -> signature failure.
    CHECK_FALSE(open_room_message(in, sealed[0], ROOM, "mallory", alice.ed_pk).has_value());
    // Wrong signer key -> signature failure.
    auto mallory = make_identity();
    CHECK_FALSE(open_room_message(in, sealed[0], ROOM, ALICE, mallory.ed_pk).has_value());

    // All the forgeries failed without consuming the legitimate key.
    CHECK(open_room_message(in, sealed[0], ROOM, ALICE, alice.ed_pk).has_value());
}

TEST(refuses_to_jump_far_ahead) {
    auto alice = make_identity();
    OutboundSenderKey out = OutboundSenderKey::generate();
    InboundSenderKey in(out.chain_key());

    auto sealed = seal_n(out, alice,
                         static_cast<int>(MAX_SKIPPED_MESSAGE_KEYS) + 10);

    // Far beyond the skip window -> reject without mutating state.
    CHECK_FALSE(open_room_message(in,
                                  sealed[MAX_SKIPPED_MESSAGE_KEYS + 5],
                                  ROOM, ALICE, alice.ed_pk)
                    .has_value());

    // Within the window still works.
    CHECK(open_room_message(in, sealed[MAX_SKIPPED_MESSAGE_KEYS],
                            ROOM, ALICE, alice.ed_pk)
              .has_value());
}

// ---------------------------------------------------------------------------
// Envelopes
// ---------------------------------------------------------------------------

TEST(envelope_roundtrip) {
    auto alice = make_identity();
    auto bob = make_identity();

    OutboundSenderKey out = OutboundSenderKey::generate();

    auto wrapped = wrap_sender_key_envelope(
        ROOM, ALICE, BOB, EPOCH, out.key_id(), out.chain_key(),
        alice.x_pk, alice.x_sk, alice.ed_sk, bob.x_pk);
    CHECK(wrapped.has_value());

    auto env = unwrap_sender_key_envelope(
        ROOM, ALICE, BOB, EPOCH, out.key_id(), wrapped.value(),
        bob.x_sk, alice.x_pk, alice.ed_pk);
    CHECK(env.has_value());
    CHECK_EQ(env->room, std::string(ROOM));
    CHECK_EQ(env->epoch, EPOCH);
    CHECK_EQ(env->key_id, out.key_id());
    CHECK_EQ(env->chain_key, out.chain_key());
}

TEST(envelope_tampering_rejected) {
    auto alice = make_identity();
    auto bob = make_identity();
    auto mallory = make_identity();

    OutboundSenderKey out = OutboundSenderKey::generate();

    auto wrapped = wrap_sender_key_envelope(
        ROOM, ALICE, BOB, EPOCH, out.key_id(), out.chain_key(),
        alice.x_pk, alice.x_sk, alice.ed_sk, bob.x_pk);
    CHECK(wrapped.has_value());

    // Tampered ciphertext.
    WrappedEnvelope tampered = wrapped.value();
    tampered.box_b64[0] = (tampered.box_b64[0] == 'A') ? 'B' : 'A';
    CHECK_FALSE(unwrap_sender_key_envelope(ROOM, ALICE, BOB, EPOCH, out.key_id(),
                                           tampered, bob.x_sk, alice.x_pk, alice.ed_pk)
                    .has_value());

    // Wrong recipient secret key.
    CHECK_FALSE(unwrap_sender_key_envelope(ROOM, ALICE, BOB, EPOCH, out.key_id(),
                                           wrapped.value(), mallory.x_sk,
                                           alice.x_pk, alice.ed_pk)
                    .has_value());

    // Wrong pinned sender key (e.g. server swapped identity).
    CHECK_FALSE(unwrap_sender_key_envelope(ROOM, ALICE, BOB, EPOCH, out.key_id(),
                                           wrapped.value(), bob.x_sk,
                                           mallory.x_pk, mallory.ed_pk)
                    .has_value());

    // Wrong epoch (replayed old envelope).
    CHECK_FALSE(unwrap_sender_key_envelope(ROOM, ALICE, BOB, EPOCH + 1, out.key_id(),
                                           wrapped.value(), bob.x_sk,
                                           alice.x_pk, alice.ed_pk)
                    .has_value());

    // Wrong key id.
    CHECK_FALSE(unwrap_sender_key_envelope(ROOM, ALICE, BOB, EPOCH, "00000000",
                                           wrapped.value(), bob.x_sk,
                                           alice.x_pk, alice.ed_pk)
                    .has_value());
}

// ---------------------------------------------------------------------------
// Persistence
// ---------------------------------------------------------------------------

TEST(outbound_state_roundtrip) {
    OutboundSenderKey out = OutboundSenderKey::generate();

    auto restored = OutboundSenderKey::deserialize_state(out.serialize_state());
    CHECK(restored.has_value());
    CHECK_EQ(restored->key_id(), out.key_id());
    CHECK_EQ(restored->chain_key(), out.chain_key());
    CHECK_EQ(restored->next_index(), out.next_index());

    CHECK_FALSE(OutboundSenderKey::deserialize_state("not json").has_value());
    CHECK_FALSE(OutboundSenderKey::deserialize_state("{}").has_value());
}

TEST(inbound_state_roundtrip_preserves_skipped_keys) {
    auto alice = make_identity();
    OutboundSenderKey out = OutboundSenderKey::generate();
    InboundSenderKey in(out.chain_key());

    auto sealed = seal_n(out, alice, 4);
    CHECK(open_room_message(in, sealed[2], ROOM, ALICE, alice.ed_pk).has_value());

    auto restored = InboundSenderKey::deserialize_state(in.serialize_state());
    CHECK(restored.has_value());

    // Skipped keys survive the roundtrip: 0 and 1 still openable.
    CHECK(open_room_message(*restored, sealed[1], ROOM, ALICE, alice.ed_pk).has_value());
    CHECK_FALSE(open_room_message(*restored, sealed[1], ROOM, ALICE, alice.ed_pk).has_value());
    CHECK(open_room_message(*restored, sealed[0], ROOM, ALICE, alice.ed_pk).has_value());
    CHECK(open_room_message(*restored, sealed[3], ROOM, ALICE, alice.ed_pk).has_value());
}

TEST(wipe_clears_buffers) {
    Bytes b(32, 0x42);
    secure_wipe(b);
    CHECK(b.empty());

    std::string s = "secret";
    secure_wipe_str(s);
    CHECK(s.empty());
}
