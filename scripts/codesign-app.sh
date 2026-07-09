#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "usage: $0 <identity> <entitlements-plist> <app-bundle>" >&2
    exit 2
fi

IDENTITY="$1"
ENTITLEMENTS="$2"
APP="$3"

if [[ ! -d "$APP" ]]; then
    echo "codesign-app: app bundle not found: $APP" >&2
    exit 1
fi

if [[ ! -f "$ENTITLEMENTS" ]]; then
    echo "codesign-app: entitlements plist not found: $ENTITLEMENTS" >&2
    exit 1
fi

sign_one() {
    local path="$1"
    codesign --force --timestamp --options runtime \
        --sign "$IDENTITY" \
        --entitlements "$ENTITLEMENTS" \
        "$path"
}

is_macho() {
    local path="$1"
    [[ -f "$path" ]] || return 1
    file -b "$path" | grep -q 'Mach-O'
}

# 1. Nested Mach-Os under Resources/turmeric (bundled `tur` and any dylibs).
TURMERIC_DIR="$APP/Contents/Resources/turmeric"
if [[ -d "$TURMERIC_DIR" ]]; then
    while IFS= read -r -d '' candidate; do
        if is_macho "$candidate"; then
            echo "codesign-app: signing $candidate"
            sign_one "$candidate"
        fi
    done < <(find "$TURMERIC_DIR" -type f -print0)
fi

# 2. Any bundled frameworks (macdeployqt output, future).
FRAMEWORKS_DIR="$APP/Contents/Frameworks"
if [[ -d "$FRAMEWORKS_DIR" ]]; then
    while IFS= read -r -d '' fw; do
        echo "codesign-app: signing framework $fw"
        sign_one "$fw"
    done < <(find "$FRAMEWORKS_DIR" -maxdepth 1 -type d -name '*.framework' -print0)

    while IFS= read -r -d '' dylib; do
        echo "codesign-app: signing dylib $dylib"
        sign_one "$dylib"
    done < <(find "$FRAMEWORKS_DIR" -type f -name '*.dylib' -print0)
fi

# 3. Main executable.
MAIN_EXE="$APP/Contents/MacOS/trowel"
if [[ -f "$MAIN_EXE" ]]; then
    echo "codesign-app: signing main executable $MAIN_EXE"
    sign_one "$MAIN_EXE"
fi

# 4. Bundle.
echo "codesign-app: signing bundle $APP"
sign_one "$APP"

# 5. Verify.
echo "codesign-app: verifying signature"
codesign --verify --deep --strict --verbose=2 "$APP"

echo "codesign-app: assessing with spctl"
if ! spctl --assess --type execute --verbose "$APP"; then
    echo "codesign-app: spctl assessment failed (expected until notarization)" >&2
    exit 1
fi
