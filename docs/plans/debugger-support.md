# Debugger support — plan

Wire Trowel to `tur dap`, the Debug Adapter Protocol server already shipping
inside the `tur` binary Trowel bundles: breakpoints, stepping, call stack,
locals, and in-frame evaluation. The debugger surfaces as a sibling tab to the
REPL inside the right-hand pane.

Companion to [`lsp-support.md`](lsp-support.md), which established the
JSON-RPC-over-stdio transport this reuses.

> **Status:** not started. Phase 0 (spike) should be done before committing to
> the rest.

## Context

**Turmeric has a working debugger today.** This is not speculative — it landed
upstream in v0.25.2 and was upgraded in v0.25.6, and Trowel already bundles a
binary that carries it (`TROWEL_TURMERIC_VERSION "v0.32.6"`,
`CMakeLists.txt:113`). Verified empirically against the staged binary: `tur dap`
answers `initialize` with

```json
{"supportsConfigurationDoneRequest": true, "supportsConditionalBreakpoints": true,
 "supportsEvaluateForHovers": true, "supportsTerminateRequest": true}
```

followed by an `initialized` event.

Ground truth for everything below:

| What | Where (in `../turmeric`) |
| --- | --- |
| The DAP server | `src/turi/dap.c` (691 lines) |
| The debugger control API it drives | `src/turi/eval.h`, the `turi_debug_*` block |
| Design write-up | `docs/artifacts/debugger-dap-phase3.md` |
| A working reference client | `tests/dap-driver.py`, driven by `tests/run-dap.sh` |
| Fixture | `tests/fixtures/dap/input.tur` |

There are two front ends upstream: `tur debug <file.tur>` (an interactive
line-oriented stepper on stdin/stdout, meant for humans and scripts) and
`tur dap` (DAP, meant for editors). Only the latter is a sane integration
target.

### What the adapter supports

Requests: `initialize`, `launch`, `setBreakpoints` (with `condition`),
`setExceptionBreakpoints` / `setFunctionBreakpoints` (accepted, no-op),
`configurationDone`, `threads`, `stackTrace`, `scopes`, `variables`,
`evaluate`, `continue` / `next` / `stepIn` / `stepOut`, `pause` (acknowledged
only), `disconnect` / `terminate`.

Events: `initialized`, `stopped` (`entry` / `breakpoint` / `step` / `pause`),
`output` (debuggee stdout), `exited`, `terminated`.

## Constraints discovered

These are read out of `dap.c` and `eval.h`, not assumed. They shape the entire
design, so they come before the design.

1. **Interpreter only.** `tur dap` runs the tree-walking interpreter (the same
   path as `tur debug`, via `cmd_eval(..., debug=true)` in `src/main.c`). It is
   not the compiled or JIT path. "Debug this file" therefore has different
   performance and, potentially, different behaviour from `tur run`.
2. **`launch` only — there is no `attach`.** `dap_server_run` handles no attach
   request. **The debugger cannot be pointed at the running `tur repl`
   process.** A debug session is a separate child process with its own fresh
   environment. This is the single most consequential constraint in this plan.
3. **One program per session.** `dap_server_run` clears `launched` after
   `configurationDone` (`/* one program per session */`). "Restart" means
   respawning `tur dap`.
4. **`launch` carries no `cwd`.** Only `program`, `args`, and `stopOnEntry` are
   read. The working directory must be set on the child process by Trowel.
5. **Breakpoints match by *basename*.** `dap_set_breakpoints` reduces
   `source.path` through `dap_basename()` before calling
   `turi_debug_add_breakpoint` / `turi_debug_clear_breakpoints_for_file`. Two
   files named `main.tur` in different directories share one breakpoint set
   inside the interpreter.
6. **The adapter only reads stdin while paused or before `configurationDone`.**
   Once the program runs, the process is inside the eval loop; `dap_on_pause` is
   the only reader. Consequences: asynchronous `pause` is impossible (the
   handler only replies "already paused" when it is *already* paused);
   `setBreakpoints` sent mid-run is not seen until the next stop; a runaway
   program can only be stopped by killing the process.
7. **`variablesReference` is always 0.** `dap_var_cb` hardcodes it. There are no
   expandable structured values; structs, options, and results render inline as
   a string. `scopes` returns exactly one `Locals` scope whose reference is
   `frameId + 1`.
8. **No `setVariable`, no `source`, no real exception or function breakpoints,
   no data breakpoints, no `restart`, no `stepBack`.**
9. **No debuggee stdin.** The debuggee's stdout is redirected to a pipe
   (best-effort 1 MiB) and drained at every stop into `output` events. **stderr
   is not captured** — it goes to the adapter's own stderr. A program writing
   more than the pipe buffer between stops blocks until the next drain.
10. **`disconnect` / `terminate` while paused calls `_exit(0)`.** No unwind, and
    the program's real exit code is lost.
11. **Frame `id` is the frame index**, 0 = innermost. `stackTrace` ignores
    `startFrame` / `levels` and always returns the whole stack. `evaluate`
    clamps a negative `frameId` to 0.
12. **Conditions accept two syntaxes.** `dap_rewrite_condition` passes anything
    starting with `(` straight to `turi_debug_eval_expr`, and otherwise rewrites
    a single `name op literal` comparison (`== != < > <= >=`) into Lisp —
    `a != 3` becomes `(not (= a 3))`. A condition that fails to evaluate falls
    back to *stopping*, so a bad condition never silently swallows a breakpoint.
13. **Windows is explicitly unsupported** (see the `#else` in
    `dap_run_program`). Moot for Trowel today, but do not build on it.
14. **Exactly one thread**, hardcoded as `{"id": 1, "name": "main"}`.

Nothing has to be invented on the Turmeric side to ship phases 1–5. What we
*would* want from Turmeric later is enumerated at the end.

---

## Protocol decision — DAP, not a Turmeric-native protocol

**Recommendation: speak DAP directly.**

The case for DAP:

- **It exists and is tested.** `tests/run-dap.sh` drives a scripted session
  through `tests/dap-driver.py` as a ctest target. A Turmeric-native protocol is
  net-new work upstream that Trowel would then have to wait on, and it would be
  tested only by Trowel.
- **The transport is already written.** DAP uses the *identical* framing to LSP:
  `Content-Length: N\r\n\r\n<json>` over the child's stdin/stdout.
  `src/lsp/lsp_transport.{h,cpp}` implements exactly this, including the
  chunk-boundary-safe accumulation and the desync bail-out. Critically,
  `LspTransport` contains **no LSP semantics at all** — its API is
  `send(QJsonObject)` / `messageReceived(QJsonObject)`. It is a JSON-RPC framing
  class that happens to be named after its first caller.
- **The correlation layer is 90% shared.** `LspClient`'s id→`Reply` table,
  per-request `QTimer`, `takePending`, and `failAllPending` are exactly what a
  DAP client needs. The envelope differs (`type`/`command`/`arguments` and
  `request_seq`/`body`/`success`, versus `method`/`params`/`id`/`result`), and
  DAP interleaves events with responses more aggressively, but the shape is the
  same.
- **A native protocol would fix nothing that matters.** The tempting wins —
  structured variables, attach-to-REPL, async interrupt — are all *interpreter*
  and *adapter-loop* limitations, not wire-format limitations. DAP already has
  `variablesReference` trees, `attach`, and asynchronous `pause`. The bottleneck
  is `turi_debug_*` and `dap.c`'s single-threaded read loop. A second protocol
  would reproduce every one of those limits at higher cost.

The honest case against, and how to handle it:

- **DAP is a large spec and we need a sliver of it.** The failure mode is
  building a general adapter client. Don't. Build a `DapClient` that speaks
  exactly the requests `dap.c` handles and treats everything else as
  unsupported. Roughly 200 lines.
- **Reusing `LspTransport` makes the name lie.** Accept it for phase 1 with a
  comment; a follow-up can move it to `src/rpc/jsonrpc_transport.{h,cpp}` with
  `LspTransport` as a thin alias. Doing the rename first means churning
  `lsp_manager.cpp` for no functional gain, and blocks phase 1 on a refactor.

**Decision:** `DapClient` composes an `LspTransport` directly. No transport code
is written or moved in phase 1.

---

## Lifecycle and process management

### Where a debug session sits relative to the REPL

They are **independent sibling processes**. Because there is no `attach`
(constraint 2), a debug session cannot share the REPL's environment. State this
plainly in the UI, because it will otherwise confuse people:

- Anything `(load ...)`ed into the REPL is **not** visible in a debug session.
- The REPL keeps running during a debug session; they contend for nothing but
  CPU.
- Debuggee output goes to the **Debugger tab's output pane**, not the REPL
  terminal.

That last point is a design decision, not just a convenience. `TerminalView` is
driven by a PTY byte stream through an ANSI parser; DAP `output` events are
plain text chunks with a category. Splicing them into `TerminalView` would mean
faking a terminal stream for text that was never one. A read-only log widget in
the debugger pane is the right shape, and it doubles as the place `evaluate`
results land.

### Session object

`src/debug/debug_session.{h,cpp}` owns one `DapClient` and one program run:

```
Idle → Starting     (process spawned, `initialize` in flight)
     → Configuring  (`initialized` received; push setBreakpoints)
     → Running      (`configurationDone` sent)
     ⇄ Paused       (`stopped` / resume)
     → Terminated   (`exited` + `terminated`, or process death)
     → Idle
```

**Per window, not per application** — unlike `LspManager`. Rationale: one
language server indexing shared documents is a genuinely global resource; a
debug run is a per-window user action bound to that window's active buffer and
its REPL working directory. Two windows debugging at once means two
interpreters, which is fine — they are separate processes with no shared state.

### Starting

- Binary: reuse `ResolveTurBinary()` (`src/repl/repl_session.h`) — already the
  single source of truth for the QSettings override → bundled → PATH order.
- Environment: pin `TUR_STDLIB_DIR` through `extraEnv`, exactly as the
  `LspTransport::start` callers do.
- Working directory: `ReplSession::workingDir()` (or
  `MainWindow::replWorkingDir()`), set on the `QProcess`. Since `launch` carries
  no `cwd` (constraint 4), this is the only lever, and matching the REPL means
  relative paths in the debuggee behave the way the user already expects.
- Gating: only offer "Debug Buffer" when `EvalModeForPath(path) ==
  EvalMode::Buffer` (from `src/repl/run_buffer.h`). A `build.tur` manifest is a
  project description, not a script; non-Turmeric files aren't debuggable at
  all.

### Dirty and untitled buffers

`RunBuffer` writes dirty buffers to a scratch file under the cache dir. **Do not
copy that here.** Breakpoints bind by basename (constraint 5) and stack frames
carry the scratch path, so a scratch file with a mangled name would silently
fail to bind breakpoints *and* leave the editor unable to tell which buffer a
frame belongs to.

**Recommendation: phase 1 requires a saved file.** Prompt "Save before
debugging?" and debug the saved path. Simple, correct, and matches what anyone
expects from a debugger. If scratch-file debugging is revisited later, the
scratch file must preserve the original basename.

### Stopping and teardown

- **While paused:** send `terminate`; the adapter `_exit(0)`s. Report "stopped
  by user", *not* "exited 0" — the real exit code is gone (constraint 10).
- **While running:** the adapter is not reading stdin at all (constraint 6), so
  the only option is `QProcess::terminate()` then `kill()` after a grace period
  — the same shape as `LspTransport::terminate()`.
- On `finished(exitCode)`: return to `Idle`, clear every execution decoration in
  every open editor.
- Tear the session down in `MainWindow::closeEvent` and in the `WindowManager`
  teardown path, alongside the existing REPL stop. `tests/smoke/test_shutdown.py`
  is the place to prove no child is orphaned.

---

## The REPL-pane tab bar

Target layout — a tab strip at the **bottom** of the right-hand pane:

```
+------------------------+---------------------------+
|                        |                           |
| editor stack           |  TerminalView             |
|                        |  or DebuggerView          |
|                        |                           |
|                        +---------------------------+
|                        | [ REPL ] [ Debugger ]     |
+------------------------+---------------------------+
```

Top-mounting the strip instead is a one-line change (layout order); bottom is
what the design intent calls for, and it keeps the pane's top edge flush with
the editor's.

### New widget

`src/app/repl_pane.{h,cpp}`:

```cpp
class ReplPane : public QWidget {
    Q_OBJECT
public:
    TerminalView* terminal() const;
    DebuggerView* debugger() const;
    void showTerminal();
    void showDebugger();
    int activeTab() const;         // for sessionState()
    void setActiveTab(int index);
private:
    QStackedWidget* stack_;
    TabBar* tabs_;
};
```

### Wiring into `MainWindow`

`MainWindow::setupUi` currently builds the pane inline (`main_window.cpp:91–98`):
`terminal_ = new TerminalView(splitter_)` then `splitter_->addWidget(terminal_)`.

Replace with `replPane_ = new ReplPane(splitter_); splitter_->addWidget(replPane_);`
and keep `terminal_ = replPane_->terminal()`.

**Keeping `terminal_` and `MainWindow::terminalView()` meaning exactly what they
mean today is what makes this refactor cheap.** `ApplyThemeToTerminal`,
`clearRepl`, `focusRepl`, `toggleReplEditorFocus`, `ReplSession`'s constructor
argument, and every `repl.*` handler in `src/control/control_handlers.cpp`
(`repl.get_screen`, `repl.get_cursor`, `wait.repl_output`, …) continue to work
untouched.

`toggleReplVisible` (`main_window.cpp:345`) becomes
`replPane_->setVisible(visible)`.

### Reusing `src/app/tab_bar.cpp`

`TabBar` is almost right for this, but has three mismatches:

1. **Close buttons are unconditional.** `relayout()` always reserves
   `kCloseSlot` (24px, `tab_bar.cpp:113`), `paintEvent` always draws the ×, and
   `mousePressEvent` emits `closeRequested`. REPL and Debugger are not closable.
2. **It scrolls and elides for a large, variable tab set.** With exactly two
   fixed tabs, `ensureActiveVisible` / `maxScrollOffset` / `wheelEvent` are dead
   weight — harmless, but noise.
3. **The divider is drawn assuming the bar sits above its content.** A
   bottom-mounted bar wants its 1px rule on top.

Three options:

- **(a) Add `setClosable(bool)` and a divider-edge option to `TabBar`.**
  Smallest diff: when closable is false, `relayout()` skips the close slot,
  `paintEvent` skips the glyph, `closeHit()` returns false. ~15 lines in
  `tab_bar.cpp`, no new files, and the document tab bar is untouched because
  both flags default to today's behaviour. **Recommended.**
- (b) A separate `PaneTabBar` widget. Conceptually cleaner; duplicates ~120
  lines of layout and paint code that will then drift apart.
- (c) Extract a shared base class. Over-engineering for two call sites.

Both additions are additive and defaulted, so `MainWindow::refreshTabBar` and
the document tab bar need no changes at all.

### Behaviour

- Persist the active pane tab in `MainWindow::sessionState()`, next to
  `replVisible` and `splitterState`. Default REPL.
- Auto-switch **to** the Debugger tab when a session starts. Do **not** auto-switch
  away when it ends — yanking the pane out from under someone reading output is
  worse than a stale tab.
- A keybinding to cycle pane tabs, distinct from the document `Ctrl+Tab`.

---

## Debugger UI surfaces

`src/debug/debugger_view.{h,cpp}` — a plain `QWidget`, **not** a `TabContent`
(it lives in the REPL pane, not the editor stack, and has no path or modified
state). Top to bottom:

**1. Toolbar row.** Continue (F5), Step Over (F10), Step In (F11), Step Out
(⇧F11), Restart, Stop (⇧F5). Icons from `src/app/icon_font.h` where suitable
glyphs exist. Enablement is driven entirely by the session state machine:
everything except Stop is disabled unless `Paused`.

**No Pause button.** The adapter cannot honour it (constraint 6) — it would be a
button that does nothing while running and is redundant while paused. If a
button belongs in that slot, it is Stop, and Stop kills.

**2. A horizontal splitter** with three panels:

- **Call Stack** — a flat list (`QListWidget` is sufficient; DAP hands us a flat
  array): `fn_name    file.tur:line`. Frame `id` is the index (constraint 11),
  so selection maps directly. Selecting a frame re-issues `scopes` + `variables`
  for it and moves the editor's *selected-frame* highlight, which is visually
  distinct from the *current-execution-line* highlight on frame 0.
- **Variables** — a two-column `QTreeWidget` (Name / Value), flat for now.
  `variablesReference` is always 0 upstream (constraint 7), so **build no
  expansion machinery**. Use a tree so the widget is ready when Turmeric grows
  structured variables; never request children.
- **Breakpoints** — a checkable list: `file:line`, condition, enabled. This is a
  *view over the model* described below, not the model itself.

**3. Output / console** — a read-only `QPlainTextEdit` fed by `output` events,
with a one-line input beneath that sends `evaluate` against the selected frame's
id and appends `body.result`, or the error `message` on failure. Not a
`TerminalView`: there is no ANSI stream and no debuggee stdin to write back to.

### Breakpoint model

`src/debug/breakpoint_model.{h,cpp}`, living **outside** the view so breakpoints
survive across sessions:

- Keyed by absolute path + 1-based line, carrying `enabled` and `condition`.
- Emits `changed(path)`. `DebugSession` maps that to a `setBreakpoints` for that
  one source (a full replacement per source — which is exactly what `dap.c`
  expects); `EditorView` maps it to gutter markers.
- Per window, matching how the session is scoped. Persist in `sessionState()`.

Three things to get right, all of which are the classic breakpoint bugs:

- **Lines move when you edit.** Scintilla will track this for you:
  `markerAdd` returns a handle and `markerLineFromHandle()` follows it across
  insertions and deletions. Store the handle beside the line and reconcile on
  edit/save. Storing a bare line number and hoping is the bug.
- **Send timing.** Mid-run `setBreakpoints` is not processed until the next stop
  (constraint 6). Send during `Configuring` (before `configurationDone`), again
  on every `stopped`, and immediately when already paused. Render
  not-yet-verified breakpoints with a hollow marker so the delay is visible
  rather than mysterious.
- **The basename collision.** Two open sources with the same
  `QFileInfo::fileName()` will share one breakpoint set inside the interpreter
  (constraint 5). Detect this client-side and surface a warning row in the
  breakpoints list. Silently wrong is much worse than loudly limited.

---

## Editor-side integration

`src/editor/editor_view.cpp` already has the exact precedent to follow:
`namespace diag` (`editor_view.h:22`) declares indicator and marker slots, and
`applyDefaultStyling()` (`editor_view.cpp:122–139`) configures margin 1 as a
symbol margin of width 12 with a mask of the two diagnostic markers, then
`markerDefine`s them. Diagnostics flow in through `setDiagnostics()`, and the
view stays dumb — it paints what it is told.

Do the same for the debugger.

**Marker slots.** Add a `namespace dbg` beside `namespace diag`:
`kBreakpointMarker = 2`, `kBreakpointPendingMarker = 3`,
`kBreakpointDisabledMarker = 4`, `kCurrentLineMarker = 5`,
`kSelectedFrameMarker = 6`. Markers 0–1 are taken by diagnostics and 25–31 by
folding, so 2–6 are free.

**Margins.** The existing `kSymbolMargin` is 12px — enough for a diagnostic dot,
but too narrow to be a comfortable click target and too narrow to hold a
breakpoint dot and a current-line arrow on the same line. Two options: widen it
to ~16px, or add a **dedicated breakpoint margin** and shift the diagnostic
margin over.

**Recommend the dedicated margin.** `setMarginSensitiveN(margin, true)` then
gives you `marginClicked` for free without stealing clicks from the diagnostic
margin, and the two decorations stop competing for 12px. Be honest that this
does change existing gutter layout — a small but visible change to every buffer.

**Click to toggle.** Connect `ScintillaEdit`'s `marginClicked(position,
modifiers, margin)` to toggle a breakpoint at `lineFromPosition(position) + 1`.
Nothing listens to it today; check the exact signal signature against the
vendored Scintilla in `cmake/scintilla_qt.cmake` before writing the connect.

**Current-execution line.** A `SC_MARK_BACKGROUND` marker on `kCurrentLineMarker`
for the full-line tint plus `SC_MARK_SHORTARROW` in the margin, with
`scrollRange`/`gotoLine` to reveal it. Watch for a fight with Scintilla's
caret-line highlight (`Theme::currentLineBg`) — pick a clearly different colour
and set the debug marker's alpha/layer so it wins.

**New `EditorView` API,** mirroring `setDiagnostics` in spirit:

```cpp
void setBreakpointMarkers(const QVector<BreakpointMark>& marks);
void setExecutionLine(int line, bool isTopFrame);
void clearExecutionLine();
signals:
    void breakpointToggleRequested(int line);
```

Keep the wiring outside the view — `MainWindow` or a small `DebugController`
connects model to view, the same way `LspManager` is wired to editors today.

**Theme.** Add `debugBreakpoint`, `debugBreakpointDisabled`, `debugCurrentLine`
to `struct Theme` (`theme_loader.h:20`) and to the bundled theme JSON under
`resources/`, following `diagnosticError` / `diagnosticWarning`.

**Frame → buffer mapping.** `TuriDbgFrame.file_path` is a full path and `dap.c`
emits it as `source.path`, so `MainWindow::indexOfPath` resolves it. If the file
isn't open: phase 1 should simply not highlight and show the path in the stack
list; auto-opening frames is a phase 5 nicety.

---

## Phases

Each is independently shippable.

### Phase 0 — spike (nothing shipped)

Drive the *bundled* `tur` with upstream's `tests/dap-driver.py` against a small
fixture and record the actual transcript on the version Trowel ships. Confirm
specifically: does `evaluate` handle arbitrary expressions on this version (the
in-frame evaluator landed in 0.25.6), what does `stackTrace` return at the entry
stop, and does `output` ordering interleave the way the docs claim. Half a day,
and it de-risks everything below.

### Phase 1 — headless session

New: `src/debug/dap_client.{h,cpp}`, `src/debug/debug_session.{h,cpp}`. Reuse
`LspTransport` unchanged.

A "Debug Buffer" menu/toolbar action that launches with `stopOnEntry: true`,
immediately continues, streams `output` events into a plain text pane, and
reports the exit code in the status bar. No breakpoints, no stack, no stepping.

Ships as *"run the current file under the interpreter with captured output"* —
useful on its own, and it proves transport, lifecycle, and teardown before any
UI is committed to.

Also: add sources to `trowel_lib` in `CMakeLists.txt`; add `debug.status`,
`debug.start`, `debug.stop` control commands in `control_handlers.cpp` so smoke
tests can drive it; add `tests/smoke/test_debugger.py`.

### Phase 2 — REPL-pane tab bar

`TabBar::setClosable(false)` plus the divider-edge option; new
`src/app/repl_pane.{h,cpp}`; the `MainWindow::setupUi` swap; persist the active
pane tab. The Debugger tab holds phase 1's output pane and nothing else.

A pure layout change — independently shippable, and it depends on none of phases
3–5. Land it after phase 1 only so there is something to put in the tab.

### Phase 3 — breakpoints and stepping

`src/debug/breakpoint_model.{h,cpp}`; the `EditorView` margin, markers, and
`marginClicked` handling; `setBreakpoints` during `Configuring` and on each
stop; the stepping toolbar; the `stopped`/resumed state machine; the
current-execution-line highlight.

This is where it becomes a debugger, and it is the largest chunk. If it needs
splitting, land the markers-and-model half first — it is entirely client-side
and touches no adapter traffic.

### Phase 4 — inspection

Call stack list; variables tree; frame selection driving `scopes` + `variables`;
selected-frame highlight in the editor; the `evaluate` console line.

### Phase 5 — polish

Condition editing (right-click a marker; document *both* accepted syntaxes —
`i > 3` and `(> i 3)`); enable/disable; the breakpoints list panel;
basename-collision warning; marker-handle line tracking across edits; breakpoint
persistence in session state; Restart (respawn, since it is one program per
session); auto-open a frame's file when not already open.

---

## Risks and trade-offs

- **Interpreter-only semantics.** A program that works under `tur run` may
  behave differently, or be far slower, under `tur dap`. Label the action
  "Debug (interpreter)" or say so in the tooltip. Otherwise this generates bug
  reports that are not Trowel's.
- **No asynchronous interrupt.** An infinite loop can only be killed
  (constraint 6). Make Stop prominent and instant, and don't pretend otherwise
  with a Pause button.
- **Version skew.** Trowel pins `TROWEL_TURMERIC_VERSION` but `ResolveTurBinary()`
  honours a QSettings override and PATH, so a user can point at an old `tur`
  with no `dap` subcommand. If `initialize` doesn't answer within the timeout or
  the process exits immediately, say "this `tur` has no DAP server" rather than
  failing generically. Note also that at time of writing the staged binary under
  `build/macos-release/_deps/turmeric_prebuilt-src/tur` reported v0.27.0 while
  CMake pins v0.32.6 — the bundling cache can go stale (cf. commit "Fix bundled
  toolchain going stale on an incremental build"). Confirm which binary actually
  ships before relying on a version gate.
- **`LspTransport` reuse.** Correct today (it is pure framing), but the name
  will mislead. Comment it in phase 1; rename in a follow-up. Do not block phase
  1 on the move.
- **Three `tur` children.** REPL + DAP per window, plus the app-wide `tur lsp`.
  Resource cost is fine; cleanup on crash and quit is the thing to get right,
  and `LspTransport::terminate` / `ReplSession::stop` / `test_shutdown.py`
  already establish the pattern.
- **stderr is dropped.** The adapter does not capture the debuggee's stderr
  (constraint 9), and `LspTransport` routes the child's stderr to `qWarning` and
  discards it. A Turmeric panic message would vanish. Surfacing it in the
  debugger output pane needs a `stderrReceived` signal on `LspTransport` — a
  ~5-line change, but it touches a file the LSP path depends on, so it deserves
  its own commit.
- **Lost exit codes.** `_exit(0)` on disconnect (constraint 10) means a
  user-stopped session always reports 0. Report "stopped by user" instead of a
  fake exit code.
- **Uncertainty flagged honestly:** the exact `ScintillaEdit` margin-click signal
  signature in the vendored copy, and whether the shipped `tur` build's
  `evaluate` supports arbitrary expressions or only single names. Both are
  resolved by phase 0.

## What we would want from Turmeric (none of it blocks phases 1–5)

Each maps to a specific upstream file — `src/turi/dap.c` and `src/turi/eval.h`:

- **`attach`**, so the debugger can drive the REPL's own environment. This is the
  one feature that would make the sibling-tab UX feel unified rather than "a
  second, unrelated process".
- **Expandable variables** (`variablesReference > 0`) for structs, ADTs, and
  collections.
- **An out-of-band stdin read** during the run, enabling real async `pause` and
  mid-run `setBreakpoints`.
- **`cwd` in `launch` arguments.**
- **Full-path breakpoint matching** instead of basename.
- **stderr capture** into `output` events with `category: "stderr"`.
- **`setVariable`**, real exception breakpoints, and the `source` request.
- **A clean `terminate`** that unwinds and reports the program's real exit code.

## Non-goals

- Native (`emit-C`) debugging via lldb/gdb. Upstream Phases 4–5 exist
  (`tools/turmeric_lldb.py`,
  `docs/artifacts/debugger-native-sourcemaps-phase4.md`), but that is an lldb
  front end — a different integration entirely.
- Debugging the REPL session itself (impossible without `attach`).
- Fiber-aware or multi-threaded debugging (`dap.c` reports one hardcoded thread).
- Remote debugging.
- A general-purpose DAP client capable of driving non-Turmeric adapters.
- Windows (unsupported upstream).

## Files

**Create**

- `src/debug/dap_client.{h,cpp}` — DAP envelope + request correlation over `LspTransport`
- `src/debug/debug_session.{h,cpp}` — process lifecycle and state machine
- `src/debug/breakpoint_model.{h,cpp}` — persistent breakpoint store
- `src/debug/debugger_view.{h,cpp}` — toolbar, stack, variables, breakpoints, console
- `src/app/repl_pane.{h,cpp}` — `TabBar` + `QStackedWidget` wrapper for the right pane
- `tests/smoke/test_debugger.py`

**Modify**

- `src/app/tab_bar.{h,cpp}` — `setClosable(bool)`, divider edge
- `src/app/main_window.{h,cpp}` — `setupUi` pane swap, Debug actions, session ownership, `sessionState`
- `src/editor/editor_view.{h,cpp}` — `namespace dbg` markers, breakpoint margin, `marginClicked`, breakpoint/execution-line API
- `src/editor/theme_loader.{h,cpp}` + `resources/` theme JSON — three debug colours
- `src/control/control_handlers.cpp` — `debug.*` commands
- `CMakeLists.txt` — new sources in `trowel_lib`
- `src/lsp/lsp_transport.{h,cpp}` — *optional*, `stderrReceived` signal

---

## Summary

Turmeric ships a real, tested DAP debugger today (`tur dap`, `src/turi/dap.c`),
and the version Trowel bundles has it — verified by handshaking with the staged
binary. Nothing needs to be invented upstream for a v1.

The recommendation is to **speak DAP directly**, reusing `LspTransport`
unchanged (it is pure `Content-Length` framing with zero LSP semantics) under a
new narrow `DapClient`. A Turmeric-native protocol would cost more and fix none
of the real limits, which are all in the interpreter and the adapter's
single-threaded read loop.

The three constraints that drive everything: **there is no `attach`**, so a debug
session is a separate process from the REPL and cannot see anything the REPL has
loaded; **the adapter only reads stdin while paused**, so there is no working
Pause and Stop means kill; and **breakpoints bind by basename**, which needs a
client-side collision warning.

For the REPL pane, the cheapest correct refactor is `TabBar::setClosable(false)`
plus a divider-edge flag (~15 additive lines) and a new `ReplPane` wrapper, while
keeping `MainWindow::terminal_` and `terminalView()` meaning exactly what they
mean today — that alone keeps the control socket and every REPL slot untouched.

Five phases, each shippable: headless session with output capture → pane tab bar
→ breakpoints and stepping → inspection → polish.
