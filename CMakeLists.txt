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
    src/app/single_instance.cpp
    src/app/single_instance.h
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

# ---- Bundle a prebuilt Turmeric toolchain next to the editor ----------------
# Trowel ships a pinned `tur` so a fresh install has a working REPL / run-file
# without the user installing Turmeric separately. Each release tarball's layout
# (`tur` and `stdlib/` side by side) matches what ResolveTurBinary() /
# bundledTurPath() (src/repl/repl_session.cpp) probe for:
#   macOS: Trowel.app/Contents/Resources/turmeric/tur
#   Linux: <dir-of-trowel-binary>/turmeric/tur
# `tur` locates its stdlib by walking up from its own directory, so no env var
# is needed. When no prebuilt matches the host, we skip bundling and fall back
# to `tur` on PATH at runtime.
# Intentionally a normal (non-cache) variable so bumping it here is the single
# source of truth: the per-arch SHA-256s below are pinned to this exact tag, so
# the version must not be independently overridable via -D (which would leave a
# stale cached value pointing at mismatched hashes).
set(TROWEL_TURMERIC_VERSION "v0.30.2")

# Select the release asset + its SHA-256 for this platform/arch. Hashes are from
# the release's sha256sums.txt.
set(_turmeric_key "")
if(APPLE)
    if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64" OR
       CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
        set(_turmeric_key "macos-arm64")
        set(_turmeric_sha "a1b996e63af290955860c5f955f297515e3ec79341ad4fb4134196b1aa7c9891")
    endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
        set(_turmeric_key "linux-x86_64")
        set(_turmeric_sha "cf6d6f6c1df2daba16f9919cdf1e1ccc8bb7f7272a9fa0973daff7d6e94b2d71")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(_turmeric_key "linux-aarch64")
        set(_turmeric_sha "ac7722884bfbdc64f69033aed3e2491716bd1c229fea91e83fa7eded573cc037")
    endif()
endif()

if(_turmeric_key)
    include(FetchContent)
    FetchContent_Declare(turmeric_prebuilt
        URL      "https://github.com/rjungemann/turmeric/releases/download/${TROWEL_TURMERIC_VERSION}/turmeric-${TROWEL_TURMERIC_VERSION}-${_turmeric_key}.tar.gz"
        URL_HASH SHA256=${_turmeric_sha}
    )
    FetchContent_MakeAvailable(turmeric_prebuilt)

    # Stage next to the built binary. On macOS this lands inside the app bundle
    # BEFORE codesigning (POST_BUILD commands run in the order added, and the
    # codesign command below is added later), so the signature seals it in.
    if(APPLE)
        set(_turmeric_stage_dir "$<TARGET_BUNDLE_CONTENT_DIR:trowel>/Resources/turmeric")
    else()
        set(_turmeric_stage_dir "$<TARGET_FILE_DIR:trowel>/turmeric")
    endif()

    add_custom_command(TARGET trowel POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${turmeric_prebuilt_SOURCE_DIR}" "${_turmeric_stage_dir}"
        COMMENT "Staging Turmeric ${TROWEL_TURMERIC_VERSION} (${_turmeric_key}) next to trowel"
        VERBATIM
    )
else()
    message(WARNING
        "No prebuilt Turmeric for ${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}; "
        "Trowel will fall back to `tur` on PATH at runtime.")
endif()

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

# ---- Linux install layout ---------------------------------------------------
# Install the binary with the bundled Turmeric toolchain as a sibling `turmeric/`
# directory, matching the runtime probe (<dir-of-binary>/turmeric/tur). This is
# what the AppImage recipe (docs/guides) stages into the AppDir. Desktop-file,
# icon, and MIME integration are added in a later step.
if(NOT APPLE)
    include(GNUInstallDirs)
    install(TARGETS trowel RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    if(_turmeric_key)
        install(DIRECTORY "${turmeric_prebuilt_SOURCE_DIR}/"
                DESTINATION "${CMAKE_INSTALL_BINDIR}/turmeric"
                USE_SOURCE_PERMISSIONS)
    endif()

    # Desktop integration: .desktop launcher, XDG MIME type for .tur/.sweet, and
    # the hicolor icon set. These land in the standard XDG data locations; the
    # AppImage recipe (scripts/build-appimage.sh) installs into an AppDir and
    # linuxdeploy picks them up from here.
    install(FILES "${CMAKE_SOURCE_DIR}/resources/linux/trowel.desktop"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
    install(FILES "${CMAKE_SOURCE_DIR}/resources/linux/trowel.xml"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/mime/packages")
    install(FILES "${CMAKE_SOURCE_DIR}/resources/linux/com.turmeric_lang.Trowel.metainfo.xml"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/metainfo")
    foreach(_sz 32 48 64 128 256 512)
        install(FILES
            "${CMAKE_SOURCE_DIR}/resources/linux/icons/hicolor/${_sz}x${_sz}/apps/trowel.png"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/${_sz}x${_sz}/apps")
    endforeach()
endif()
