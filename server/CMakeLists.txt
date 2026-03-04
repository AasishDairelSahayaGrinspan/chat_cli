add_executable(chat_server
    main.cpp
    network/tls_server.cpp
    network/connection.cpp
    network/connection_manager.cpp
    session/session_manager.cpp
    session/rate_limiter.cpp
    auth/auth_service.cpp
    commands/command_handler.cpp
    commands/room_manager.cpp
    storage/sqlite_storage.cpp
    health/health_endpoint.cpp
)

target_include_directories(chat_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(chat_server PRIVATE
    chat_common
    asio
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    ${SQLITE3_TARGET_LIBS}
    ${SODIUM_TARGET_LIBS}
)

if(SQLITE3_TARGET_INCLUDES)
    target_include_directories(chat_server PRIVATE ${SQLITE3_TARGET_INCLUDES})
endif()
if(SODIUM_TARGET_INCLUDES)
    target_include_directories(chat_server PRIVATE ${SODIUM_TARGET_INCLUDES})
endif()

