add_executable(chat_tests
    test_main.cpp
    test_room_crypto.cpp
    test_trust_store.cpp
    test_group_flow.cpp
    ${CMAKE_SOURCE_DIR}/client/group_manager.cpp
    ${CMAKE_SOURCE_DIR}/client/crypto.cpp
)

target_include_directories(chat_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/client
)

target_link_libraries(chat_tests PRIVATE chat_common)

if(SODIUM_TARGET_INCLUDES)
    target_include_directories(chat_tests PRIVATE ${SODIUM_TARGET_INCLUDES})
endif()

add_test(NAME chat_tests COMMAND chat_tests)
