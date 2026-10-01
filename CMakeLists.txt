cmake_minimum_required(VERSION 3.16)

project(motor VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Widgets
    Network
    Charts
)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(motor
    WIN32

    main.cpp
    app_icon.rc

    mainwindow.cpp
    mainwindow.h
    mainwindow.ui

    dataframe.h

    dataparser.cpp
    dataparser.h

    datacache.h
    recordfiledialog.h
    recorddatasource.h
    recorddatasource.cpp
)

target_link_libraries(motor
    PRIVATE
        Qt::Core
        Qt::Widgets
        Qt::Network
        Qt::Charts
)

include(GNUInstallDirs)

install(TARGETS motor
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_generate_deploy_app_script(
    TARGET motor
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)

install(SCRIPT ${deploy_script})