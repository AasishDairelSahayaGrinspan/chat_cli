#include "auth_service.hpp"
#include "logging/logger.hpp"

namespace chat::server {

AuthService::AuthService(storage::IStorage& storage) : storage_(storage) {}

AuthService::Result AuthService::register_user(const std::string& username,
                                                const std::string& password) {
    if (!is_valid_username(username)) {
        return Result::INVALID_USERNAME;
    }

    if (!is_valid_password(password)) {
        return Result::INVALID_PASSWORD;
    }

    // Check if user exists
    if (storage_.get_user_by_username(username).has_value()) {
        return Result::USER_EXISTS;
    }

    // Hash password and create user
    try {
        std::string hash = hasher_.hash(password);
        if (storage_.create_user(username, hash)) {
            LOG_INFO("User registered: {}", username);
            return Result::SUCCESS;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("Failed to register user '{}': {}", username, e.what());
    }

    return Result::STORAGE_ERROR;
}

std::pair<AuthService::Result, uint64_t> AuthService::login(const std::string& username,
                                                             const std::string& password) {
    auto user = storage_.get_user_by_username(username);
    if (!user.has_value()) {
        return {Result::USER_NOT_FOUND, 0};
    }

    if (!user->active) {
        return {Result::USER_NOT_FOUND, 0};  // Treat inactive as not found
    }

    if (!hasher_.verify(password, user->password_hash)) {
        LOG_WARN("Failed login attempt for user '{}'", username);
        return {Result::WRONG_PASSWORD, 0};
    }

    // Update last login time
    storage_.update_last_login(user->id);

    // Check if password needs rehash
    if (hasher_.needs_rehash(user->password_hash)) {
        try {
            std::string new_hash = hasher_.hash(password);
            storage_.update_password(user->id, new_hash);
            LOG_DEBUG("Rehashed password for user '{}'", username);
        } catch (const std::exception& e) {
            LOG_WARN("Failed to rehash password: {}", e.what());
        }
    }

    LOG_INFO("User logged in: {}", username);
    return {Result::SUCCESS, user->id};
}

bool AuthService::is_valid_username(const std::string& username) {
    if (username.length() < protocol::MIN_USERNAME_LENGTH ||
        username.length() > protocol::MAX_USERNAME_LENGTH) {
        return false;
    }

    // Alphanumeric, underscores, hyphens only
    static const std::regex pattern("^[a-zA-Z][a-zA-Z0-9_-]*$");
    return std::regex_match(username, pattern);
}

bool AuthService::is_valid_password(const std::string& password) {
    if (password.length() < protocol::MIN_PASSWORD_LENGTH ||
        password.length() > protocol::MAX_PASSWORD_LENGTH) {
        return false;
    }

    // Require at least one letter and one digit
    bool has_letter = false;
    bool has_digit = false;

    for (char c : password) {
        if (std::isalpha(static_cast<unsigned char>(c))) has_letter = true;
        if (std::isdigit(static_cast<unsigned char>(c))) has_digit = true;
    }

    return has_letter && has_digit;
}

std::string AuthService::result_message(Result result) {
    switch (result) {
        case Result::SUCCESS:
            return "Success";
        case Result::INVALID_USERNAME:
            return "Invalid username. Must be 3-32 characters, start with a letter, "
                   "and contain only letters, numbers, underscores, or hyphens.";
        case Result::INVALID_PASSWORD:
            return "Invalid password. Must be 8-128 characters with at least "
                   "one letter and one number.";
        case Result::USER_EXISTS:
            return "Username already taken.";
        case Result::USER_NOT_FOUND:
            return "User not found.";
        case Result::WRONG_PASSWORD:
            return "Incorrect password.";
        case Result::STORAGE_ERROR:
            return "Internal server error. Please try again.";
    }
    return "Unknown error";
}

} // namespace chat::server

