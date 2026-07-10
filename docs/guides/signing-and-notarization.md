# Signing & Notarization

How to produce a distributable, Gatekeeper-friendly `Trowel.app` on macOS.

Signing is wired into the CMake build; notarization is a manual follow-up
step against an already-signed bundle. See
`docs/plans/codesigning-plan.md` and `docs/plans/notarization-plan.md` for
the design behind this.

## One-time setup

1. Confirm the Developer ID Application identity is in the login keychain:
   ```
   security find-identity -v -p codesigning
   ```
   Expected:
   ```
   C5E89348AACF6E5FD50B38432E4D410B322EA8A1 "Developer ID Application: Roger Bradley-Jungemann (N486ZZ4H36)"
   ```
2. Store notarization credentials once (only needed for the notarize step):
   ```
   xcrun notarytool store-credentials trowel-notary \
       --apple-id "roger@teamsketchy.com" \
       --team-id N486ZZ4H36 \
       --password "<app-specific-password>"
   ```
   Generate the app-specific password at appleid.apple.com. The keychain
   profile name `trowel-notary` is what the notarize script expects.

## Signed build

Point CMake at the signing identity when configuring. Empty identity means
"do not sign" — the default, so unsigned dev builds still work.

```
cmake -S . -B build/macos-debug \
    -DTROWEL_CODESIGN_IDENTITY=C5E89348AACF6E5FD50B38432E4D410B322EA8A1
cmake --build build/macos-debug
```

The POST_BUILD hook in `CMakeLists.txt` runs `scripts/codesign-app.sh`,
which signs inside-out (bundled `tur` and any dylibs first, then the main
executable, then the bundle) with `--options runtime` and the entitlements
plist at `resources/trowel.entitlements`. On success:

```
codesign-app: verifying signature
… valid on disk
… satisfies its Designated Requirement
codesign-app: assessing with spctl
… accepted
source=Developer ID
```

Override the entitlements path with `-DTROWEL_CODESIGN_ENTITLEMENTS=<path>`
if you need custom entitlements for a build.

### Verifying by hand

```
codesign --verify --deep --strict --verbose=2 build/macos-debug/trowel.app
codesign --display --verbose=2 build/macos-debug/trowel.app
spctl --assess --type execute --verbose build/macos-debug/trowel.app
```

## Notarization & stapling

Signing is enough to launch locally without warnings, but a user who
downloads the app will hit a Gatekeeper block until the bundle is
notarized and stapled. `scripts/notarize-app.sh` runs the full flow —
zip with `ditto`, `notarytool submit --wait`, `stapler staple`,
`stapler validate`, and a final `spctl --assess` gate:

```
scripts/notarize-app.sh build/macos-debug/trowel.app trowel-notary
```

The submit step blocks on Apple's notary service (typically 1–3 minutes).
On failure the script fetches the submission log and exits non-zero.
Re-running against an already-stapled bundle is safe — Apple re-issues a
ticket and the staple is refreshed.

After stapling, `spctl --assess` reports `source=Notarized Developer ID`
and the stapled `.app` opens on any Mac without a network round-trip.

### Quarantine dry-run

To simulate what a downloaded copy sees, apply the quarantine attribute
Safari would set and open it from Finder:

```
cp -R build/macos-debug/trowel.app /tmp/Trowel.app
xattr -w com.apple.quarantine \
    "0083;$(printf '%x' $(date +%s));Safari;" /tmp/Trowel.app
open /tmp/Trowel.app
```

A properly notarized bundle launches with no "cannot be opened because
Apple cannot check it for malicious software" dialog. If that dialog
appears, the bundle is signed but not notarized (or the staple is
missing).

## Troubleshooting

- **`codesign` fails with "unable to build chain to self-signed root"** —
  the Developer ID certificate lost its issuing intermediate. Reinstall the
  "Developer ID - G2" intermediate from Apple's PKI page.
- **App launches locally but crashes on a fresh Mac** — check Console.app
  for hardened-runtime rejections. The entitlements plist starts empty;
  add `com.apple.security.cs.disable-library-validation` first if a
  bundled binary dlopens third-party plugins, or the JIT/unsigned-memory
  entitlements only when a specific failure demands them.
- **`notarytool submit` returns `Invalid`** — fetch the log:
  ```
  xcrun notarytool log <submission-id> --keychain-profile trowel-notary
  ```
  The usual culprits are an unsigned Mach-O in `Contents/Resources/` or a
  binary missing the hardened runtime flag.
- **Unsigned dev builds** — omit `-DTROWEL_CODESIGN_IDENTITY` (or set it
  to `""`) and the POST_BUILD signing hook is skipped entirely.
