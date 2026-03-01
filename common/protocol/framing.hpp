#pragma once

#include "message.hpp"
#include <vector>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <optional>

namespace chat::protocol {

// Length-prefixed binary framing with 4-byte big-endian header
class Framing {
public:
    static constexpr size_t HEADER_SIZE = 4;
    static constexpr size_t MAX_FRAME_SIZE = MAX_MESSAGE_SIZE + HEADER_SIZE;

    // Encode a message into a length-prefixed frame
    static std::vector<uint8_t> encode(const Message& msg) {
        nlohmann::json j = msg;
        std::string json_str = j.dump();

        if (json_str.size() > MAX_MESSAGE_SIZE) {
            throw std::runtime_error("Message exceeds maximum size");
        }

        std::vector<uint8_t> frame;
        frame.reserve(HEADER_SIZE + json_str.size());

        // Write 4-byte big-endian length
        uint32_t len = static_cast<uint32_t>(json_str.size());
        frame.push_back(static_cast<uint8_t>((len >> 24) & 0xFF));
        frame.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));

        // Append JSON payload
        frame.insert(frame.end(), json_str.begin(), json_str.end());
        return frame;
    }

    // Encode raw JSON string
    static std::vector<uint8_t> encode_raw(const std::string& json_str) {
        if (json_str.size() > MAX_MESSAGE_SIZE) {
            throw std::runtime_error("Message exceeds maximum size");
        }

        std::vector<uint8_t> frame;
        frame.reserve(HEADER_SIZE + json_str.size());

        uint32_t len = static_cast<uint32_t>(json_str.size());
        frame.push_back(static_cast<uint8_t>((len >> 24) & 0xFF));
        frame.push_back(static_cast<uint8_t>((len >> 16) & 0xFF));
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));

        frame.insert(frame.end(), json_str.begin(), json_str.end());
        return frame;
    }

    // Read length from header (returns 0 if invalid)
    static uint32_t read_length(const uint8_t* header) {
        return (static_cast<uint32_t>(header[0]) << 24) |
               (static_cast<uint32_t>(header[1]) << 16) |
               (static_cast<uint32_t>(header[2]) << 8) |
               static_cast<uint32_t>(header[3]);
    }

    // Streaming decoder for handling partial reads
    class Decoder {
    public:
        Decoder() : state_(State::READING_HEADER), bytes_needed_(HEADER_SIZE) {
            header_buffer_.reserve(HEADER_SIZE);
        }

        // Feed data to decoder, returns decoded messages
        std::vector<Message> feed(const uint8_t* data, size_t len) {
            std::vector<Message> messages;
            size_t offset = 0;

            while (offset < len) {
                if (state_ == State::READING_HEADER) {
                    size_t to_read = std::min(bytes_needed_, len - offset);
                    header_buffer_.insert(header_buffer_.end(),
                                         data + offset, data + offset + to_read);
                    offset += to_read;
                    bytes_needed_ -= to_read;

                    if (bytes_needed_ == 0) {
                        payload_length_ = read_length(header_buffer_.data());

                        if (payload_length_ > MAX_MESSAGE_SIZE) {
                            throw std::runtime_error("Frame too large");
                        }

                        header_buffer_.clear();
                        payload_buffer_.clear();
                        payload_buffer_.reserve(payload_length_);
                        bytes_needed_ = payload_length_;
                        state_ = State::READING_PAYLOAD;
                    }
                } else {
                    size_t to_read = std::min(bytes_needed_, len - offset);
                    payload_buffer_.insert(payload_buffer_.end(),
                                          data + offset, data + offset + to_read);
                    offset += to_read;
                    bytes_needed_ -= to_read;

                    if (bytes_needed_ == 0) {
                        std::string json_str(payload_buffer_.begin(), payload_buffer_.end());
                        try {
                            auto j = nlohmann::json::parse(json_str);
                            messages.push_back(j.get<Message>());
                        } catch (const std::exception& e) {
                            throw std::runtime_error(
                                std::string("Invalid JSON in frame: ") + e.what());
                        }

                        payload_buffer_.clear();
                        bytes_needed_ = HEADER_SIZE;
                        state_ = State::READING_HEADER;
                    }
                }
            }

            return messages;
        }

        void reset() {
            state_ = State::READING_HEADER;
            bytes_needed_ = HEADER_SIZE;
            header_buffer_.clear();
            payload_buffer_.clear();
        }

    private:
        enum class State { READING_HEADER, READING_PAYLOAD };

        State state_;
        size_t bytes_needed_;
        uint32_t payload_length_ = 0;
        std::vector<uint8_t> header_buffer_;
        std::vector<uint8_t> payload_buffer_;
    };
};

} // namespace chat::protocol

