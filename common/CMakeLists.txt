add_library(chat_common INTERFACE)

target_include_directories(chat_common INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(chat_common INTERFACE
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    OpenSSL::SSL
    OpenSSL::Crypto
    ${SODIUM_TARGET_LIBS}
)

if(SODIUM_TARGET_INCLUDES)
    target_include_directories(chat_common INTERFACE ${SODIUM_TARGET_INCLUDES})
endif()

