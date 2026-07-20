cmake_minimum_required(VERSION 3.24)

file(READ "${CMAKE_SOURCE_DIR}/VERSION" TROWEL_VERSION_RAW)
string(STRIP "${TROWEL_VERSION_RAW}" TROWEL_VERSION)

project(trowel
    VERSION ${TROWEL_VERSION}
    DESCRIPTION "Trowel — a native editor for turmeric"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

qt_standard_project_setup()

include(${CMAKE_SOURCE_DIR}/cmake/scintilla_qt.cmake)

qt_add_resources(TROWEL_RESOURCES resources/resources.qrc)

add_library(trowel_lib STATIC
    src/app/directory_view.cpp
    src/app/directory_view.h
    src/app/icon_font.cpp
    src/app/icon_font.h
    src/app/main_window.cpp
    src/app/main_window.h
    src/app/preferences_view.cpp
    src/app/preferences_view.h
    src/app/tab_bar.cpp
    src/app/tab_bar.h
    src/app/tab_content.h
    src/app/trowel_application.cpp
    src/app/trowel_application.h
    src/editor/editor_view.cpp
    src/editor/editor_view.h
    src/editor/turmeric_lexer.cpp
    src/editor/turmeric_lexer.h
    src/editor/theme_loader.cpp
    src/editor/theme_loader.h
    src/repl/pty_session.cpp
    src/repl/pty_session.h
    src/repl/terminal_view.cpp
    src/repl/terminal_view.h
    src/repl/repl_session.cpp
    src/repl/repl_session.h
    src/repl/run_buffer.cpp
    src/repl/run_buffer.h
    src/control/control_server.cpp
    src/control/control_server.h
    src/control/control_connection.cpp
    src/control/control_connection.h
    src/control/control_handlers.cpp
    src/control/control_handlers.h
    ${TROWEL_RESOURCES}
)
target_include_directories(trowel_lib PUBLIC src)
target_link_libraries(trowel_lib PUBLIC Qt6::Widgets Qt6::Network scintilla_qt)
target_compile_options(trowel_lib PRIVATE
    -Wall -Wextra -Wpedantic -Werror
)

set(TROWEL_APP_ICON "${CMAKE_SOURCE_DIR}/resources/trowel.icns")
set_source_files_properties("${TROWEL_APP_ICON}" PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources"
)

qt_add_executable(trowel
    src/main.cpp
    ${TROWEL_APP_ICON}
)
target_link_libraries(trowel PRIVATE trowel_lib)

if(APPLE)
    set_target_properties(trowel PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Trowel"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.turmeric-lang.TrowelEditor"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "trowel.icns"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/resources/Info.plist.in"
    )

    # Ship the `trowel` CLI shim inside the bundle so Homebrew's cask can
    # symlink it onto PATH. This must run BEFORE codesigning below — the
    # bundle signature seals Contents/Resources, so staging afterwards would
    # invalidate the signature. POST_BUILD commands run in the order added.
    add_custom_command(TARGET trowel POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy
            "${CMAKE_SOURCE_DIR}/resources/trowel-cli.sh"
            "$<TARGET_BUNDLE_CONTENT_DIR:trowel>/Resources/trowel"
        COMMAND chmod +x "$<TARGET_BUNDLE_CONTENT_DIR:trowel>/Resources/trowel"
        COMMENT "Staging trowel CLI shim into app bundle"
        VERBATIM
    )

    set(TROWEL_TURMERIC_VERSION "v0.29.1")
    set(TROWEL_TURMERIC_SHA256
        "0c6a9f46b86b68b3e92226ca293b476e2a42f4b1c05b1a5da31b2a6399155500")
    set(TROWEL_TURMERIC_URL
        "https://github.com/rjungemann/turmeric/releases/download/${TROWEL_TURMERIC_VERSION}/turmeric-${TROWEL_TURMERIC_VERSION}-macos-arm64.tar.gz")

    if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64" OR
       CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
        include(FetchContent)
        FetchContent_Declare(turmeric_prebuilt
            URL      ${TROWEL_TURMERIC_URL}
            URL_HASH SHA256=${TROWEL_TURMERIC_SHA256}
        )
        FetchContent_MakeAvailable(turmeric_prebuilt)

        add_custom_command(TARGET trowel POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${turmeric_prebuilt_SOURCE_DIR}"
                "$<TARGET_BUNDLE_CONTENT_DIR:trowel>/Resources/turmeric"
            COMMENT "Staging Turmeric ${TROWEL_TURMERIC_VERSION} into app bundle"
            VERBATIM
        )
    else()
        message(WARNING
            "No prebuilt Turmeric available for host ${CMAKE_HOST_SYSTEM_PROCESSOR}; "
            "Trowel.app will fall back to `tur` on PATH at runtime.")
    endif()

    set(TROWEL_CODESIGN_IDENTITY "" CACHE STRING
        "Developer ID Application identity (SHA1 or name) used to codesign Trowel.app. Empty disables signing.")
    set(TROWEL_CODESIGN_ENTITLEMENTS
        "${CMAKE_SOURCE_DIR}/resources/trowel.entitlements"
        CACHE FILEPATH
        "Path to the entitlements plist used when codesigning Trowel.app.")

    if(NOT TROWEL_CODESIGN_IDENTITY STREQUAL "")
        add_custom_command(TARGET trowel POST_BUILD
            COMMAND "${CMAKE_SOURCE_DIR}/scripts/codesign-app.sh"
                "${TROWEL_CODESIGN_IDENTITY}"
                "${TROWEL_CODESIGN_ENTITLEMENTS}"
                "$<TARGET_BUNDLE_DIR:trowel>"
            COMMENT "Codesigning Trowel.app with ${TROWEL_CODESIGN_IDENTITY}"
            VERBATIM
        )
    endif()
endif()
