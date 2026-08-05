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
)

target_link_libraries(client-qt
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt6::Network
)

install(TARGETS client-qt
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

qt_generate_deploy_app_script(
    TARGET client-qt
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
