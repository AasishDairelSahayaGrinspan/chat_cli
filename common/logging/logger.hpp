#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/async.h>
#include <memory>
#include <string>
#include <iostream>

namespace chat::logging {

class Logger {
public:
    static void init(const std::string& name,
                    const std::string& log_file = "",
                    spdlog::level::level_enum level = spdlog::level::info) {
        try {
            std::vector<spdlog::sink_ptr> sinks;

            // Console sink with color
            auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            console_sink->set_level(level);
            console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
            sinks.push_back(console_sink);

            // File sink if specified
            if (!log_file.empty()) {
                auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    log_file, 10 * 1024 * 1024, 5);  // 10MB, 5 files
                file_sink->set_level(level);
                file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");
                sinks.push_back(file_sink);
            }

            auto logger = std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end());
            logger->set_level(level);
            logger->flush_on(spdlog::level::info);

            spdlog::set_default_logger(logger);
            spdlog::info("Logger initialized: {}", name);
        } catch (const spdlog::spdlog_ex& ex) {
            std::cerr << "Logger initialization failed: " << ex.what() << std::endl;
        }
    }

    static void set_level(spdlog::level::level_enum level) {
        spdlog::set_level(level);
    }

    static void shutdown() {
        spdlog::shutdown();
    }

    // Structured logging helpers
    template<typename... Args>
    static void info(const char* fmt, Args&&... args) {
        spdlog::info(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void warn(const char* fmt, Args&&... args) {
        spdlog::warn(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void error(const char* fmt, Args&&... args) {
        spdlog::error(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void debug(const char* fmt, Args&&... args) {
        spdlog::debug(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void trace(const char* fmt, Args&&... args) {
        spdlog::trace(fmt, std::forward<Args>(args)...);
    }

    template<typename... Args>
    static void critical(const char* fmt, Args&&... args) {
        spdlog::critical(fmt, std::forward<Args>(args)...);
    }
};

// Convenience macros for including source location
#define LOG_INFO(...) spdlog::info(__VA_ARGS__)
#define LOG_WARN(...) spdlog::warn(__VA_ARGS__)
#define LOG_ERROR(...) spdlog::error(__VA_ARGS__)
#define LOG_DEBUG(...) spdlog::debug(__VA_ARGS__)
#define LOG_TRACE(...) spdlog::trace(__VA_ARGS__)
#define LOG_CRITICAL(...) spdlog::critical(__VA_ARGS__)

} // namespace chat::logging

