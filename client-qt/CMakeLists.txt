cmake_minimum_required(VERSION 3.19)

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Network)
qt_standard_project_setup(REQUIRES 6.8)


qt_add_executable(client-qt
        WIN32 MACOSX_BUNDLE
        main.cpp
        mainwindow.cpp
        mainwindow.h
        HttpClient.h
        HttpClient.cpp
        mainwindow.ui
        logindialog.cpp
        logindialog.h
        logindialog.ui
        chatclient.cpp
        chatclient.h
        chat_types.h
)

target_include_directories(client-qt PRIVATE
        ${CMAKE_SOURCE_DIR}/chat-server/include
)

target_link_libraries(client-qt
        PRIVATE
        Qt::Core
        Qt::Widgets
        Qt6::Network
)

install(TARGETS client-qt
        BUNDLE DESTINATION .
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
        TARGET client-qt
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

# C1：以本地 TCP 对端验证认证后的历史首屏请求与分块接收。
qt_add_executable(history_client_test
        tests/history_client_test.cpp
        chatclient.cpp
        chatclient.h
        chat_types.h
)

target_include_directories(history_client_test PRIVATE
        ${CMAKE_SOURCE_DIR}/chat-server/include
)

target_link_libraries(history_client_test PRIVATE
        Qt::Core
        Qt6::Network
)

add_test(NAME history_client_test COMMAND history_client_test)

# C2.2：验证历史页与实时消息合并、以及本地消息确认后的实际会话显示顺序。
qt_add_executable(conversation_order_test
        tests/conversation_order_test.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        chatclient.cpp
        chatclient.h
        chat_types.h
)

target_include_directories(conversation_order_test PRIVATE
        ${CMAKE_SOURCE_DIR}/chat-server/include
)

target_link_libraries(conversation_order_test PRIVATE
        Qt::Core
        Qt::Widgets
        Qt6::Network
)

add_test(NAME conversation_order_test COMMAND conversation_order_test)
