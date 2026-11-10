cmake_minimum_required(VERSION 3.16)

project(BGAssistant VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        configform.cpp
        configform.h
        configform.ui
        lockscreen.cpp
        lockscreen.h
        aes.cpp
        aes.h
        resources.qrc
)

# exe 图标：CMake 没有 WINDOWS_ICON 这个 target 属性（写了也是静默忽略），
# 必须通过 .rc 资源脚本把图标编进 PE 资源段才会生效。
if(WIN32)
    enable_language(RC)
    # 生成到 build 目录并写入绝对路径：rc.exe 从 build 目录运行，
    # 相对路径会找不到源目录下的图标。
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/app.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
        @ONLY
    )
    list(APPEND PROJECT_SOURCES "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
endif()

qt_add_executable(BGAssistant
    MANUAL_FINALIZATION
    ${PROJECT_SOURCES}
)

target_link_libraries(BGAssistant PRIVATE Qt6::Widgets)

# 注意这里不能写 if(${QT_VERSION} ...)：变量为空时展开成 if( VERSION_LESS 6.1.0)，
# CMake 会直接语法报错。
set(BUNDLE_PROPS
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)
if(QT_VERSION VERSION_LESS 6.1.0)
    list(APPEND BUNDLE_PROPS MACOSX_BUNDLE_GUI_IDENTIFIER com.example.BGAssistant)
endif()

set_target_properties(BGAssistant PROPERTIES ${BUNDLE_PROPS})

include(GNUInstallDirs)
install(TARGETS BGAssistant
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

qt_finalize_executable(BGAssistant)
