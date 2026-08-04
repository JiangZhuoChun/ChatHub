cmake_minimum_required(VERSION 3.21)
project(chathub LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 支持 chat-server 的 CTest
enable_testing()

# 独立 asio（W3 已验证路径）
set(ASIO_INCLUDE_DIR "D:/asio/asio/include")
# Boost（仅 chat_payload 的 JSON 解析用，asio 保持 standalone）
set(BOOST_INCLUDE_DIR "D:/CppLearn/third_party/boost_1_91_0")
# MinGW 自带 sqlite3
set(SQLITE3_INCLUDE_DIR "D:/QT/Tools/mingw1310_64/opt/include")
set(SQLITE3_LIB_DIR "D:/QT/Tools/mingw1310_64/opt/lib")

add_subdirectory(chat-server)
add_subdirectory(client-qt)
