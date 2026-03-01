add_library(chat_common INTERFACE)

target_include_directories(chat_common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(chat_common INTERFACE
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    OpenSSL::SSL
    OpenSSL::Crypto
    ${SODIUM_LIBRARIES}
)

target_include_directories(chat_common INTERFACE
    ${SODIUM_INCLUDE_DIRS}
)

