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
    ${SQLITE3_LIBRARIES}
    ${SODIUM_LIBRARIES}
)

target_include_directories(chat_server PRIVATE
    ${SQLITE3_INCLUDE_DIRS}
    ${SODIUM_INCLUDE_DIRS}
)

