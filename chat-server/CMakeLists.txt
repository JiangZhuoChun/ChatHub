cmake_minimum_required(VERSION 3.21)

# 所有目标共用的头文件搜索路径
set(CHAT_SERVER_INCLUDES
    ${ASIO_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_executable(chat-server
    app/main.cpp
    src/net/server.cpp
    src/net/session.cpp
    src/protocol/frame_decoder.cpp
#        src/protocol/chat_payload.cpp
)
target_include_directories(chat-server PRIVATE
        ${CHAT_SERVER_INCLUDES}
        ${JWT_INCLUDE_DIR}
        ${SQLITE3_INCLUDE_DIR}   # OpenSSL 头文件（opt/include 下）
)
target_link_directories(chat-server PRIVATE ${SQLITE3_LIB_DIR})
target_link_libraries(chat-server PRIVATE
        ws2_32
        mswsock
        crypto
)
target_compile_definitions(chat-server PRIVATE
        ASIO_STANDALONE # 告诉 asio "不用 Boost"
        _WIN32_WINNT=0x0A00 # 告诉 Windows 头文件 "支持 Win10 API"
)

add_executable(frame_decoder_test
        examples/frame_decoder_test.cpp
        src/protocol/frame_decoder.cpp
)
add_executable(jwt_demo
        examples/jwt_demo.cpp
)

target_include_directories(frame_decoder_test PRIVATE ${CHAT_SERVER_INCLUDES})
target_link_libraries(frame_decoder_test PRIVATE ws2_32 mswsock)
target_compile_definitions(frame_decoder_test PRIVATE
        ASIO_STANDALONE
        _WIN32_WINNT=0x0A00
)

add_test(NAME frame_decoder_test COMMAND frame_decoder_test)

# jwt_demo：验证 JWT 库可用（W7）
target_include_directories(jwt_demo PRIVATE
    ${CHAT_SERVER_INCLUDES}
    ${JWT_INCLUDE_DIR}
    ${SQLITE3_INCLUDE_DIR}   # OpenSSL 头文件（opt/include 下）
)
target_link_directories(jwt_demo PRIVATE ${SQLITE3_LIB_DIR})
target_link_libraries(jwt_demo PRIVATE crypto)
target_compile_definitions(jwt_demo PRIVATE
    ASIO_STANDALONE
    _WIN32_WINNT=0x0A00
)
add_test(NAME jwt_demo COMMAND jwt_demo)
