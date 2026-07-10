#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "usage: $0 <app-bundle> <keychain-profile>" >&2
    exit 2
fi

APP="$1"
PROFILE="$2"

if [[ ! -d "$APP" ]]; then
    echo "notarize-app: app bundle not found: $APP" >&2
    exit 1
fi

CODESIGN_INFO="$(codesign --display --verbose=2 "$APP" 2>&1)"
if ! grep -q 'flags=.*runtime' <<<"$CODESIGN_INFO"; then
    echo "notarize-app: bundle is not signed with the hardened runtime" >&2
    echo "notarize-app: run scripts/codesign-app.sh first" >&2
    exit 1
fi

DIST_DIR="$(dirname "$APP")/../../dist"
mkdir -p "$DIST_DIR"
DIST_DIR="$(cd "$DIST_DIR" && pwd)"

APP_NAME="$(basename "$APP" .app)"
ZIP="$DIST_DIR/${APP_NAME}.zip"

echo "notarize-app: zipping $APP -> $ZIP"
rm -f "$ZIP"
ditto -c -k --keepParent "$APP" "$ZIP"

echo "notarize-app: submitting to Apple notary service (this may take several minutes)"
SUBMIT_OUTPUT="$(xcrun notarytool submit "$ZIP" --keychain-profile "$PROFILE" --wait 2>&1)"
echo "$SUBMIT_OUTPUT"

STATUS="$(printf '%s\n' "$SUBMIT_OUTPUT" | awk '/^ *status:/ {print $2; exit}')"
SUBMISSION_ID="$(printf '%s\n' "$SUBMIT_OUTPUT" | awk '/^ *id:/ {print $2; exit}')"

if [[ "$STATUS" != "Accepted" ]]; then
    echo "notarize-app: notarization did not succeed (status=$STATUS)" >&2
    if [[ -n "$SUBMISSION_ID" ]]; then
        echo "notarize-app: fetching log for $SUBMISSION_ID" >&2
        xcrun notarytool log "$SUBMISSION_ID" --keychain-profile "$PROFILE" >&2 || true
    fi
    exit 1
fi

echo "notarize-app: stapling ticket to $APP"
xcrun stapler staple "$APP"

echo "notarize-app: validating stapled bundle"
xcrun stapler validate "$APP"

echo "notarize-app: gating with spctl"
spctl --assess --type execute --verbose "$APP"

echo "notarize-app: done"
