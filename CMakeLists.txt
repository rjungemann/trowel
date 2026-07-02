cmake_minimum_required(VERSION 3.24)
project(trowel
    VERSION 0.0.1
    DESCRIPTION "Trowel — a native editor for turmeric"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

include(${CMAKE_SOURCE_DIR}/cmake/scintilla_qt.cmake)

add_library(trowel_lib STATIC
    src/app/main_window.cpp
    src/app/main_window.h
    src/editor/editor_view.cpp
    src/editor/editor_view.h
    src/repl/pty_session.cpp
    src/repl/pty_session.h
    src/repl/terminal_view.cpp
    src/repl/terminal_view.h
    src/repl/repl_session.cpp
    src/repl/repl_session.h
)
target_include_directories(trowel_lib PUBLIC src)
target_link_libraries(trowel_lib PUBLIC Qt6::Widgets scintilla_qt)
target_compile_options(trowel_lib PRIVATE
    -Wall -Wextra -Wpedantic -Werror
)

qt_add_executable(trowel
    src/main.cpp
)
target_link_libraries(trowel PRIVATE trowel_lib)

if(APPLE)
    set_target_properties(trowel PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Trowel"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.turmeric.trowel"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    )
endif()
