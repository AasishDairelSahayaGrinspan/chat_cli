#pragma once

// Trust-on-first-use (TOFU) store for peer identity keys.
//
// Identity keys are X25519 (crypto_box, used for envelopes and DMs) and
// Ed25519 (used to sign room messages). The first key we ever see for a
// user is pinned. Any subsequent change is reported as CHANGED and does
// not overwrite the pin until the user explicitly accepts it (/verify).

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace chat::crypto {

struct TrustedIdentity {
    std::string x25519_b64;
    std::string ed25519_b64;  // may be empty until we learn it
    bool verified = false;    // user confirmed the fingerprint out-of-band
};

enum class TrustStatus {
    KNOWN,          // matches the pinned keys
    NEWLY_PINNED,   // first time we see this user
    CHANGED,        // key mismatch with the pin (pin NOT updated)
};

class TrustStore {
public:
    // First-sight pinning. `ed25519_b64` may be empty when unknown;
    // a later non-empty value fills it in without triggering CHANGED.
    TrustStatus check_and_pin(const std::string& user,
                              const std::string& x25519_b64,
                              const std::string& ed25519_b64 = "");

    std::optional<TrustedIdentity> get(const std::string& user) const;

    // Explicit user action after a key change was reported.
    bool accept_new_key(const std::string& user,
                        const std::string& x25519_b64,
                        const std::string& ed25519_b64);

    bool mark_verified(const std::string& user, bool verified);

    // Users whose pin changed and has not been accepted yet this session.
    std::vector<std::string> pending_changes() const;
    void clear_pending_change(const std::string& user);
    bool has_pending_change(const std::string& user) const;

    std::vector<std::string> known_users() const;

    bool load(const std::string& path);
    bool save(const std::string& path) const;

private:
    std::unordered_map<std::string, TrustedIdentity> users_;
    std::unordered_set<std::string> pending_changes_;
};

} // namespace chat::crypto
