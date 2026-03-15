#include "crypto.hpp"
#include "logging/logger.hpp"
#include <fstream>
#include <filesystem>
#include <cstring>

namespace chat::client {

KeyManager::KeyManager() {
    if (sodium_init() < 0) {
        LOG_ERROR("Failed to initialize libsodium");
    }
}

bool KeyManager::load_or_create(const std::string& key_dir) {
    if (load_keypair(key_dir)) {
        initialized_ = true;
        LOG_INFO("Loaded E2EE keypair from {}", key_dir);
        return true;
    }

    // Generate new keypair
    crypto_box_keypair(public_key_, secret_key_);

    if (save_keypair(key_dir)) {
        initialized_ = true;
        LOG_INFO("Generated new E2EE keypair in {}", key_dir);
        return true;
    }

    // Even if save fails, we can still use the keys in memory
    initialized_ = true;
    LOG_WARN("Generated E2EE keypair but failed to save to disk");
    return true;
}

std::string KeyManager::get_public_key() const {
    return to_base64(public_key_, crypto_box_PUBLICKEYBYTES);
}

std::string KeyManager::get_fingerprint() const {
    std::string b64 = get_public_key();
    if (b64.length() > 8) {
        return b64.substr(0, 8) + "...";
    }
    return b64;
}

std::optional<EncryptedMessage> KeyManager::encrypt(
    const std::string& plaintext,
    const std::string& recipient_public_key_b64) {

    if (!initialized_) return std::nullopt;

    auto recipient_pk = from_base64(recipient_public_key_b64);
    if (recipient_pk.size() != crypto_box_PUBLICKEYBYTES) {
        LOG_ERROR("Invalid recipient public key size");
        return std::nullopt;
    }

    // Generate random nonce
    unsigned char nonce[crypto_box_NONCEBYTES];
    randombytes_buf(nonce, sizeof(nonce));

    // Encrypt
    std::vector<unsigned char> ciphertext(plaintext.size() + crypto_box_MACBYTES);

    if (crypto_box_easy(ciphertext.data(),
                        reinterpret_cast<const unsigned char*>(plaintext.data()),
                        plaintext.size(),
                        nonce,
                        recipient_pk.data(),
                        secret_key_) != 0) {
        LOG_ERROR("Encryption failed");
        return std::nullopt;
    }

    return EncryptedMessage{
        to_base64(ciphertext.data(), ciphertext.size()),
        to_base64(nonce, crypto_box_NONCEBYTES)
    };
}

std::optional<std::string> KeyManager::decrypt(
    const std::string& ciphertext_b64,
    const std::string& nonce_b64,
    const std::string& sender_public_key_b64) {

    if (!initialized_) return std::nullopt;

    auto ciphertext = from_base64(ciphertext_b64);
    auto nonce = from_base64(nonce_b64);
    auto sender_pk = from_base64(sender_public_key_b64);

    if (nonce.size() != crypto_box_NONCEBYTES) {
        LOG_ERROR("Invalid nonce size");
        return std::nullopt;
    }
    if (sender_pk.size() != crypto_box_PUBLICKEYBYTES) {
        LOG_ERROR("Invalid sender public key size");
        return std::nullopt;
    }
    if (ciphertext.size() < crypto_box_MACBYTES) {
        LOG_ERROR("Ciphertext too short");
        return std::nullopt;
    }

    std::vector<unsigned char> plaintext(ciphertext.size() - crypto_box_MACBYTES);

    if (crypto_box_open_easy(plaintext.data(),
                              ciphertext.data(),
                              ciphertext.size(),
                              nonce.data(),
                              sender_pk.data(),
                              secret_key_) != 0) {
        LOG_ERROR("Decryption failed — message may be corrupted or from wrong sender");
        return std::nullopt;
    }

    return std::string(plaintext.begin(), plaintext.end());
}

void KeyManager::cache_key(const std::string& username, const std::string& public_key_b64) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    key_cache_[username] = public_key_b64;
}

std::optional<std::string> KeyManager::get_cached_key(const std::string& username) const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = key_cache_.find(username);
    if (it != key_cache_.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool KeyManager::has_cached_key(const std::string& username) const {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    return key_cache_.count(username) > 0;
}

std::string KeyManager::to_base64(const unsigned char* data, size_t len) {
    // libsodium base64 (URL-safe, no padding)
    size_t b64_len = sodium_base64_encoded_len(len, sodium_base64_VARIANT_ORIGINAL);
    std::string result(b64_len, '\0');
    sodium_bin2base64(&result[0], b64_len, data, len, sodium_base64_VARIANT_ORIGINAL);
    // Remove trailing null
    while (!result.empty() && result.back() == '\0') {
        result.pop_back();
    }
    return result;
}

std::vector<unsigned char> KeyManager::from_base64(const std::string& b64) {
    std::vector<unsigned char> result(b64.size());  // will be larger than needed
    size_t bin_len = 0;

    if (sodium_base642bin(result.data(), result.size(),
                          b64.c_str(), b64.size(),
                          nullptr, &bin_len, nullptr,
                          sodium_base64_VARIANT_ORIGINAL) != 0) {
        return {};
    }

    result.resize(bin_len);
    return result;
}

bool KeyManager::save_keypair(const std::string& key_dir) {
    try {
        std::filesystem::create_directories(key_dir);

        std::string pk_path = key_dir + "/public.key";
        std::string sk_path = key_dir + "/secret.key";

        // Save public key
        std::ofstream pk_file(pk_path, std::ios::binary);
        if (!pk_file) return false;
        pk_file.write(reinterpret_cast<const char*>(public_key_), crypto_box_PUBLICKEYBYTES);

        // Save secret key
        std::ofstream sk_file(sk_path, std::ios::binary);
        if (!sk_file) return false;
        sk_file.write(reinterpret_cast<const char*>(secret_key_), crypto_box_SECRETKEYBYTES);

        // Set restrictive permissions on secret key
        std::filesystem::permissions(sk_path,
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace);

        return true;
    } catch (const std::exception& e) {
        LOG_WARN("Failed to save keypair: {}", e.what());
        return false;
    }
}

bool KeyManager::load_keypair(const std::string& key_dir) {
    try {
        std::string pk_path = key_dir + "/public.key";
        std::string sk_path = key_dir + "/secret.key";

        if (!std::filesystem::exists(pk_path) || !std::filesystem::exists(sk_path)) {
            return false;
        }

        std::ifstream pk_file(pk_path, std::ios::binary);
        if (!pk_file) return false;
        pk_file.read(reinterpret_cast<char*>(public_key_), crypto_box_PUBLICKEYBYTES);
        if (pk_file.gcount() != crypto_box_PUBLICKEYBYTES) return false;

        std::ifstream sk_file(sk_path, std::ios::binary);
        if (!sk_file) return false;
        sk_file.read(reinterpret_cast<char*>(secret_key_), crypto_box_SECRETKEYBYTES);
        if (sk_file.gcount() != crypto_box_SECRETKEYBYTES) return false;

        return true;
    } catch (const std::exception& e) {
        LOG_WARN("Failed to load keypair: {}", e.what());
        return false;
    }
}

} // namespace chat::client
