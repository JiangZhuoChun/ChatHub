cmake_minimum_required(VERSION 3.21)

find_package(SQLite3 REQUIRED)

find_package(OpenSSL REQUIRED)

find_package(unofficial-libmariadb CONFIG REQUIRED)
# CMake 4.3 新增 SQLite3::SQLite3；旧版仅提供 SQLite::SQLite3。
if (NOT TARGET SQLite3::SQLite3 AND TARGET SQLite::SQLite3)
    add_library(SQLite3::SQLite3 ALIAS SQLite::SQLite3)
endif ()

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
        src/repository/sqlite_message_repository.cpp
        src/repository/mysql_connection_config.cpp
        src/repository/mysql_connection.cpp
        app/server_runtime_config.cpp
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
        OpenSSL::Crypto
        SQLite3::SQLite3
        unofficial::libmariadb
)

if (NOT DEFINED VCPKG_INSTALLED_DIR
        OR NOT DEFINED VCPKG_TARGET_TRIPLET)
    message(FATAL_ERROR
            "vcpkg manifest variables are required")
endif ()

set(mariadb_plugin_source_dir
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/plugins/libmariadb")

if (NOT EXISTS
        "${mariadb_plugin_source_dir}/caching_sha2_password.dll")
    message(FATAL_ERROR
            "MariaDB caching_sha2_password plugin is missing")
endif ()

add_custom_command(
        TARGET chat-server
        POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
        "$<TARGET_FILE_DIR:chat-server>/plugins/libmariadb"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${mariadb_plugin_source_dir}"
        "$<TARGET_FILE_DIR:chat-server>/plugins/libmariadb"
        VERBATIM
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
        examples/message_repository_contract_test_runner.cpp
        src/repository/sqlite_message_repository.cpp
)
configure_chat_server_target(message_repository_test)
target_link_libraries(message_repository_test PRIVATE
        SQLite3::SQLite3
)
register_chat_server_test(message_repository_test)

add_executable(history_response_test
        examples/history_response_test.cpp
        src/net/session.cpp
        src/protocol/frame_decoder.cpp
        src/protocol/chat_payload.cpp
        src/repository/sqlite_message_repository.cpp
)
configure_chat_server_target(history_response_test)
target_include_directories(history_response_test PRIVATE
        ${JWT_INCLUDE_DIR}
)
target_link_libraries(history_response_test PRIVATE
        ${CHAT_SERVER_NETWORK_LIBRARIES}
        OpenSSL::Crypto
        SQLite3::SQLite3
)
register_chat_server_test(history_response_test)

add_executable(server_runtime_config_test
        examples/server_runtime_config_test.cpp
        app/server_runtime_config.cpp
)
configure_chat_server_target(server_runtime_config_test)
target_include_directories(server_runtime_config_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/app
)
register_chat_server_test(server_runtime_config_test)

add_executable(server_process_config_test
        examples/server_process_config_test.cpp
)
configure_chat_server_target(server_process_config_test)
target_link_libraries(server_process_config_test PRIVATE
        ${CHAT_SERVER_NETWORK_LIBRARIES}
)
add_dependencies(server_process_config_test chat-server)
add_test(NAME server_process_config_test
        COMMAND $<TARGET_FILE:server_process_config_test> $<TARGET_FILE:chat-server>
)

add_executable(jwt_demo
        examples/jwt_demo.cpp
)
configure_chat_server_target(jwt_demo)
target_include_directories(jwt_demo PRIVATE
        ${JWT_INCLUDE_DIR}
)
target_link_libraries(jwt_demo PRIVATE
        OpenSSL::Crypto
)

add_executable(online_users_integration_test
        examples/online_users_integration_test.cpp
        src/net/server.cpp
        src/net/session.cpp
        src/protocol/frame_decoder.cpp
        src/protocol/chat_payload.cpp
        src/repository/sqlite_message_repository.cpp
)
configure_chat_server_target(online_users_integration_test)
target_include_directories(online_users_integration_test PRIVATE
        ${JWT_INCLUDE_DIR}
)
target_link_libraries(online_users_integration_test PRIVATE
        ${CHAT_SERVER_NETWORK_LIBRARIES}
        OpenSSL::Crypto
        SQLite3::SQLite3
)
register_chat_server_test(online_users_integration_test)

add_executable(mysql_connection_test
        examples/mysql_connection_test.cpp
        src/repository/mysql_connection_config.cpp
        src/repository/mysql_connection.cpp
)
configure_chat_server_target(mysql_connection_test)
target_link_libraries(mysql_connection_test PRIVATE
        unofficial::libmariadb
)
#构建测试前，先构建 chat-server，保证其 POST_BUILD 插件部署已执行。
add_dependencies(mysql_connection_test chat-server)
add_test(NAME mysql_connection_test
        COMMAND $<TARGET_FILE:mysql_connection_test>
        "$<TARGET_FILE_DIR:chat-server>/plugins/libmariadb"
)
set_tests_properties(mysql_connection_test PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)


add_executable(mysql_connection_integration_test
        examples/mysql_connection_integration_test.cpp
        src/repository/mysql_connection_config.cpp
        src/repository/mysql_connection.cpp
)
configure_chat_server_target(mysql_connection_integration_test)
target_link_libraries(mysql_connection_integration_test PRIVATE
        unofficial::libmariadb
)
add_dependencies(mysql_connection_integration_test chat-server)
add_test(NAME mysql_connection_integration_test
        COMMAND $<TARGET_FILE:mysql_connection_integration_test>
)
set_tests_properties(mysql_connection_integration_test PROPERTIES
        SKIP_RETURN_CODE 77
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
)