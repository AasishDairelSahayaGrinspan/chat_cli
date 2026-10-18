#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <cstdint>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>

namespace chat::protocol {

enum class MessageType {
    MESSAGE,
    COMMAND,
    SYSTEM,
    ERROR,
    PRESENCE,
    AUTH_REQUEST,
    AUTH_RESPONSE,
    ROOM_EVENT,
    DIRECT_MESSAGE,
    KEY_EXCHANGE,
    ROOM_KEY,            // pairwise-wrapped room sender key (v2)
    ROOM_MEMBERSHIP,     // room epoch / membership change (v2)
    ENCRYPTED_MESSAGE,   // end-to-end encrypted room message (v2)
    HEALTH
};

NLOHMANN_JSON_SERIALIZE_ENUM(MessageType, {
    {MessageType::MESSAGE, "message"},
    {MessageType::COMMAND, "command"},
    {MessageType::SYSTEM, "system"},
    {MessageType::ERROR, "error"},
    {MessageType::PRESENCE, "presence"},
    {MessageType::AUTH_REQUEST, "auth_request"},
    {MessageType::AUTH_RESPONSE, "auth_response"},
    {MessageType::ROOM_EVENT, "room_event"},
    {MessageType::DIRECT_MESSAGE, "direct_message"},
    {MessageType::KEY_EXCHANGE, "key_exchange"},
    {MessageType::ROOM_KEY, "room_key"},
    {MessageType::ROOM_MEMBERSHIP, "room_membership"},
    {MessageType::ENCRYPTED_MESSAGE, "encrypted_message"},
    {MessageType::HEALTH, "health"}
})

struct Message {
    MessageType type;
    std::string id;
    int64_t timestamp;
    std::string from;
    std::string room;
    nlohmann::json payload;

    static std::string generate_uuid() {
        static thread_local std::random_device rd;
        static thread_local std::mt19937_64 gen(rd());
        static thread_local std::uniform_int_distribution<uint64_t> dis;

        uint64_t ab = dis(gen);
        uint64_t cd = dis(gen);

        // Set version 4 and variant bits
        ab = (ab & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
        cd = (cd & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

        std::ostringstream ss;
        ss << std::hex << std::setfill('0');
        ss << std::setw(8) << (ab >> 32) << '-';
        ss << std::setw(4) << ((ab >> 16) & 0xFFFF) << '-';
        ss << std::setw(4) << (ab & 0xFFFF) << '-';
        ss << std::setw(4) << (cd >> 48) << '-';
        ss << std::setw(12) << (cd & 0xFFFFFFFFFFFFULL);
        return ss.str();
    }

    static int64_t current_timestamp() {
        return std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    static Message create(MessageType t, const std::string& from_user = "",
                         const std::string& room_name = "") {
        Message msg;
        msg.type = t;
        msg.id = generate_uuid();
        msg.timestamp = current_timestamp();
        msg.from = from_user;
        msg.room = room_name;
        return msg;
    }

    static Message error(const std::string& error_msg, const std::string& code = "ERROR") {
        auto msg = create(MessageType::ERROR, "server");
        msg.payload = {{"message", error_msg}, {"code", code}};
        return msg;
    }

    static Message system(const std::string& sys_msg) {
        auto msg = create(MessageType::SYSTEM, "server");
        msg.payload = {{"message", sys_msg}};
        return msg;
    }
};

inline void to_json(nlohmann::json& j, const Message& m) {
    j = nlohmann::json{
        {"type", m.type},
        {"id", m.id},
        {"timestamp", m.timestamp},
        {"from", m.from},
        {"room", m.room},
        {"payload", m.payload}
    };
}

inline void from_json(const nlohmann::json& j, Message& m) {
    j.at("type").get_to(m.type);
    j.at("id").get_to(m.id);
    j.at("timestamp").get_to(m.timestamp);
    if (j.contains("from")) j.at("from").get_to(m.from);
    if (j.contains("room")) j.at("room").get_to(m.room);
    if (j.contains("payload")) j.at("payload").get_to(m.payload);
}

// Message validation constants
constexpr size_t MAX_MESSAGE_SIZE = 65536;      // 64KB max message
constexpr size_t MAX_USERNAME_LENGTH = 32;
constexpr size_t MIN_USERNAME_LENGTH = 3;
constexpr size_t MAX_ROOM_NAME_LENGTH = 64;
constexpr size_t MAX_PASSWORD_LENGTH = 128;
constexpr size_t MIN_PASSWORD_LENGTH = 8;

// Protocol version 2 adds end-to-end encrypted room messages.
// Clients that predate v2 cannot read room traffic on servers where
// room E2EE is required (the default).
constexpr int PROTOCOL_VERSION = 2;

// Room E2EE (v2) payload size limits. These are generous upper bounds on
// base64-encoded fields, enforced by the server before relaying.
constexpr size_t MAX_ROOM_KEY_B64_LENGTH = 2048;       // wrapped sender key box
constexpr size_t MAX_ROOM_NONCE_B64_LENGTH = 64;       // 24-byte nonce
constexpr size_t MAX_ROOM_SIGNATURE_B64_LENGTH = 128;  // 64-byte Ed25519 sig
constexpr size_t MAX_ROOM_PUBKEY_B64_LENGTH = 64;      // 32-byte public key
constexpr size_t MAX_ROOM_CIPHERTEXT_B64_LENGTH = 60000;

} // namespace chat::protocol

