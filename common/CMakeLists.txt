add_library(chat_common STATIC
    crypto/room_crypto.cpp
    crypto/trust_store.cpp
)

target_include_directories(chat_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(chat_common PUBLIC
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    OpenSSL::SSL
    OpenSSL::Crypto
    ${SODIUM_TARGET_LIBS}
)

if(SODIUM_TARGET_INCLUDES)
    target_include_directories(chat_common PUBLIC ${SODIUM_TARGET_INCLUDES})
endif()
