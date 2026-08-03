# Changelog

All notable changes to Trowel are documented here. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions
follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

<!-- New releases are inserted immediately below this comment. -->

## [Unreleased]

### Changed
- **Bundled Turmeric v0.33.2** -- updated the embedded `tur` compiler/REPL from v0.32.6.

## [0.1.2] -- 2026-07-31

### Added
- **Vertical toolbar** -- the horizontal toolbar is now a vertical icon bar down the left edge of the window, with the settings menu at its foot. Buttons are flat, carry their action's tooltip, and grey out with it.
- **Build Project** -- a `build.tur` manifest describes a project, not a script, so **Run > Build Project** runs `tur build` as a child process and echoes its output into the same terminal as the REPL.
- **Sweet-expression support** -- `.tur.sweet` files get their own indentation-aware highlighting, and `#lang` lines are honoured the way the toolchain honours them.
- **More languages highlighted** -- Python, shell, TOML, and CMake each get a real scanner, joining Turmeric, C, JSON, Markdown, and Justfiles. The dark theme was extended to cover them.

### Changed
- **Evaluation is gated by file type** -- Run/evaluate is a Turmeric-only action, so non-Turmeric documents grey it out, and opening or saving-as a `build.tur` retargets Run at the project build instead of loading the manifest into the REPL.

## [0.1.1] -- 2026-07-27

### Fixed
- **Crash when closing a window** -- tearing down a window destroyed its editor views after the buffer list was already gone, and the still-live `LspManager::diagnosticsUpdated` connection then read freed memory. The connection is now severed before teardown begins.

## [0.1.0] -- 2026-07-27

### Added
- **Language intelligence** -- Trowel now speaks LSP to the bundled `tur` language server: inline diagnostics (squiggles plus gutter markers), completion, and hover. Analysis is debounced as you type, and a file open in two windows shares one document with the server.
- **Multi-language syntax highlighting** -- Turmeric, C, JSON, Markdown, and Justfiles each get a real scanner, replacing the single Turmeric-only lexer. Themes were reworked to match.
- **Multiple windows** -- Trowel can now open more than one window. **File > New Window** (`Ctrl/Cmd+Shift+N`) opens an empty one, **Close Window** (`Ctrl/Cmd+Shift+W`) closes it, and the new **Window** menu lists everything open. Each window has its own tabs and its own REPL. See [Windows, tabs & the REPL](docs/guides/windows-tabs-and-repl.md).
- **Open-in-window rules** -- opening a file with no window open creates one; with a window open it becomes a tab in that window; opening a file that is already open focuses its existing tab instead of duplicating it. This applies to the File menu, the `trowel` command line, Finder, and single-instance forwarding alike.
- **Drag and drop** -- dropping a file onto a window replaces the current tab (prompting first if it has unsaved changes), or fills the tab when the window is empty. Dropping several files replaces the current tab with the first and opens the rest as tabs; dropping a directory opens the directory browser.
- **Per-window session restore** -- quitting now remembers every open window (tabs, active tab, geometry, REPL visibility) and restores them all on the next launch. Closing a window removes it from the saved session. Existing single-window sessions migrate automatically.
- **macOS app lifecycle** -- closing the last window leaves Trowel running in the dock, as Mac apps normally do; a dock click or an opened document brings a window back. Linux and Windows continue to exit with the last window.
- **REPL working-directory indicator** -- the REPL's startup banner now names the directory it is rooted in (`[trowel] tur repl started in ~/projects/foo`, with `$HOME` shown as `~`), so the working directory is visible rather than something you infer. Restarting the REPL reports the new directory too.
- **Run > Restart REPL In…** -- pick any directory and restart this window's REPL there. A running process cannot be moved between directories, so changing it necessarily restarts the REPL (clearing its state) -- which is why the menu entry says "Restart". The chosen directory holds until the next plain **Restart REPL**, which returns to the current file's directory.
- **Follows the REPL's own `:cd`** -- when the REPL reports a working-directory change (OSC 7, emitted by `tur repl`'s `:cd`), Trowel updates what it reports so the two stay in agreement. Unlike the menu commands this needs no restart, so REPL state survives. Inert with a `tur` that does not emit it.

### Changed
- **Bundled Turmeric v0.32.2** -- updated the embedded `tur` compiler/REPL from v0.30.8. Brings `positionEncoding` negotiation, `textDocument/formatting`, signature help, completion in an unbalanced buffer, and language-server support for untitled documents.
- **Restart REPL tooltip** -- now states what it has always done: restarts the REPL in the current file's directory.

### Fixed
- **Crash on editor commands with a non-editor tab** -- driving the control socket (`editor.get_text`, `editor.type`, and friends) while a directory browser or the preferences pane was the active tab dereferenced a null editor and killed the app. These commands now return a `no_editor` error.
- **REPL working directory on launch** -- starting Trowel with a file (`trowel foo.tur`, or opening a document from Finder) now roots that window's REPL in the file's directory. It previously rooted at the restored session's directory, or the home directory, ignoring the file being opened.
- **Recent files lost with several windows open** -- each window kept its own recent-files list and the last window to close overwrote the others'. The lists are now merged.

## [0.0.10] -- 2026-07-24

### Changed
- **Bundled Turmeric v0.30.8** -- updated the embedded `tur` compiler/REPL from v0.30.5 to v0.30.8.

## [0.0.9] -- 2026-07-22

### Changed
- **Bundled Turmeric v0.30.5** -- updated the embedded `tur` compiler/REPL from v0.30.4 to v0.30.5.

## [0.0.8] -- 2026-07-22

### Fixed
- **Crash launching from Finder** -- the macOS app now bundles the Qt frameworks and plugins inside `Trowel.app` (via `macdeployqt`) and code-signs them with the app's Developer ID. Previously the notarized binary still referenced the build machine's Homebrew Qt, so a Finder launch aborted before startup with a "Library not loaded / different Team IDs" dyld error. Launching from the CLI happened to mask the bug.

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
