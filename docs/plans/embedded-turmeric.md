# Embedded Turmeric — plan

Ship Trowel on macOS as a self-contained `.app` bundle that carries its
own `tur` binary, installable by dragging into `/Applications` (the
DrRacket model). A user who has never heard of Turmeric downloads a DMG,
drags Trowel across, launches it, and gets a working REPL — no `brew
install`, no `PATH` fiddling.

Today `ReplSession::start` resolves `tur` purely via
`QStandardPaths::findExecutable` against the user's `PATH`
(`src/repl/repl_session.cpp:39`) and prints an "install turmeric" banner
when that fails. Everything else about the drag-install flow is a
packaging problem, not a code problem.

## Resolution order

Three sources, in order of priority, mirroring the shape of the
experiment-flags plan:

1. **User-set path in the Trowel config file.**
   `~/.config/trowel/config.tur` — a turmeric-manifest-syntax file
   with a `:tur-binary "…"` key. If that key is present and points to
   an executable file, use it. This is the escape hatch — a Turmeric
   contributor pointing Trowel at a debug build, a user on an unusual
   layout, CI. `.config/trowel/` is the same directory the existing
   "Trowel Settings" menu entry already opens.
2. **Bundled binary.** `Trowel.app/Contents/MacOS/tur` (or
   `Contents/Resources/tur`), resolved from
   `QCoreApplication::applicationDirPath()`. This is the default the
   drag-installed user gets without knowing it exists.
3. **`PATH` lookup.** Current behavior via
   `QStandardPaths::findExecutable("tur")`. Preserved so a developer
   running `./build/trowel.app/Contents/MacOS/trowel` from a source
   checkout — where no bundled `tur` has been copied in — still works
   against whatever `tur` is on their shell `PATH`.

The banner text at `src/repl/repl_session.cpp:42` updates to name which
sources were tried, so a broken preference doesn't look like a missing
install.

### Why a `.tur` config file, not a preferences dialog or a hardcoded bundle-first path

The file is absent by default and the bundled binary is the zero-config
answer. Using a `.tur` file rather than a `QSettings`-backed dialog:

- **Turmeric development stays ergonomic.** Add
  `:tur-binary "~/src/turmeric/target/release/tur"` to the config
  once; every launch picks it up. No rebuilding Trowel, no symlink
  dance inside the bundle.
- **Version pinning is possible.** A user who wants to stay on
  turmeric 0.4 while Trowel ships with 0.5 can, without shell hacks.
- **Consistent with Trowel's config story.** The experiment-flags plan
  already commits to `.tur`-syntax files under `~/.config/` as the
  surface for turmeric-related knobs. Trowel-side knobs mirror that,
  under `~/.config/trowel/config.tur`. Same workflow ("open the
  settings directory, edit a file, restart the REPL"), same syntax,
  same mental model.
- **Room to grow.** Using a keyed file rather than a bare path file
  means the second Trowel-side knob (theme override, default working
  dir, whatever) fits into the same file without a schema migration.
- **Scriptable.** Provisioning scripts, dotfiles repos, and CI can
  drop the file into place.

## Config surface

No new menu entry — the existing "Trowel Settings" action opens
`~/.config/trowel/`, where the user edits `config.tur`. Trowel seeds
the file on first launch with a commented-out template (the whole
`:tur-binary` entry is a `;;` comment), so the user sees a starting
point instead of an empty directory. Seeding is a no-op on subsequent
launches — the user's edits are never overwritten.

File format: turmeric manifest syntax. Trowel reads one key,
`:tur-binary`, whose value is a string containing an absolute path
(env-var / `~` expansion is out of scope for v1; users can write the
full path).

```turmeric
;; ~/.config/trowel/config.tur
:tur-binary "/Users/alice/src/turmeric/target/release/tur"
```

Missing file, missing key, or a value that does not resolve to an
executable → fall through to source 2, then 3. A key that is set but
unresolvable is surfaced in the failure banner (see below) so the user
notices a typo rather than silently ending up on `PATH`.

Trowel does not embed a full turmeric reader — it matches the
`:tur-binary "…"` key with a regex, after stripping `;;` line
comments. Good enough for one knob; graduates to `tur config` (or
similar) once turmeric grows a real reader Trowel can call.

`ReplSession` gains a small helper — `resolveTurBinary()` — that runs
the three-source lookup and returns both the path and which source
won, so the failure banner can name every source it tried.

## Packaging: getting `tur` into the bundle

Two build-time paths, pick one:

1. **Prebuilt drop-in.** A packaging step (`just package` or a CMake
   `install(PROGRAMS ...)`) copies a `tur` binary from a known location
   into `Trowel.app/Contents/MacOS/`. The turmeric repo already
   publishes release binaries; download-and-verify is the simplest
   pipeline. Fast local iteration, and the CI job that cuts a Trowel
   release pins a specific turmeric version by URL/sha.
2. **`FetchContent` / `ExternalProject_Add`.** CMake builds turmeric
   from source as part of Trowel's build. Guarantees the two are
   in-sync, but drags the turmeric toolchain (and its build time) into
   every Trowel developer's setup. Reasonable once turmeric is stable;
   overkill now.

Start with (1). Document the pinned turmeric version in
`docs/plans/PLAN.md` and bump it deliberately.

Source builds (running `trowel` out of `build/`) will not have a
bundled binary — that is fine, source 3 (`PATH`) handles it.

## Dylib closure

`tur` may link non-system dylibs. If it does, drag-installing on a
machine without those dylibs will fail at exec time with a confusing
dyld error.

Packaging step, after copy:

- `otool -L Trowel.app/Contents/MacOS/tur` to list non-`/usr/lib` and
  non-`/System` dependencies.
- Copy each into `Trowel.app/Contents/Frameworks/`.
- `install_name_tool -change <old> @executable_path/../Frameworks/<lib>`
  on `tur` and on any dylib that references another.
- Recurse until closure is empty.

If turmeric ships as a static binary (Rust default, some Zig builds),
this step is a no-op and can stay a defensive check.

`macdeployqt` handles Qt's own libs already — do not point it at `tur`;
run the fixup as its own step.

## Codesigning and notarization

Drag-to-Applications only works on a modern macOS if the bundle is
signed and notarized. Otherwise Gatekeeper marks it "damaged, move to
trash" the first time it's launched — the single most common way this
delivery mode fails.

Release pipeline:

1. `codesign --deep --force --options runtime --sign "$DEVELOPER_ID" \
      Trowel.app` — signs the app, `tur`, and every dylib under
   `Contents/Frameworks/`.
2. `ditto -c -k --keepParent Trowel.app Trowel.zip` and submit to
   `xcrun notarytool submit Trowel.zip --wait`.
3. `xcrun stapler staple Trowel.app` so the notarization ticket is
   embedded and the first launch works offline.
4. `hdiutil create` a DMG containing `Trowel.app` and an
   `/Applications` symlink — the standard "drag here" background layout.

Notarization requires a paid Apple Developer ID. Until we have one,
publish the unsigned `.app` with a documented one-liner
(`xattr -dr com.apple.quarantine Trowel.app`) so early testers can
launch it, and treat "signed release" as a v1.0 gate rather than a v0
one.

## Architectures

Ship a universal binary. Two knobs:

- Trowel itself: `set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")` in
  `CMakeLists.txt`. Qt's macOS distributions are universal, so this
  Just Works.
- `tur`: either a universal build from the turmeric side, or `lipo` an
  Intel and an ARM build together before dropping it in. The packaging
  step verifies with `lipo -info` and fails loudly on a single-arch
  binary.

## Non-goals for v1

- **No Linux / Windows equivalent.** They have their own conventions
  (AppImage, MSI/portable zip) and separate signing stories. Same
  three-source resolution applies, but packaging is out of scope here.
- **No auto-update.** If turmeric ships a new version, the user
  downloads a new Trowel DMG. Sparkle / built-in updater is a separate
  plan.
- **No in-app version display for turmeric.** The preferences dialog
  shows the resolved path; the user can `tur --version` themselves.
  Adding a "Turmeric: 0.5.1 (bundled)" line is easy later but is not
  load-bearing for the install experience.

## Rollout order

1. Add the three-source resolver + `~/.config/trowel/config.tur`
   `:tur-binary` lookup + banner update. Ships value immediately even
   without a bundle (users who have sibling turmeric checkouts stop
   needing PATH shims).
2. Add the packaging step that copies a prebuilt `tur` into the
   bundle; wire it into `just package`.
3. Add the dylib fixup step, guarded by an `otool` check that no-ops
   for static builds.
4. Signing + notarization + DMG once a Developer ID is available.
