# Multiple windows — plan

Trowel is currently a single-`MainWindow` app: one window owns the tab
set (`buffers_`), the REPL, the splitter, and is the fixed target of the
control socket. This plan adds real multi-window support, modeled
loosely on VSCode.

Four behaviors drive the design:

- **(a)** If no window is open and a file is opened → open it in a new
  window.
- **(b)** If a window is open and a file is opened → open it in a new
  tab in that window.
- **(c)** The user can open a new blank window.
- **(d)** A file dropped onto a window replaces the current tab (or
  creates one if the window somehow has none).

## What already works

Worth stating plainly, because it shrinks the job considerably:

- **Opening already fans into tabs.** `MainWindow::openPath()` reuses an
  empty, unmodified Untitled buffer and otherwise appends a tab. So (b)
  already holds *within* a window — the work is routing, not tabbing.
- **(d) is most of the way there.** `replaceBufferWithFile(index, path)`
  swaps a tab's content in place and is already used by `DirectoryView`
  activation. Drop handling can call it directly. **But it does not
  prompt to save** — dropping onto a modified tab would silently discard
  edits. That guard is net-new and is the one real bug risk in (d).
- **The REPL is already per-window by construction.** Each `MainWindow`
  ctor builds its own `ReplSession`. N windows means N independent
  `tur repl` processes — which is the behavior we want (see
  "REPL working directory" below), just at N shells of cost.

## What blocks it today

- **One window pointer, everywhere.** `TrowelApplication` (macOS
  `QEvent::FileOpen`), the single-instance forwarder (Linux), and
  `ControlServer` each hold a single `MainWindow*`. `Dispatch()` takes
  one window.
- **Restore happens in the constructor.** `MainWindow`'s ctor calls
  `restoreState()`, which reopens persisted buffers. A *new* window must
  come up blank, so construction and restore have to be separated.
- **Persistence is singular.** The `QSettings` keys `geometry`,
  `splitterState`, `openBuffers`, `activeBuffer`, and `replVisible` all
  describe exactly one window.
- **`quitOnLastWindowClosed` is default-true**, so closing the window
  quits the app. Requirement (a) can never trigger until that changes on
  macOS.

## Decisions

Settled up front; the rest of the plan assumes these.

- **Lifecycle.** `setQuitOnLastWindowClosed(false)` on macOS only, so
  the app survives with zero windows and a reopen makes a fresh one
  (this is what enables (a)). Linux/Windows keep quitting on last close
  — cleaner interaction with the single-instance socket, which assumes
  "process alive ⇒ window available".
- **Session restore.** Reopen *every* window from the last session, with
  its tabs and geometry. Requires the per-window settings array below.
- **Duplicate files.** Re-opening a file already open in the target
  window focuses that tab instead of adding a second one. Cross-window
  duplicates stay allowed — no global search, no window-raising
  surprises.
- **Last tab close.** Unchanged from today: `Ctrl/Cmd+W` on the last tab
  leaves a fresh Untitled buffer and the window stays. Windows close via
  `Ctrl/Cmd+Shift+W` or the window controls.

## Architecture: a window registry

Introduce a small registry — either on `TrowelApplication` or a
`WindowManager` it owns — holding `QList<MainWindow*>` plus a
most-recently-activated pointer, and exposing:

```cpp
MainWindow* activeWindow();       // last-activated, or nullptr
MainWindow* newWindow();          // blank window, one Untitled buffer
MainWindow* activeOrNewWindow();  // the (a)/(b) routing primitive
```

Track "last activated" ourselves rather than calling
`QApplication::activeWindow()`, which returns null whenever the app is
backgrounded — precisely the case when a forwarded CLI open or a
LaunchServices `FileOpen` arrives. Windows register on construction and
deregister on close.

With that in place the four behaviors are one-liners at the routing
points:

| Req | Behavior              | Implementation                                     |
|-----|-----------------------|----------------------------------------------------|
| (a) | no window → new window| `activeOrNewWindow()->openPath(f)`                 |
| (b) | window → new tab      | same call; `openPath` already appends a tab        |
| (c) | blank new window      | `newWindow()` from File ▸ New Window               |
| (d) | drop replaces tab     | `maybeSaveBuffer` → `replaceBufferWithFile`        |

Note that (a) and (b) collapse into the *same call*. The branch lives
entirely inside `activeOrNewWindow()`, which is the point of the
abstraction.

## Open routing

Three entry points must funnel through `activeOrNewWindow()`:

1. **macOS** — `TrowelApplication::openFile()`, driven by
   `QEvent::FileOpen` from LaunchServices, Finder, or the `trowel` CLI
   shim's `open -b`.
2. **Linux** — the single-instance forwarder, which currently sends
   `editor.open` per file plus a `window.activate`.
3. **Control socket** — `editor.open`.

Because the forwarder and the control socket share `editor.open`, fixing
the handler's target resolution covers both. `Dispatch()` keeps taking a
concrete `MainWindow*`; only the resolution of *which* window changes.

The **focus-existing-tab** rule lands inside `openPath()`: compare
absolute paths against open editor tabs in that window and activate a
match instead of appending.

## Drag and drop

No drop handling exists today (`grep` finds no `setAcceptDrops` or
`dropEvent` anywhere in `src/`), so this is entirely net-new:

- `setAcceptDrops(true)` on `MainWindow`.
- `dragEnterEvent` accepts `text/uri-list` payloads containing local
  file URLs.
- `dropEvent`:
  1. `maybeSaveBuffer(activeIndex_)` — the missing guard. Bail if the
     user cancels.
  2. Replace the active tab with the first dropped path via
     `replaceBufferWithFile()`; a dropped directory swaps in a
     `DirectoryView` the way `openDirectory()` does.
  3. Any remaining dropped files open as new tabs.
  4. `raise()` + `activateWindow()`.

**Divergence from VSCode, on purpose:** VSCode opens a drop as a *new*
tab. The spec here is replace-current, which is why step 1 matters —
without the save prompt, replace is destructive.

## Persistence

Replace the singular keys with a `windows` array, each entry carrying
what one window needs:

```
windows/<n>/geometry
windows/<n>/splitterState
windows/<n>/openBuffers      # same dir:// encoding as today
windows/<n>/activeBuffer
windows/<n>/replVisible
focusedWindow                # index into the array
```

Migration: when `windows` is absent, synthesize a single entry from the
existing top-level keys (which themselves already migrate the older
`lastFile`), then drop them.

Persist the whole set at app quit rather than per-window, so two windows
can't clobber each other's keys. A window closing mid-session removes
its entry and rewrites, so a crash still leaves the most recent good
state.

## REPL working directory

No change, and that is a deliberate choice worth recording.

`replWorkingDir()` returns the active file's directory, or `$HOME` when
there is no file, and is consulted only at REPL start and restart. Carry
that unchanged into multi-window and the two things users actually want
fall out of how the window was born:

- **Window opened onto a file** → REPL roots in that file's directory —
  "follows the project".
- **Blank New Window** → no file → `$HOME` → a scratch REPL.

So the determining question is "did this window open onto a file?",
which is already in the user's head. No mode, no toggle, no new concept.

The cwd deliberately does **not** chase tab switches: a REPL that
silently `cd`s when you click another tab would break relative paths and
in-flight work. **Restart REPL** is the explicit, already-existing
escape hatch, and it re-roots to the current active file's directory.

Further control over the REPL's cwd is its own plan — see
[`repl-working-directory.md`](repl-working-directory.md). It has no
dependency on this work and can land before or after.

## Milestones

1. **Registry + refactor.** Window registry, construction/restore split
   so a bare `MainWindow` comes up blank, `ControlServer` repointed at
   the registry. No user-visible change — this is the load-bearing
   refactor everything else sits on, and it should be provable by the
   existing smoke suite passing untouched.
2. **New Window.** File ▸ New Window (`Ctrl/Cmd+Shift+N`). Optionally a
   Window menu listing open windows (macOS convention, cheap).
3. **Open routing.** Route all three entry points through
   `activeOrNewWindow()`, delivering (a) and (b). Add the
   focus-existing-tab rule to `openPath()`.
4. **Drag and drop.** Accept drops, add the save guard, deliver (d).
5. **Persistence + lifecycle.** Per-window settings array with
   migration; `setQuitOnLastWindowClosed(false)` on macOS.
6. **Tests.** `window.new` and `window.list` control commands; smoke
   coverage for multi-window routing; update the single-instance
   forwarding test for the route-or-create path.
7. **Documentation.** A user-facing "Windows, tabs & the REPL" guide
   under `docs/guides/`, linked from the README, plus a CHANGELOG entry.
   It should state behavior as rules a user can predict, not
   implementation: the four open behaviors (flagging the drop
   divergence), the tab/window key map, focus-existing-tab, session
   restore, and the REPL cwd rule above — including that Restart REPL
   re-roots. That last one is "obvious once explained", which is exactly
   what earns a doc line.

## Corner cases

- **Zero windows on macOS.** `activeOrNewWindow()` must mint a window,
  or control commands like `editor.open` hit a null `editorView()`.
- **Modified tab + drop** → must prompt; see above.
- **Multiple files dropped** → first replaces, rest become tabs.
- **Quit with unsaved work across several windows** → each window's
  `closeEvent` already runs `maybeSaveAll()`; cancelling in any window
  must abort the whole quit, not leave a half-closed set.

## Non-goals

- Moving or dragging tabs between windows.
- Split editors within a window.
- A shared REPL across windows.
- Cross-window "focus the tab that already has this file open".
- Per-window themes or fonts (font stays a global `QSettings` value).
- Windows-platform lifecycle tuning beyond "quit on last close".

## Future

- Drag a tab out to spawn a window (natural once drop handling exists).
- Window menu with numbered shortcuts.
- Per-window session restore including cursor position and scroll.
- Remembering which display/space a window was on.
