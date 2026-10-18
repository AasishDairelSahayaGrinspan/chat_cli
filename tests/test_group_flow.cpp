// End-to-end style tests for the client-side room E2EE orchestration.
//
// These tests simulate the server relay in-process: two or three
// GroupManager instances (one per user) exchange the same ROOM_KEY /
// ENCRYPTED_MESSAGE frames the real server would relay, using real
// libsodium keys on disk in temporary directories.

#include "group_manager.hpp"
#include "crypto.hpp"
#include "crypto/room_crypto.hpp"
#include "protocol/message.hpp"
#include "test_framework.hpp"

#include <filesystem>
#include <map>
#include <memory>
#include <random>
#include <vector>

using namespace chat;
using namespace chat::client;

namespace {

std::filesystem::path make_temp_dir(const std::string& tag) {
    auto dir = std::filesystem::temp_directory_path() /
               ("chat_cli_flow_" + tag + "_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(dir);
    return dir;
}

struct User {
    std::string name;
    std::filesystem::path base;
    std::unique_ptr<KeyManager> keys;
    std::unique_ptr<GroupManager> group;

    explicit User(const std::string& n) : name(n) {
        base = make_temp_dir(n);
        auto key_dir = (base / "keys").string();

        keys = std::make_unique<KeyManager>();
        keys->load_or_create(key_dir);

        group = std::make_unique<GroupManager>(*keys, key_dir);
        group->set_username(n);
        group->load();
    }

    std::string pub() const { return keys->get_public_key(); }
};

using Envelopes = std::vector<std::pair<std::string, protocol::Message>>;

// Client-side key fetch: ask the (simulated) server for `target`'s key and
// hand the response to the client, which caches it, checks TOFU and releases
// any envelopes that were waiting on it.
Envelopes fetch_key_and_distribute(User& sender, const std::string& target,
                                   const std::map<std::string, std::string>& registry) {
    Envelopes out;
    auto it = registry.find(target);
    if (it == registry.end()) {
        return out;
    }

    // This mirrors on_network_message(KEY_EXCHANGE key_response).
    sender.keys->cache_key(target, it->second);
    for (auto& env : sender.group->on_key_available(target)) {
        out.emplace_back(target, std::move(env));
    }
    return out;
}

// Deliver envelopes to their addressed recipients.
void deliver(const std::map<std::string, User*>& users, Envelopes& envelopes) {
    std::string detail;
    for (auto& [to, msg] : envelopes) {
        auto it = users.find(to);
        if (it == users.end()) {
            throw chat_test::Failure("envelope addressed to unknown user " + to);
        }
        auto status = it->second->group->process_envelope(msg, detail);
        if (status != GroupManager::EnvelopeStatus::OK) {
            throw chat_test::Failure("envelope delivery failed for " + to + ": " + detail);
        }
    }
}

// Everyone joins `room` at `epoch` with `members`, fetches missing keys from
// the registry, and all outstanding envelopes are delivered.
void everyone_syncs(const std::map<std::string, User*>& users,
                    const std::vector<std::string>& members,
                    const std::map<std::string, std::string>& registry,
                    const std::string& room, uint64_t epoch) {
    Envelopes all;

    for (const auto& name : members) {
        User* user = users.at(name);
        for (auto& env : user->group->sync_room(room, epoch, members)) {
            all.emplace_back(env.payload.value("recipient", ""), std::move(env));
        }
    }

    for (const auto& name : members) {
        User* user = users.at(name);
        for (const auto& member : user->group->missing_keys(room)) {
            for (auto& env : fetch_key_and_distribute(*user, member, registry)) {
                all.push_back(std::move(env));
            }
        }
    }

    deliver(users, all);
}

protocol::Message make_room_message(User& sender, const std::string& room,
                                    const std::string& text) {
    auto msg = protocol::Message::create(protocol::MessageType::ENCRYPTED_MESSAGE,
                                         sender.name, room);
    std::string err;
    auto payload = sender.group->seal(room, msg.id, text, err);
    if (!payload.has_value()) {
        throw chat_test::Failure("seal failed for " + sender.name + ": " + err);
    }
    msg.payload = *payload;
    return msg;
}

std::optional<std::string> open_room_message(User& receiver, const protocol::Message& msg) {
    std::string err;
    return receiver.group->open(msg.room, msg.from, msg.payload, err);
}

} // namespace

TEST(two_users_exchange_encrypted_room_messages) {
    User alice("alice"), bob("bob");
    std::map<std::string, User*> users = {{"alice", &alice}, {"bob", &bob}};
    std::map<std::string, std::string> registry = {
        {"alice", alice.pub()}, {"bob", bob.pub()}};
    std::vector<std::string> members = {"alice", "bob"};

    // Joining generates sender keys; envelopes can't go out until the
    // peer's box key has been fetched.
    auto pending = alice.group->sync_room("general", 2, members);
    CHECK(pending.empty());

    everyone_syncs(users, members, registry, "general", 2);

    // Bob -> Alice
    auto msg = make_room_message(bob, "general", "hello alice");
    auto plaintext = open_room_message(alice, msg);
    CHECK(plaintext.has_value());
    CHECK_EQ(*plaintext, "hello alice");

    // Alice -> Bob
    auto reply = make_room_message(alice, "general", "hi bob");
    auto reply_pt = open_room_message(bob, reply);
    CHECK(reply_pt.has_value());
    CHECK_EQ(*reply_pt, "hi bob");

    // Replay of the same frame must fail (one-time message keys).
    CHECK_FALSE(open_room_message(alice, msg).has_value());
}

TEST(out_of_order_delivery_still_decrypts) {
    User alice("alice"), bob("bob");
    std::map<std::string, User*> users = {{"alice", &alice}, {"bob", &bob}};
    std::map<std::string, std::string> registry = {
        {"alice", alice.pub()}, {"bob", bob.pub()}};
    std::vector<std::string> members = {"alice", "bob"};
    everyone_syncs(users, members, registry, "general", 2);

    auto m0 = make_room_message(bob, "general", "zero");
    auto m1 = make_room_message(bob, "general", "one");
    auto m2 = make_room_message(bob, "general", "two");

    // Deliver 2, 0, 1.
    auto p2 = open_room_message(alice, m2);
    CHECK(p2.has_value());
    CHECK_EQ(*p2, "two");

    auto p0 = open_room_message(alice, m0);
    CHECK(p0.has_value());
    CHECK_EQ(*p0, "zero");

    auto p1 = open_room_message(alice, m1);
    CHECK(p1.has_value());
    CHECK_EQ(*p1, "one");

    // Replays of the skipped-key messages must fail too.
    CHECK_FALSE(open_room_message(alice, m0).has_value());
    CHECK_FALSE(open_room_message(alice, m1).has_value());
}

TEST(epoch_rotation_on_join_and_leave) {
    User alice("alice"), bob("bob"), carol("carol");
    std::map<std::string, User*> users = {
        {"alice", &alice}, {"bob", &bob}, {"carol", &carol}};
    std::map<std::string, std::string> registry = {
        {"alice", alice.pub()}, {"bob", bob.pub()}, {"carol", carol.pub()}};

    // ---- Epoch 2: alice + bob chat ------------------------------------
    std::vector<std::string> two = {"alice", "bob"};
    everyone_syncs(users, two, registry, "general", 2);

    auto old_msg = make_room_message(bob, "general", "before carol");
    auto old_pt = open_room_message(alice, old_msg);
    CHECK(old_pt.has_value());
    CHECK_EQ(*old_pt, "before carol");

    // ---- Epoch 3: carol joins -----------------------------------------
    std::vector<std::string> three = {"alice", "bob", "carol"};
    // Alice sends a message in epoch 2 that bob hasn't seen yet; it must
    // still decrypt after the rotation (previous-epoch keys are retained).
    auto in_flight = make_room_message(alice, "general", "in flight");

    everyone_syncs(users, three, registry, "general", 3);

    // Carol cannot read traffic from the previous epoch...
    CHECK_FALSE(open_room_message(carol, old_msg).has_value());

    // ...but bob can still open the in-flight epoch-2 message.
    auto in_flight_pt = open_room_message(bob, in_flight);
    CHECK(in_flight_pt.has_value());
    CHECK_EQ(*in_flight_pt, "in flight");

    // New epoch flows to everyone.
    auto new_msg = make_room_message(alice, "general", "welcome carol");
    auto carol_pt = open_room_message(carol, new_msg);
    CHECK(carol_pt.has_value());
    CHECK_EQ(*carol_pt, "welcome carol");
    CHECK_EQ(*open_room_message(bob, new_msg), "welcome carol");

    // ---- Epoch 4: carol leaves -----------------------------------------
    std::vector<std::string> two_again = {"alice", "bob"};
    everyone_syncs(users, two_again, registry, "general", 4);

    auto after_leave = make_room_message(bob, "general", "carol is gone");
    CHECK_EQ(*open_room_message(alice, after_leave), "carol is gone");

    // Carol has no epoch-4 chain: the removed member is locked out.
    CHECK_FALSE(open_room_message(carol, after_leave).has_value());
}

TEST(forged_envelope_from_changed_identity_is_rejected) {
    User alice("alice"), bob("bob");
    std::map<std::string, User*> users = {{"alice", &alice}, {"bob", &bob}};
    std::map<std::string, std::string> registry = {
        {"alice", alice.pub()}, {"bob", bob.pub()}};
    std::vector<std::string> members = {"alice", "bob"};
    everyone_syncs(users, members, registry, "general", 2);

    // Mallory (a different identity) forges a "bob" envelope to alice.
    User mallory("mallory");

    auto chain = crypto::OutboundSenderKey::generate();
    crypto::Bytes alice_pk = crypto::from_b64(alice.pub());
    crypto::Bytes mallory_x_pk(mallory.keys->x25519_public_key(),
                               mallory.keys->x25519_public_key() + crypto_box_PUBLICKEYBYTES);
    crypto::Bytes mallory_x_sk(mallory.keys->x25519_secret_key(),
                               mallory.keys->x25519_secret_key() + crypto_box_SECRETKEYBYTES);
    crypto::Bytes mallory_ed_sk(mallory.keys->ed25519_secret_key(),
                                mallory.keys->ed25519_secret_key() + crypto_sign_SECRETKEYBYTES);

    auto wrapped = crypto::wrap_sender_key_envelope(
        "general", "bob", "alice", 2, chain.key_id(), chain.chain_key(),
        mallory_x_pk, mallory_x_sk, mallory_ed_sk, alice_pk);
    CHECK(wrapped.has_value());

    auto forged = protocol::Message::create(protocol::MessageType::ROOM_KEY, "bob", "general");
    forged.payload = {
        {"recipient", "alice"},
        {"epoch", 2},
        {"key_id", chain.key_id()},
        {"nonce", wrapped->nonce_b64},
        {"box", wrapped->box_b64},
        {"signature", wrapped->signature_b64},
        {"sender_x25519", wrapped->sender_x25519_b64},
        {"sender_ed25519", wrapped->sender_ed25519_b64},
    };

    std::string detail;
    CHECK(alice.group->process_envelope(forged, detail) ==
          GroupManager::EnvelopeStatus::KEY_CHANGED);

    // The pin is untouched: bob's real key is still the trusted one.
    auto bob_info = alice.group->peer_info("bob");
    CHECK(bob_info.has_value());
    CHECK(bob_info->pin_known);
    CHECK_EQ(bob_info->x25519_b64, bob.pub());
    CHECK(bob_info->pending_change);

    // Explicit acceptance re-pins to the new identity.
    CHECK(alice.group->accept_peer_key("bob"));
    auto accepted = alice.group->peer_info("bob");
    CHECK_FALSE(accepted->pending_change);
    CHECK_EQ(accepted->x25519_b64, crypto::to_b64(mallory_x_pk));

    // With the new pin, the same envelope now verifies end to end.
    CHECK(alice.group->process_envelope(forged, detail) ==
          GroupManager::EnvelopeStatus::OK);
}

TEST(missing_key_requests_are_bounded_and_retried) {
    User alice("alice"), bob("bob");
    std::vector<std::string> members = {"alice", "bob"};

    // Alice joins with bob, whose key is not in the directory yet.
    alice.group->sync_room("general", 2, members);
    CHECK(alice.group->missing_keys("general").size() == 1);

    // The client keeps asking for bob's key (bounded attempts).
    for (size_t i = 0; i < 5; ++i) {
        auto requests = alice.group->outstanding_key_requests();
        CHECK_EQ(requests.size(), size_t(1));
        CHECK_EQ(requests[0].room, "general");
        CHECK_EQ(requests[0].user, "bob");
        alice.group->note_key_request_sent(requests[0].room, requests[0].user);
    }

    // Attempt cap reached: stop asking so a silent peer cannot cause an
    // endless request loop.
    CHECK(alice.group->outstanding_key_requests().empty());

    // Bob's key appears (he published on login). The outstanding request
    // bookkeeping clears and the envelope is produced on the next tick.
    alice.keys->cache_key("bob", bob.pub());
    alice.group->note_key_available("bob");
    CHECK(alice.group->missing_keys("general").empty());
    CHECK(alice.group->outstanding_key_requests().empty());

    auto envelopes = alice.group->on_key_available("bob");
    CHECK_EQ(envelopes.size(), size_t(1));
    CHECK_EQ(envelopes[0].payload.value("recipient", ""), "bob");
}

TEST(distribution_heals_when_key_arrives_outside_key_response) {
    // Regression: the login publish race where a member's key reaches us
    // through their own envelope (which caches it) instead of a
    // key_response. Distribution must still happen on the next tick.
    User alice("alice"), bob("bob");
    std::map<std::string, User*> users = {{"alice", &alice}, {"bob", &bob}};
    std::vector<std::string> members = {"alice", "bob"};

    alice.group->sync_room("general", 2, members);
    bob.group->sync_room("general", 2, members);

    // Bob already has alice's key (published long ago); alice does NOT have
    // bob's yet, so bob's envelope to alice is produced but alice's to bob
    // is not.
    bob.keys->cache_key("alice", alice.pub());
    Envelopes bob_envs;
    for (auto& env : bob.group->on_key_available("alice")) {
        bob_envs.emplace_back("alice", std::move(env));
    }
    CHECK_EQ(bob_envs.size(), size_t(1));
    deliver(users, bob_envs);

    // Processing bob's envelope cached bob's X25519 key on alice's side.
    CHECK(alice.keys->has_cached_key("bob"));
    CHECK(alice.group->outstanding_key_requests().empty());

    // The periodic tick must now produce alice's envelope for bob.
    auto envelopes = alice.group->distribute_new_keys();
    CHECK_EQ(envelopes.size(), size_t(1));
    CHECK_EQ(envelopes[0].payload.value("recipient", ""), "bob");
    Envelopes alice_envs;
    alice_envs.emplace_back("bob", std::move(envelopes[0]));
    deliver(users, alice_envs);

    // Idempotent: nothing more to distribute.
    CHECK(alice.group->distribute_new_keys().empty());

    // Bob can now decrypt alice's messages.
    auto msg = make_room_message(alice, "general", "now you can read me");
    CHECK_EQ(*open_room_message(bob, msg), "now you can read me");
}

TEST(chains_survive_restart_via_persistence) {
    User alice("alice"), bob("bob");
    std::map<std::string, User*> users = {{"alice", &alice}, {"bob", &bob}};
    std::map<std::string, std::string> registry = {
        {"alice", alice.pub()}, {"bob", bob.pub()}};
    std::vector<std::string> members = {"alice", "bob"};
    everyone_syncs(users, members, registry, "general", 2);

    auto first = make_room_message(bob, "general", "before restart");
    CHECK_EQ(*open_room_message(alice, first), "before restart");
    alice.group->save();

    // "Restart": fresh KeyManager/GroupManager reading the same directory.
    KeyManager alice_keys;
    alice_keys.load_or_create((alice.base / "keys").string());
    GroupManager alice_again(alice_keys, (alice.base / "keys").string());
    alice_again.set_username("alice");
    CHECK(alice_again.load());

    // The chain (and the replay counter) survived: new messages open on the
    // reloaded instance, and replays are still rejected.
    auto second = make_room_message(bob, "general", "after restart");
    std::string err;
    auto pt = alice_again.open("general", "bob", second.payload, err);
    CHECK(pt.has_value());
    CHECK_EQ(*pt, "after restart");

    auto pt2 = alice_again.open("general", "bob", second.payload, err);
    CHECK_FALSE(pt2.has_value());

    CHECK(alice_again.peer_info("bob").has_value());
    CHECK(alice_again.peer_info("bob")->pin_known);
}
