#include "crypto/trust_store.hpp"

#include <nlohmann/json.hpp>

#include <fstream>

namespace chat::crypto {

namespace {

constexpr int TRUST_STORE_VERSION = 1;

} // namespace

TrustStatus TrustStore::check_and_pin(const std::string& user,
                                      const std::string& x25519_b64,
                                      const std::string& ed25519_b64) {
    if (user.empty() || x25519_b64.empty()) {
        return TrustStatus::CHANGED;  // invalid input: never treat as trusted
    }

    auto it = users_.find(user);
    if (it == users_.end()) {
        TrustedIdentity identity;
        identity.x25519_b64 = x25519_b64;
        identity.ed25519_b64 = ed25519_b64;
        users_[user] = identity;
        return TrustStatus::NEWLY_PINNED;
    }

    TrustedIdentity& pinned = it->second;

    if (pinned.x25519_b64 != x25519_b64) {
        pending_changes_.insert(user);
        return TrustStatus::CHANGED;
    }

    if (!ed25519_b64.empty()) {
        if (pinned.ed25519_b64.empty()) {
            // First time we learn the signing key: bind it to the pin.
            pinned.ed25519_b64 = ed25519_b64;
        } else if (pinned.ed25519_b64 != ed25519_b64) {
            pending_changes_.insert(user);
            return TrustStatus::CHANGED;
        }
    }

    return TrustStatus::KNOWN;
}

std::optional<TrustedIdentity> TrustStore::get(const std::string& user) const {
    auto it = users_.find(user);
    if (it == users_.end()) {
        return std::nullopt;
    }
    return it->second;
}

bool TrustStore::accept_new_key(const std::string& user,
                                const std::string& x25519_b64,
                                const std::string& ed25519_b64) {
    if (user.empty() || x25519_b64.empty()) {
        return false;
    }

    TrustedIdentity identity;
    identity.x25519_b64 = x25519_b64;
    identity.ed25519_b64 = ed25519_b64;
    identity.verified = false;  // acceptance is not out-of-band verification
    users_[user] = identity;
    pending_changes_.erase(user);
    return true;
}

bool TrustStore::mark_verified(const std::string& user, bool verified) {
    auto it = users_.find(user);
    if (it == users_.end()) {
        return false;
    }
    it->second.verified = verified;
    return true;
}

std::vector<std::string> TrustStore::pending_changes() const {
    return std::vector<std::string>(pending_changes_.begin(), pending_changes_.end());
}

void TrustStore::clear_pending_change(const std::string& user) {
    pending_changes_.erase(user);
}

bool TrustStore::has_pending_change(const std::string& user) const {
    return pending_changes_.count(user) > 0;
}

std::vector<std::string> TrustStore::known_users() const {
    std::vector<std::string> users;
    users.reserve(users_.size());
    for (const auto& [name, _] : users_) {
        users.push_back(name);
    }
    return users;
}

bool TrustStore::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;  // nothing pinned yet is not an error
    }

    auto j = nlohmann::json::parse(file, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        return false;
    }

    users_.clear();
    try {
        if (!j.contains("users") || !j.at("users").is_object()) {
            return true;
        }
        for (auto it = j.at("users").begin(); it != j.at("users").end(); ++it) {
            TrustedIdentity identity;
            identity.x25519_b64 = it.value().value("x25519", "");
            identity.ed25519_b64 = it.value().value("ed25519", "");
            identity.verified = it.value().value("verified", false);
            if (!identity.x25519_b64.empty()) {
                users_[it.key()] = identity;
            }
        }
    } catch (...) {
        return false;
    }
    return true;
}

bool TrustStore::save(const std::string& path) const {
    nlohmann::json users = nlohmann::json::object();
    for (const auto& [name, identity] : users_) {
        users[name] = {
            {"x25519", identity.x25519_b64},
            {"ed25519", identity.ed25519_b64},
            {"verified", identity.verified},
        };
    }

    nlohmann::json j{
        {"version", TRUST_STORE_VERSION},
        {"users", users},
    };

    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }
    file << j.dump(2);
    return file.good();
}

} // namespace chat::crypto
