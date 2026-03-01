#pragma once

#include <sodium.h>
#include <string>
#include <stdexcept>
#include <memory>

namespace chat::crypto {

class PasswordHasher {
public:
    PasswordHasher() {
        if (sodium_init() < 0) {
            throw std::runtime_error("libsodium initialization failed");
        }
    }

    // Hash password using Argon2id (libsodium's default)
    std::string hash(const std::string& password) const {
        char hashed[crypto_pwhash_STRBYTES];

        if (crypto_pwhash_str(
                hashed,
                password.c_str(),
                password.length(),
                crypto_pwhash_OPSLIMIT_INTERACTIVE,
                crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
            throw std::runtime_error("Password hashing failed - out of memory");
        }

        return std::string(hashed);
    }

    // Verify password against hash
    bool verify(const std::string& password, const std::string& hash) const {
        return crypto_pwhash_str_verify(
            hash.c_str(),
            password.c_str(),
            password.length()) == 0;
    }

    // Check if hash needs rehashing (parameters changed)
    bool needs_rehash(const std::string& hash) const {
        return crypto_pwhash_str_needs_rehash(
            hash.c_str(),
            crypto_pwhash_OPSLIMIT_INTERACTIVE,
            crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0;
    }

    // Generate secure random bytes
    static std::string random_bytes(size_t length) {
        std::string result(length, '\0');
        randombytes_buf(result.data(), length);
        return result;
    }

    // Generate secure random token (hex encoded)
    static std::string random_token(size_t byte_length = 32) {
        std::string bytes = random_bytes(byte_length);
        std::string hex(byte_length * 2, '\0');
        sodium_bin2hex(hex.data(), hex.size() + 1,
                      reinterpret_cast<const unsigned char*>(bytes.data()), byte_length);
        return hex;
    }
};

// Thread-safe singleton
inline PasswordHasher& get_hasher() {
    static PasswordHasher hasher;
    return hasher;
}

} // namespace chat::crypto

