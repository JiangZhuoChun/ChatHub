cmake_minimum_required(VERSION 3.21)

find_package(SQLite3 REQUIRED)

# 服务端可执行程序、示例和测试共享的编译配置。
set(CHAT_SERVER_INCLUDE_DIRS
    ${ASIO_INCLUDE_DIR}
    ${BOOST_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

set(CHAT_SERVER_NETWORK_LIBRARIES
    ws2_32
    mswsock
)

set(CHAT_SERVER_COMPILE_DEFINITIONS
    ASIO_STANDALONE
    _WIN32_WINNT=0x0A00
)

function(configure_chat_server_target target_name)
    target_include_directories(${target_name} PRIVATE
        ${CHAT_SERVER_INCLUDE_DIRS}
    )
    target_compile_definitions(${target_name} PRIVATE
        ${CHAT_SERVER_COMPILE_DEFINITIONS}
    )
endfunction()

function(register_chat_server_test target_name)
    add_test(NAME ${target_name} COMMAND $<TARGET_FILE:${target_name}>)
endfunction()

# ==================== ChatServer ====================
add_executable(chat-server
    app/main.cpp
    src/net/server.cpp
    src/net/session.cpp
    src/protocol/frame_decoder.cpp
    src/protocol/chat_payload.cpp
    src/repository/message_repository.cpp
)
configure_chat_server_target(chat-server)
target_include_directories(chat-server PRIVATE
    ${JWT_INCLUDE_DIR}
)
target_link_directories(chat-server PRIVATE
    ${SQLITE3_LIB_DIR}
)
target_link_libraries(chat-server PRIVATE
    ${CHAT_SERVER_NETWORK_LIBRARIES}
    crypto
    SQLite3::SQLite3
)

# ==================== CTest ====================
add_executable(frame_decoder_test
    examples/frame_decoder_test.cpp
    src/protocol/frame_decoder.cpp
    src/protocol/chat_payload.cpp
)
configure_chat_server_target(frame_decoder_test)
target_link_libraries(frame_decoder_test PRIVATE
    ${CHAT_SERVER_NETWORK_LIBRARIES}
)
register_chat_server_test(frame_decoder_test)

add_executable(message_repository_test
    examples/message_repository_test.cpp
    src/repository/message_repository.cpp
)
configure_chat_server_target(message_repository_test)
target_link_libraries(message_repository_test PRIVATE
    SQLite3::SQLite3
)
register_chat_server_test(message_repository_test)

add_executable(jwt_demo
    examples/jwt_demo.cpp
)
configure_chat_server_target(jwt_demo)
target_include_directories(jwt_demo PRIVATE
    ${JWT_INCLUDE_DIR}
)
target_link_directories(jwt_demo PRIVATE
    ${SQLITE3_LIB_DIR}
)
target_link_libraries(jwt_demo PRIVATE
    crypto
)
register_chat_server_test(jwt_demo)
