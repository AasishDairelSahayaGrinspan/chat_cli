#pragma once

#include "storage/storage_interface.hpp"
#include "crypto/hash.hpp"
#include "protocol/message.hpp"
#include <memory>
#include <string>
#include <regex>

namespace chat::server {

class AuthService {
public:
    enum class Result {
        SUCCESS,
        INVALID_USERNAME,
        INVALID_PASSWORD,
        USER_EXISTS,
        USER_NOT_FOUND,
        WRONG_PASSWORD,
        STORAGE_ERROR
    };

    explicit AuthService(storage::IStorage& storage);

    // Register new user
    Result register_user(const std::string& username, const std::string& password);

    // Login existing user, returns user_id on success
    std::pair<Result, uint64_t> login(const std::string& username, const std::string& password);

    // Validate username format
    static bool is_valid_username(const std::string& username);

    // Validate password strength
    static bool is_valid_password(const std::string& password);

    // Get error message for result
    static std::string result_message(Result result);

private:
    storage::IStorage& storage_;
    crypto::PasswordHasher hasher_;
};

} // namespace chat::server

