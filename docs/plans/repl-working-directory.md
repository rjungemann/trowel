# REPL working directory — plan

Give the user explicit control over, and visibility into, the working
directory of a window's `tur repl` session: an indicator showing where
the REPL is rooted, a GUI command to change it, and a `:cd`-style
meta-command in the REPL itself.

Independent of [`multi-window.md`](multi-window.md) — it can land before
or after. The two compose cleanly: each `MainWindow` owns its own
`ReplSession`, so everything here is inherently per-window.

## Background: how the cwd works today

- `MainWindow::replWorkingDir()` returns the active file's directory, or
  `$HOME` when there is no file.
- It is consulted at exactly two moments: `ReplSession::start()` on
  window construction, and `ReplSession::restart()` from the **Restart
  REPL** action. It does not chase tab switches — deliberately, since a
  REPL that silently `cd`s underneath you breaks relative paths and
  in-flight work.
- `ReplSession` already stores `lastWorkingDir_`, but does not expose
  it.
- `PtySession::start()` takes the working directory and applies it in
  the child before `exec`.

**The load-bearing constraint:** you cannot change a running process's
cwd from outside it. So any Trowel-side "set working directory" is
necessarily a **restart** of the REPL, which clears its state. That is
not a workaround to be papered over — it should be visible in the UI
labeling. It also means the genuinely *live* version of this feature can
only be implemented inside `tur repl`, which shapes the staging below.

Two pieces of existing machinery make this cheap:

- `TerminalView::showBanner(QString)` injects a display line into the
  terminal **without** going through the PTY — so the indicator costs
  almost nothing.
- `tur repl` **already emits OSC escape sequences that Trowel
  consumes**: `ReplSession` scans for OSC 133;A prompt markers to drive
  `isBusy()`, including a `scanTail_` for sequences split across reads.
  So there is precedent — and a parser — for the REPL reporting state to
  the editor out-of-band.

## B1 — CWD indicator

Do this first; it is the cheapest piece and useful on its own.

- Add `ReplSession::workingDir()` exposing the existing
  `lastWorkingDir_`, so the indicator and both commands below share one
  source of truth.
- On every start and restart, emit a banner line via
  `TerminalView::showBanner()`:

  ```
  ▸ cwd: ~/projects/foo
  ```

  Abbreviate `$HOME` to `~`. Style it like existing banner output so it
  reads as editor chrome, not REPL output.

Ships standalone, no dependencies.

## B2 — GUI command

Two actions in the Run menu, next to the existing **Restart REPL**:

- **"Restart REPL in Directory…"** — `QFileDialog::getExistingDirectory`
  defaulting to the current cwd (falling back to the active file's
  directory), then `repl_->restart(dir)`.
- **"Restart REPL in Current File's Directory"** — one-click version of
  the common case; restarts in the active editor's directory.

Both are per-window. Both restart, and the labels say "Restart" rather
than "Set" or "Change" precisely so the state-clearing is honest — a
label like "Set REPL Directory…" would imply a live `cd` that isn't
happening.

Also add control commands `repl.set_cwd {path}` and `repl.get_cwd` so
the smoke suite can drive and assert this.

## B3 — REPL meta-command

A `:cd` command *feels* like it belongs in Trowel, but the mechanics
push the other way. Two options were considered.

### Option A — intercept in Trowel (rejected as the primary path)

`TerminalView::keyPressEvent` would shadow-buffer the typed line and, on
Enter while idle, match `:cd <path>`, consume it, and restart.

This is fragile. Terminal input goes straight to the PTY, where `tur`'s
libedit does the line editing — history recall with ↑, `Ctrl-U`,
mid-line cursor movement, and so on. Trowel's shadow buffer would have
to stay in sync with all of it, and any desync means either swallowing a
real command or missing the meta-command. That is a bad trade for a
feature whose whole point is being simple and predictable.

### Option B — implement in `tur repl`, Trowel listens (recommended)

The REPL owns its own `:cd` / `:pwd`, alongside the `:type`, `:doc`, and
`:explain` meta-commands it already has. The payoff is real: being
in-process, it can `chdir()` **live, with no restart**, so REPL state
survives. Trowel stays out of the input path entirely.

Pair it with the REPL emitting **OSC 7** — the standard "report current
working directory" escape, already understood by most terminals — on any
cwd change. Trowel is parsing tur's OSC stream anyway, so this slots
into `ReplSession`'s existing scanner.

This makes B1's indicator *live* rather than start-only, and keeps
`repl.get_cwd` honest, with no shadow buffer anywhere.

### Staging

Because half of Option B lives in another repo:

- **B3a (Trowel, now).** Consume OSC 7 in `ReplSession`'s existing OSC
  scanner; update the cached cwd, refresh the B1 indicator, and keep
  `repl.get_cwd` accurate whenever the REPL reports a change. Harmless
  if nothing ever sends it.
- **B3b (Turmeric, upstream).** Add `:cd` / `:pwd` to `tur repl` and
  have it emit OSC 7 on change.

Trowel therefore ships the indicator and the GUI command immediately,
and the meta-command lights up the moment the Turmeric side lands — with
no fragile interception layer, and no dependency in B1/B2 on upstream
work.

## Milestones

1. **B1** — `workingDir()` accessor + banner line on start/restart.
2. **B2** — the two Run-menu actions; `repl.set_cwd` / `repl.get_cwd`
   control commands; smoke coverage.
3. **B3a** — OSC 7 consumption, live indicator updates.
4. **B3b** — upstream `:cd` / `:pwd` in `tur repl` (Turmeric repo).

## Non-goals

- Changing the cwd of a running REPL from Trowel's side. Not possible;
  B3b is the answer.
- Auto-following the active tab's directory. Explicitly rejected — see
  the predictability argument in `multi-window.md`.
- A persisted per-window cwd override that survives restart. The cwd is
  derived from the file or set explicitly per session; adding a sticky
  override is a separate decision.
- Shell-style `cd -`, `~user` expansion, or path completion in the GUI
  dialog.

## Future

- A clickable cwd indicator in the window chrome (status bar or REPL
  header) rather than a banner line, once there is a natural home for
  it.
- Project-root detection (nearest VCS root or manifest) as the default
  offered in the directory picker.
- Remembering the last explicitly-set cwd per window across sessions,
  if the derived-from-file default proves insufficient.
