#pragma once

#include <string>
#include <vector>
#include <optional>
#include <unordered_map>
#include <mutex>
#include <sodium.h>

namespace chat::client {

struct EncryptedMessage {
    std::string ciphertext;  // base64 encoded
    std::string nonce;       // base64 encoded
};

class KeyManager {
public:
    KeyManager();
    ~KeyManager() = default;

    // Load existing keypair(s) from disk or generate new ones.
    // Creates an X25519 box keypair (public.key/secret.key) and an Ed25519
    // signing keypair (signing.pub/signing.key) used to sign room messages.
    bool load_or_create(const std::string& key_dir);

    // Get our public key (base64 encoded)
    std::string get_public_key() const;

    // Get our signing public key (base64 encoded)
    std::string get_signing_public_key() const;

    // Short fingerprint of the X25519 key for status display
    std::string get_fingerprint() const;

    // Full identity fingerprint covering both long-term keys
    // (this is what users compare out-of-band with /verify)
    std::string get_identity_fingerprint() const;

    // Raw key material for group crypto
    const unsigned char* x25519_public_key() const { return public_key_; }
    const unsigned char* x25519_secret_key() const { return secret_key_; }
    const unsigned char* ed25519_public_key() const { return signing_public_key_; }
    const unsigned char* ed25519_secret_key() const { return signing_secret_key_; }

    // Encrypt a message for a recipient
    std::optional<EncryptedMessage> encrypt(const std::string& plaintext,
                                             const std::string& recipient_public_key_b64);

    // Decrypt a message from a sender
    std::optional<std::string> decrypt(const std::string& ciphertext_b64,
                                        const std::string& nonce_b64,
                                        const std::string& sender_public_key_b64);

    // Key cache management
    void cache_key(const std::string& username, const std::string& public_key_b64);
    std::optional<std::string> get_cached_key(const std::string& username) const;
    bool has_cached_key(const std::string& username) const;

    bool is_initialized() const { return initialized_; }

private:
    // Base64 helpers using libsodium
    static std::string to_base64(const unsigned char* data, size_t len);
    static std::vector<unsigned char> from_base64(const std::string& b64);

    bool save_keypair(const std::string& key_dir);
    bool load_keypair(const std::string& key_dir);

    bool save_signing_keypair(const std::string& key_dir);
    bool load_signing_keypair(const std::string& key_dir);

    unsigned char public_key_[crypto_box_PUBLICKEYBYTES]{};
    unsigned char secret_key_[crypto_box_SECRETKEYBYTES]{};
    unsigned char signing_public_key_[crypto_sign_PUBLICKEYBYTES]{};
    unsigned char signing_secret_key_[crypto_sign_SECRETKEYBYTES]{};
    bool initialized_ = false;

    mutable std::mutex cache_mutex_;
    std::unordered_map<std::string, std::string> key_cache_;  // username -> base64 public key
};

} // namespace chat::client
