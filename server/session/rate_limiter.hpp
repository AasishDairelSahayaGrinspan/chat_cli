#pragma once

#include <chrono>
#include <mutex>
#include <unordered_map>
#include <string>

namespace chat::server {

// Token bucket rate limiter
class RateLimiter {
public:
    struct Config {
        size_t tokens_per_second = 10;
        size_t max_burst = 20;
    };

    RateLimiter() : config_{} {}
    explicit RateLimiter(const Config& config)
        : config_(config) {}

    // Returns true if request is allowed, false if rate limited
    bool check(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto now = std::chrono::steady_clock::now();
        auto& bucket = buckets_[key];

        // Initialize new bucket
        if (bucket.tokens == 0 && bucket.last_update == std::chrono::steady_clock::time_point{}) {
            bucket.tokens = config_.max_burst;
            bucket.last_update = now;
        }

        // Refill tokens based on elapsed time
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - bucket.last_update).count();

        if (elapsed > 0) {
            double refill = (elapsed / 1000.0) * config_.tokens_per_second;
            bucket.tokens = std::min(
                static_cast<double>(config_.max_burst),
                bucket.tokens + refill
            );
            bucket.last_update = now;
        }

        // Check if we have tokens
        if (bucket.tokens >= 1.0) {
            bucket.tokens -= 1.0;
            return true;
        }

        return false;
    }

    // Get remaining tokens for a key
    double remaining(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = buckets_.find(key);
        return it != buckets_.end() ? it->second.tokens : config_.max_burst;
    }

    // Remove a key (e.g., when connection closes)
    void remove(const std::string& key) {
        std::lock_guard<std::mutex> lock(mutex_);
        buckets_.erase(key);
    }

    // Clear all buckets
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        buckets_.clear();
    }

    // Cleanup old entries
    void cleanup(std::chrono::seconds max_age = std::chrono::seconds(300)) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto now = std::chrono::steady_clock::now();

        for (auto it = buckets_.begin(); it != buckets_.end();) {
            auto age = std::chrono::duration_cast<std::chrono::seconds>(
                now - it->second.last_update);
            if (age > max_age) {
                it = buckets_.erase(it);
            } else {
                ++it;
            }
        }
    }

private:
    struct Bucket {
        double tokens = 0;
        std::chrono::steady_clock::time_point last_update{};
    };

    Config config_;
    std::mutex mutex_;
    std::unordered_map<std::string, Bucket> buckets_;
};

// Per-IP and per-user rate limiter
class DualRateLimiter {
public:
    struct Config {
        RateLimiter::Config ip_config{5, 10};      // Stricter for unauthenticated
        RateLimiter::Config user_config{10, 20};   // More lenient for authenticated
    };

    DualRateLimiter() : ip_limiter_(RateLimiter::Config{5, 10}),
                        user_limiter_(RateLimiter::Config{10, 20}) {}
    explicit DualRateLimiter(const Config& config)
        : ip_limiter_(config.ip_config),
          user_limiter_(config.user_config) {}

    bool check_ip(const std::string& ip) {
        return ip_limiter_.check(ip);
    }

    bool check_user(const std::string& username) {
        return user_limiter_.check(username);
    }

    bool check(const std::string& ip, const std::string& username) {
        if (!username.empty()) {
            return check_user(username);
        }
        return check_ip(ip);
    }

    void remove_ip(const std::string& ip) {
        ip_limiter_.remove(ip);
    }

    void remove_user(const std::string& username) {
        user_limiter_.remove(username);
    }

    void cleanup() {
        ip_limiter_.cleanup();
        user_limiter_.cleanup();
    }

private:
    RateLimiter ip_limiter_;
    RateLimiter user_limiter_;
};

} // namespace chat::server

