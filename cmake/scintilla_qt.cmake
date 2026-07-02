# Build Scintilla's Qt binding (ScintillaEdit + ScintillaEditBase + core) as a
# static library. Fetches the Scintilla source tarball on first configure.
#
# Exposes: target `scintilla_qt`.

include(FetchContent)

set(SCINTILLA_VERSION 555 CACHE STRING "Scintilla version (concatenated, e.g. 555 for 5.5.5)")

FetchContent_Declare(scintilla
    URL https://www.scintilla.org/scintilla${SCINTILLA_VERSION}.tgz
    URL_HASH SHA256=0941a8c309a172da9cbb2a2731f97950cb60446cb4709073f9e296e9a6d4c1ae
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(scintilla)

set(SCI_ROOT "${scintilla_SOURCE_DIR}")

find_package(Qt6 REQUIRED COMPONENTS Widgets Core5Compat)

file(GLOB SCI_CORE_SRC CONFIGURE_DEPENDS "${SCI_ROOT}/src/*.cxx")

set(SCI_QT_SRC
    ${SCI_ROOT}/qt/ScintillaEditBase/PlatQt.cpp
    ${SCI_ROOT}/qt/ScintillaEditBase/ScintillaQt.cpp
    ${SCI_ROOT}/qt/ScintillaEditBase/ScintillaEditBase.cpp
    ${SCI_ROOT}/qt/ScintillaEdit/ScintillaEdit.cpp
    ${SCI_ROOT}/qt/ScintillaEdit/ScintillaDocument.cpp
)

add_library(scintilla_qt STATIC ${SCI_CORE_SRC} ${SCI_QT_SRC})

set_target_properties(scintilla_qt PROPERTIES
    AUTOMOC ON
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(scintilla_qt
    PUBLIC
        ${SCI_ROOT}/include
        ${SCI_ROOT}/qt/ScintillaEdit
        ${SCI_ROOT}/qt/ScintillaEditBase
        ${SCI_ROOT}/src
)

target_compile_features(scintilla_qt PUBLIC cxx_std_17)

target_link_libraries(scintilla_qt PUBLIC
    Qt6::Widgets
    Qt6::Core5Compat
)

# Vendored code — do not fail our build on Scintilla's own warnings.
target_compile_options(scintilla_qt PRIVATE -w)
