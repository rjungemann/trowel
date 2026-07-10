# Changelog

All notable changes to Trowel are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions
follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

<!-- New releases are inserted immediately below this comment. -->

## [0.0.3] -- 2026-07-10

### Internal
- **VERSION file** -- moved the version source of truth from `CMakeLists.txt` to a top-level `VERSION` file, read at configure time.
- **Automated cask update** -- the release workflow now edits `Casks/trowel.rb` with the new version and SHA-256 automatically after publishing, and pushes to `main` via a bot commit.

## [0.0.2] -- 2026-07-10

Initial tagged release of Trowel. Snapshot of the editor as it stands today.

### Added
- **Editor core** -- Scintilla-based text editor with a custom Turmeric lexer and theme loader.
- **Directory view** -- sidebar file browser rooted at the open project directory.
- **Tab bar** -- scrollable tab bar with open-file management and reorderable tabs.
- **REPL pane** -- embedded terminal running a Turmeric REPL, toggleable via toolbar with switchable horizontal/vertical orientation and reload.
- **Toolbar** -- Show/Hide REPL, orientation toggle, and other view controls.
- **Turmeric bundled** -- `tur` binary embedded in the app bundle under `Contents/Resources/turmeric/`, so the REPL works out of the box.
- **Socket control API** -- external processes can drive the editor over a local socket (see `docs/plans/socket-api.md`).
- **App bundle** -- proper macOS `.app` with icon, hardened runtime, and Developer ID signing wired into the CMake build.
- **Notarization pipeline** -- `scripts/notarize-app.sh` submits, staples, and validates via `xcrun notarytool`.
- **Release automation** -- GitHub Actions workflow builds, signs, notarizes, and publishes a zipped `.app` on tag push; Homebrew Cask recipe at `Casks/trowel.rb`.

### Docs
- Signing and notarization guide at `docs/guides/signing-and-notarization.md`.
- Keyboard shortcuts reference at `docs/guides/keyboard-shortcuts.md`.
