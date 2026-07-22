# Changelog

All notable changes to Trowel are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions
follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

<!-- New releases are inserted immediately below this comment. -->

## [0.0.7] -- 2026-07-22

### Changed
- **Bundled Turmeric v0.30.4** -- updated the embedded `tur` compiler/REPL from v0.30.3 to v0.30.4.

## [0.0.6] -- 2026-07-21

### Added
- **Rainbow brackets** -- matching parentheses, square brackets, and curly braces are colored by nesting depth (cycling through seven colors), so it's easy to see which delimiters pair up. Unmatched closing brackets are flagged in red. Toggle it from **Trowel Settings** (on by default).

### Changed
- **Bundled Turmeric v0.30.3** -- updated the embedded `tur` compiler/REPL from v0.30.2 to v0.30.3.

### Fixed
- **REPL stdlib mismatch** -- the REPL now pins `TUR_STDLIB_DIR` to the stdlib shipped alongside the launched `tur` binary, so a stale ambient environment (e.g. a mise export) can no longer point the bundled REPL at a mismatched stdlib version.

## [0.0.5] -- 2026-07-21

### Added
- **Linux support** -- Trowel now runs on Linux, distributed as a self-contained `Trowel-<version>-<arch>.AppImage` (x86_64 and aarch64) that bundles Qt, Turmeric, and libedit; the release workflow builds and publishes these alongside the macOS bundle.
- **Desktop integration (Linux)** -- ships a `.desktop` launcher, XDG MIME registration for `.tur`/`.sweet` files, AppStream metainfo, and a hicolor icon set.
- **Single-instance forwarding (Linux)** -- a second `trowel foo.tur` forwards its files to the running window over the control socket and opens them as tabs, mirroring the macOS behavior.

### Changed
- **Bundled Turmeric v0.30.2** -- updated the embedded `tur` compiler/REPL from v0.29.1 to v0.30.2.

## [0.0.4] -- 2026-07-19

### Added
- **`trowel` CLI command** -- Homebrew now installs a `trowel` launcher (like `code`) that opens files as new tabs in the running Trowel window; passing multiple files opens multiple tabs.
- **macOS document types** -- Trowel registers as an editor for `.tur` and `.tur.sweet` files, so you can double-click them in Finder or use "Open With".

### Changed
- **Bundled Turmeric v0.29.1** -- updated the embedded `tur` compiler/REPL from v0.27.0 to v0.29.1.

### Fixed
- **Tab bar borders** -- the last tab is now closed off with a border, and the trailing border is hidden when the tab bar is scrollable.

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
