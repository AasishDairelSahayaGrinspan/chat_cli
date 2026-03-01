#pragma once

// Redis Pub/Sub hooks for horizontal scaling
// When ENABLE_REDIS is defined, integrate with hiredis library

#include "protocol/message.hpp"
#include <string>
#include <functional>
#include <memory>

namespace chat::server::storage {

// Abstract interface for message broadcasting across server instances
class IMessageBroker {
public:
    using MessageCallback = std::function<void(const std::string& channel, const std::string& message)>;

    virtual ~IMessageBroker() = default;

    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;

    virtual bool publish(const std::string& channel, const std::string& message) = 0;
    virtual bool subscribe(const std::string& channel, MessageCallback callback) = 0;
    virtual bool unsubscribe(const std::string& channel) = 0;

    virtual void poll() = 0;  // Process incoming messages
};

// Stub implementation that does nothing (single-instance mode)
class NullMessageBroker : public IMessageBroker {
public:
    bool connect() override { return true; }
    void disconnect() override {}
    bool is_connected() const override { return true; }

    bool publish(const std::string&, const std::string&) override { return true; }
    bool subscribe(const std::string&, MessageCallback) override { return true; }
    bool unsubscribe(const std::string&) override { return true; }

    void poll() override {}
};

#ifdef ENABLE_REDIS
// Redis implementation would go here
// Requires hiredis library
class RedisBroker : public IMessageBroker {
public:
    RedisBroker(const std::string& host, uint16_t port);
    ~RedisBroker() override;

    bool connect() override;
    void disconnect() override;
    bool is_connected() const override;

    bool publish(const std::string& channel, const std::string& message) override;
    bool subscribe(const std::string& channel, MessageCallback callback) override;
    bool unsubscribe(const std::string& channel) override;

    void poll() override;

private:
    std::string host_;
    uint16_t port_;
    // redisContext* context_;  // hiredis connection
    // redisContext* sub_context_;  // Separate connection for subscriptions
};
#endif

// Factory function
inline std::unique_ptr<IMessageBroker> create_message_broker(
    [[maybe_unused]] bool redis_enabled,
    [[maybe_unused]] const std::string& host = "localhost",
    [[maybe_unused]] uint16_t port = 6379) {

#ifdef ENABLE_REDIS
    if (redis_enabled) {
        return std::make_unique<RedisBroker>(host, port);
    }
#endif
    return std::make_unique<NullMessageBroker>();
}

} // namespace chat::server::storage

