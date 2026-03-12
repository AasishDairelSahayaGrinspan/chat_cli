cmake_minimum_required(VERSION 3.16)
project(chat_cli VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build options
option(BUILD_SERVER "Build the chat server" ON)
option(BUILD_CLIENT "Build the chat client" ON)
option(ENABLE_REDIS "Enable Redis Pub/Sub support" OFF)

# Compiler flags - platform-aware
if(MSVC)
    add_compile_options(/W4 /permissive-)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2 /DNDEBUG)
    endif()
    add_compile_definitions(_WIN32_WINNT=0x0601)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror=return-type)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -DNDEBUG)
    else()
        add_compile_options(-g -O0)
    endif()
endif()

# Find required packages
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

# SQLite3 - try find_package first (works with vcpkg), fall back to pkg-config
find_package(SQLite3 QUIET)
if(NOT SQLite3_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SQLITE3 REQUIRED sqlite3)
    set(SQLITE3_TARGET_LIBS ${SQLITE3_LINK_LIBRARIES})
    set(SQLITE3_TARGET_INCLUDES ${SQLITE3_INCLUDE_DIRS})
else()
    set(SQLITE3_TARGET_LIBS SQLite::SQLite3)
    set(SQLITE3_TARGET_INCLUDES "")
endif()

# libsodium - try find_package first (works with vcpkg), fall back to pkg-config
find_package(unofficial-sodium QUIET)
if(unofficial-sodium_FOUND)
    set(SODIUM_TARGET_LIBS unofficial-sodium::sodium)
    set(SODIUM_TARGET_INCLUDES "")
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SODIUM REQUIRED libsodium)
        set(SODIUM_TARGET_LIBS ${SODIUM_LINK_LIBRARIES})
        set(SODIUM_TARGET_INCLUDES ${SODIUM_INCLUDE_DIRS})
    else()
        # Manual find for platforms without pkg-config
        find_library(SODIUM_LIB NAMES sodium libsodium)
        find_path(SODIUM_INC NAMES sodium.h)
        if(NOT SODIUM_LIB OR NOT SODIUM_INC)
            message(FATAL_ERROR "libsodium not found. Install it or set CMAKE_PREFIX_PATH.")
        endif()
        set(SODIUM_TARGET_LIBS ${SODIUM_LIB})
        set(SODIUM_TARGET_INCLUDES ${SODIUM_INC})
    endif()
endif()

# FetchContent for header-only libraries
include(FetchContent)

# Standalone Asio (header-only, no Boost dependency)
FetchContent_Declare(
    asio
    GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
    GIT_TAG asio-1-28-0
)
FetchContent_MakeAvailable(asio)

add_library(asio INTERFACE)
target_include_directories(asio INTERFACE ${asio_SOURCE_DIR}/asio/include)
target_compile_definitions(asio INTERFACE ASIO_STANDALONE ASIO_NO_DEPRECATED)
target_link_libraries(asio INTERFACE Threads::Threads)

# Windows needs Winsock for Asio
if(WIN32)
    target_link_libraries(asio INTERFACE ws2_32 wsock32)
endif()

# nlohmann/json
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

# spdlog
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.13.0
)

# FTXUI - Terminal UI library
FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG v5.0.0
    GIT_SHALLOW TRUE
)

FetchContent_MakeAvailable(json spdlog ftxui)

# Common library
add_subdirectory(common)

# Server and client
if(BUILD_SERVER)
    add_subdirectory(server)
endif()

if(BUILD_CLIENT)
    add_subdirectory(client)
endif()

