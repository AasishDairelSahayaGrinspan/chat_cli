add_executable(chat_client
    main.cpp
    tls_client.cpp
    input_handler.cpp
    ui_renderer.cpp
    ftxui_app.cpp
    theme.cpp
    message_store.cpp
    banner.cpp
)

target_include_directories(chat_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(chat_client PRIVATE
    chat_common
    asio
    Threads::Threads
    OpenSSL::SSL
    OpenSSL::Crypto
    ftxui::screen
    ftxui::dom
    ftxui::component
)
