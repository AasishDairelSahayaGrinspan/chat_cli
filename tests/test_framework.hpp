#pragma once

// Minimal dependency-free test harness.
//
// Usage:
//   TEST(my_case) { CHECK(1 + 1 == 2); }

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace chat_test {

struct Failure : std::runtime_error {
    explicit Failure(const std::string& msg) : std::runtime_error(msg) {}
};

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
    static std::vector<TestCase> cases;
    return cases;
}

struct Registrar {
    Registrar(const std::string& name, std::function<void()> fn) {
        registry().push_back({name, std::move(fn)});
    }
};

struct CurrentTest {
    std::string name;
};

inline CurrentTest& current() {
    static CurrentTest test;
    return test;
}

inline int run_all() {
    int failed = 0;
    for (const auto& test : registry()) {
        current().name = test.name;
        try {
            test.fn();
            std::cout << "[PASS] " << test.name << "\n";
        } catch (const std::exception& e) {
            ++failed;
            std::cout << "[FAIL] " << test.name << ": " << e.what() << "\n";
        } catch (...) {
            ++failed;
            std::cout << "[FAIL] " << test.name << ": unknown exception\n";
        }
    }
    std::cout << (failed == 0 ? "All tests passed (" : "Failures: " + std::to_string(failed) + " of ")
              << registry().size() << (failed == 0 ? ")" : "") << "\n";
    return failed == 0 ? 0 : 1;
}

inline std::string location(const char* file, int line) {
    return std::string(file) + ":" + std::to_string(line);
}

} // namespace chat_test

#define TEST(name)                                                        \
    static void chat_test_##name();                                       \
    static ::chat_test::Registrar chat_test_reg_##name(#name, chat_test_##name); \
    static void chat_test_##name()

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            throw ::chat_test::Failure(                                   \
                ::chat_test::location(__FILE__, __LINE__) +               \
                ": CHECK failed: " #cond);                                \
        }                                                                 \
    } while (0)

#define CHECK_EQ(a, b)                                                    \
    do {                                                                  \
        if (!((a) == (b))) {                                              \
            throw ::chat_test::Failure(                                   \
                ::chat_test::location(__FILE__, __LINE__) +               \
                ": CHECK_EQ failed: " #a " != " #b);                      \
        }                                                                 \
    } while (0)

#define CHECK_FALSE(cond) CHECK(!(cond))
