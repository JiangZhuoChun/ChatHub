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
target_include_directories(chat-server PRIVATE ${CHAT_SERVER_INCLUDES})
target_link_libraries(chat-server PRIVATE ws2_32 mswsock)
target_compile_definitions(chat-server PRIVATE
        ASIO_STANDALONE # 告诉 asio "不用 Boost"
        _WIN32_WINNT=0x0A00 # 告诉 Windows 头文件 "支持 Win10 API"
)

add_executable(frame_decoder_test
    examples/frame_decoder_test.cpp
        src/protocol/frame_decoder.cpp
)
target_include_directories(frame_decoder_test PRIVATE ${CHAT_SERVER_INCLUDES})
target_link_libraries(frame_decoder_test PRIVATE ws2_32 mswsock)
target_compile_definitions(frame_decoder_test PRIVATE
        ASIO_STANDALONE
        _WIN32_WINNT=0x0A00
)

add_test(NAME frame_decoder_test COMMAND frame_decoder_test)