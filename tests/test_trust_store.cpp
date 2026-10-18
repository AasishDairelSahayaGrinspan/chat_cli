// Unit tests for the TOFU trust store.

#include "crypto/trust_store.hpp"
#include "test_framework.hpp"

#include <filesystem>
#include <random>

using namespace chat::crypto;

namespace {

std::string temp_path(const std::string& name) {
    auto dir = std::filesystem::temp_directory_path() /
               ("chat_cli_test_" + std::to_string(std::random_device{}()));
    std::filesystem::create_directories(dir);
    return (dir / name).string();
}

} // namespace

TEST(pins_new_identity) {
    TrustStore store;
    auto status = store.check_and_pin("alice", "XKEY_A", "EKEY_A");
    CHECK(status == TrustStatus::NEWLY_PINNED);

    auto identity = store.get("alice");
    CHECK(identity.has_value());
    CHECK_EQ(identity->x25519_b64, "XKEY_A");
    CHECK_EQ(identity->ed25519_b64, "EKEY_A");
    CHECK_FALSE(identity->verified);
}

TEST(same_identity_is_known) {
    TrustStore store;
    store.check_and_pin("alice", "XKEY_A", "EKEY_A");
    CHECK(store.check_and_pin("alice", "XKEY_A", "EKEY_A") == TrustStatus::KNOWN);
    CHECK_FALSE(store.has_pending_change("alice"));
}

TEST(changed_key_is_flagged_and_not_pinned) {
    TrustStore store;
    store.check_and_pin("alice", "XKEY_A", "EKEY_A");

    // Server (or an attacker) swaps in different keys.
    CHECK(store.check_and_pin("alice", "XKEY_EVIL", "EKEY_EVIL") == TrustStatus::CHANGED);
    CHECK(store.has_pending_change("alice"));

    // The pin must not have been silently updated.
    auto identity = store.get("alice");
    CHECK_EQ(identity->x25519_b64, "XKEY_A");
    CHECK_EQ(identity->ed25519_b64, "EKEY_A");

    // Still changed on subsequent checks until accepted.
    CHECK(store.check_and_pin("alice", "XKEY_EVIL", "EKEY_EVIL") == TrustStatus::CHANGED);
}

TEST(accept_new_key_overwrites_and_clears_pending) {
    TrustStore store;
    store.check_and_pin("alice", "XKEY_A", "EKEY_A");
    store.check_and_pin("alice", "XKEY_B", "EKEY_B");

    CHECK(store.accept_new_key("alice", "XKEY_B", "EKEY_B"));
    CHECK_FALSE(store.has_pending_change("alice"));

    auto identity = store.get("alice");
    CHECK_EQ(identity->x25519_b64, "XKEY_B");
    CHECK_FALSE(identity->verified);  // accepting != verifying
    CHECK(store.check_and_pin("alice", "XKEY_B", "EKEY_B") == TrustStatus::KNOWN);
}

TEST(signing_key_is_learned_later) {
    TrustStore store;
    // First seen without an Ed25519 key (e.g. a DM key response).
    CHECK(store.check_and_pin("alice", "XKEY_A", "") == TrustStatus::NEWLY_PINNED);

    // Learning the signing key is not a change.
    CHECK(store.check_and_pin("alice", "XKEY_A", "EKEY_A") == TrustStatus::KNOWN);
    CHECK_EQ(store.get("alice")->ed25519_b64, "EKEY_A");

    // But a different one afterwards is.
    CHECK(store.check_and_pin("alice", "XKEY_A", "EKEY_B") == TrustStatus::CHANGED);
}

TEST(mark_verified) {
    TrustStore store;
    store.check_and_pin("alice", "XKEY_A", "EKEY_A");
    CHECK(store.mark_verified("alice", true));
    CHECK(store.get("alice")->verified);
    CHECK_FALSE(store.mark_verified("unknown_user", true));
}

TEST(save_load_roundtrip) {
    std::string path = temp_path("trust.json");

    TrustStore store;
    store.check_and_pin("alice", "XKEY_A", "EKEY_A");
    store.check_and_pin("bob", "XKEY_B", "");
    store.mark_verified("alice", true);
    CHECK(store.save(path));

    TrustStore loaded;
    CHECK(loaded.load(path));

    auto alice = loaded.get("alice");
    CHECK(alice.has_value());
    CHECK_EQ(alice->x25519_b64, "XKEY_A");
    CHECK_EQ(alice->ed25519_b64, "EKEY_A");
    CHECK(alice->verified);

    auto bob = loaded.get("bob");
    CHECK(bob.has_value());
    CHECK_EQ(bob->x25519_b64, "XKEY_B");
    CHECK(bob->ed25519_b64.empty());

    // Pins survive: known after reload, and a change is still detected.
    CHECK(loaded.check_and_pin("alice", "XKEY_A", "EKEY_A") == TrustStatus::KNOWN);
    CHECK(loaded.check_and_pin("alice", "XKEY_EVIL", "EKEY_A") == TrustStatus::CHANGED);

    std::filesystem::remove_all(std::filesystem::path(path).parent_path());
}

TEST(load_missing_file_is_not_an_error) {
    TrustStore store;
    CHECK_FALSE(store.load("/nonexistent/path/that/does/not/exist.json"));
    CHECK(store.get("alice") == std::nullopt);
}
