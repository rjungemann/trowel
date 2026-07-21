#!/usr/bin/env bash
# Build a self-contained Trowel AppImage for the host architecture.
#
#   scripts/build-appimage.sh [ARCH]
#
# ARCH defaults to the host (`uname -m`): x86_64 or aarch64. Produces
# dist/Trowel-<version>-<arch>.AppImage bundling Qt (via linuxdeploy-plugin-qt),
# the pinned Turmeric toolchain (staged by CMake next to the binary), and
# tur's libedit dependency chain so the REPL runs on hosts without libedit2.
#
# Requires: cmake, ninja, a Qt6 dev install, curl, and FUSE *or* a kernel that
# allows AppImage extract-and-run (the script forces extract-and-run so it works
# in containers/CI without FUSE).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

ARCH="${1:-$(uname -m)}"
case "$ARCH" in
    x86_64|aarch64) ;;
    arm64) ARCH=aarch64 ;;
    *) echo "unsupported arch: $ARCH (expected x86_64 or aarch64)" >&2; exit 2 ;;
esac

TROWEL_VERSION="$(cat VERSION)"
BUILD_DIR="build/appimage-${ARCH}"
APPDIR="${BUILD_DIR}/AppDir"
TOOLS_DIR="build/appimage-tools"

# Run downloaded AppImage tools without FUSE (containers/CI).
export APPIMAGE_EXTRACT_AND_RUN=1
# Let linuxdeploy stamp the version into the output filename.
export VERSION="$TROWEL_VERSION"

echo "==> Configuring + building Trowel (Release, ${ARCH})"
cmake -S . -B "$BUILD_DIR" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    ${CMAKE_PREFIX_PATH:+-DCMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH"}
cmake --build "$BUILD_DIR"

echo "==> Installing into AppDir"
rm -rf "$APPDIR"
DESTDIR="$PWD/$APPDIR" cmake --install "$BUILD_DIR" --prefix /usr >/dev/null

echo "==> Fetching linuxdeploy + qt plugin (${ARCH})"
mkdir -p "$TOOLS_DIR"
fetch() {  # fetch <url> <dest>
    local url="$1" dest="$2"
    if [ ! -x "$dest" ]; then
        curl -fsSL -o "$dest" "$url"
        chmod +x "$dest"
    fi
}
BASE="https://github.com/linuxdeploy"
fetch "${BASE}/linuxdeploy/releases/download/continuous/linuxdeploy-${ARCH}.AppImage" \
      "${TOOLS_DIR}/linuxdeploy-${ARCH}.AppImage"
fetch "${BASE}/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${ARCH}.AppImage" \
      "${TOOLS_DIR}/linuxdeploy-plugin-qt-${ARCH}.AppImage"
fetch "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-${ARCH}.AppImage" \
      "${TOOLS_DIR}/appimagetool-${ARCH}.AppImage"

# Help the Qt plugin find the right Qt (qmake6 on Debian/Ubuntu).
if [ -z "${QMAKE:-}" ]; then
    QMAKE="$(command -v qmake6 || command -v qmake || true)"
fi
export QMAKE

# Bundle tur's libedit dependency chain (libedit -> libtinfo/libbsd/libmd) so
# the bundled REPL runs on hosts that don't ship libedit2. linuxdeploy pulls in
# transitive deps automatically.
LIBEDIT="$(ldd "$APPDIR/usr/bin/turmeric/tur" 2>/dev/null | awk '/libedit/{print $3}')"
EXTRA_LIB_ARGS=()
if [ -n "$LIBEDIT" ] && [ -e "$LIBEDIT" ]; then
    EXTRA_LIB_ARGS+=(--library "$LIBEDIT")
    echo "==> Bundling tur runtime lib: $LIBEDIT"
else
    echo "==> WARNING: libedit for tur not found; REPL may fail on minimal hosts" >&2
fi

echo "==> Deploying Qt + libraries into AppDir (linuxdeploy)"
# Deploy only (no --output): re-running linuxdeploy over an already-deployed
# AppDir is unreliable, so we package separately with appimagetool below.
"${REPO_ROOT}/${TOOLS_DIR}/linuxdeploy-${ARCH}.AppImage" \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/trowel" \
    --desktop-file "$APPDIR/usr/share/applications/trowel.desktop" \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/trowel.png" \
    "${EXTRA_LIB_ARGS[@]}" \
    --plugin qt

# linuxdeploy-plugin-qt bundles the xcb platform plugin but not offscreen. Add
# it so the AppImage can run headlessly (CI smoke tests, control-socket
# automation). Its Qt deps resolve via the AppRun-set LD_LIBRARY_PATH.
QT_PLUGINS="$("$QMAKE" -query QT_INSTALL_PLUGINS 2>/dev/null || true)"
OFFSCREEN="${QT_PLUGINS}/platforms/libqoffscreen.so"
if [ -e "$OFFSCREEN" ]; then
    cp -f "$OFFSCREEN" "$APPDIR/usr/plugins/platforms/"
    echo "==> Bundled offscreen platform plugin"
else
    echo "==> WARNING: offscreen platform plugin not found; AppImage won't run headless" >&2
fi

echo "==> Packaging AppImage (appimagetool)"
mkdir -p dist
ARCH="$ARCH" "${REPO_ROOT}/${TOOLS_DIR}/appimagetool-${ARCH}.AppImage" \
    "$APPDIR" "dist/Trowel-${TROWEL_VERSION}-${ARCH}.AppImage"

echo "==> Built dist/Trowel-${TROWEL_VERSION}-${ARCH}.AppImage"
