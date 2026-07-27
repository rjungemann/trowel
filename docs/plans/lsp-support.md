# Language server support — plan

Wire Trowel to `tur lsp`, the language server already shipping inside the `tur`
binary Trowel bundles: diagnostics, completion, and hover in phase 1, then a
follow-up phase upstream in the turmeric repo to fix the server-side limitations
phase 1 has to work around.

Expands on [`PLAN.md`](PLAN.md) §11, which sketched this work but deferred it.

## Context

Trowel is a Qt6 + Scintilla editor for Turmeric that already bundles a pinned `tur`
binary (`TROWEL_TURMERIC_VERSION v0.30.8`, staged into
`Trowel.app/Contents/Resources/turmeric/`). That binary already ships a working
language server as the `tur lsp` subcommand — verified live against the bundled
binary, which answers `initialize` over stdio with:

```
textDocumentSync: 1 (full)   hoverProvider   definitionProvider
documentSymbolProvider       workspaceSymbolProvider
completionProvider { triggerCharacters: ["(", " "] }
```

plus server-pushed `textDocument/publishDiagnostics` on every didOpen/didChange.

Trowel consumes none of it. Today errors surface only as REPL terminal text, there
is no completion popup, and no hover. `docs/plans/PLAN.md` §11 already names this
work ("Autocomplete + diagnostics via `tur lsp` … keep the LSP client behind an
interface"). This plan delivers **diagnostics, completion, and hover** on a reusable
LSP client, then a follow-up phase upstream in `../turmeric` to fix the server-side
limitations Trowel has to work around.

Out of scope for phase 1 (the plumbing supports them; they are cheap follow-ups):
go-to-definition, documentSymbol outline, workspace/symbol palette.

## Constraints discovered

- **Trowel is single-threaded.** No threads anywhere; all async work is
  `QSocketNotifier` + `QTimer` + signals on the Qt event loop. Keep it that way.
- **The server is blocking and single-threaded.** Every `didChange` writes the
  buffer to a `/tmp` temp file and runs a full symbol-collecting compile inline.
  There is no `$/cancelRequest`. Trowel must debounce, cap in-flight work, and
  time out.
- **Position encoding mismatch.** `EditorView::lineColFromPos` returns a *byte*
  column; LSP defaults to UTF-16 `character`; Turmeric's `lsp_util.c` actually
  uses *bytes*. Phase 1 matches the server (bytes) behind one switch; phase 2
  makes it protocol-correct.
- **No document version counter** and **nothing listens to Scintilla's
  `modified` signal** today. Both need adding.
- **No third-party JSON dep** — use Qt's `QJsonDocument`/`QJsonObject`, already
  used in `src/control/` and `src/editor/theme_loader.cpp`.

---

## Phase 1 — Trowel LSP client

### 1. Transport — `src/lsp/lsp_transport.{h,cpp}`

`class LspTransport : public QObject`, modelled on `src/repl/pty_session.h` but
using **`QProcess` with plain pipes, not `forkpty`** — a pty would mangle the
byte stream (ONLCR) and the server needs no tty.

```cpp
bool start(const QString& program, const QStringList& args,
           const QString& workingDir = {}, const QStringList& extraEnv = {});
bool isRunning() const;
void send(const QJsonObject& msg);          // writes Content-Length framing
void terminate();
signals:
  void messageReceived(const QJsonObject& msg);
  void started();
  void finished(int exitCode);
  void startFailed(const QString& message);
```

Framing: accumulate `readyReadStandardOutput` into a `QByteArray buf_`, parse
`Content-Length: N\r\n\r\n` headers, pop complete bodies — the same
carry-a-partial-tail idiom already used twice in `src/repl/repl_session.cpp`
(`scanTail_`, `cwdTail_`). Route stderr to `qWarning` and drop it.

### 2. JSON-RPC layer — `src/lsp/lsp_client.{h,cpp}`

`class LspClient : public QObject` owns an `LspTransport`.

```cpp
using Reply = std::function<void(const QJsonValue& result, const LspError* err)>;
int  request(const QString& method, const QJsonObject& params,
             Reply reply, int timeoutMs = 2000);
void notify(const QString& method, const QJsonObject& params);
signals:
  void notificationReceived(const QString& method, const QJsonObject& params);
```

Correlation is a `QHash<int, Pending>` where `Pending` holds the `Reply` plus a
timeout `QTimer` — reuse the exact shape of `WaitCtx` / `ArmTimeout` in
`src/control/control_handlers.cpp` (~lines 525–620), including the
**called-exactly-once** guarantee. On timeout, fire the reply with an error and
forget the id.

### 3. Session + document registry — `src/lsp/lsp_manager.{h,cpp}`

One `LspManager` for the whole application, owned by `TrowelApplication`
(`src/app/trowel_application.h`) and reachable from every `MainWindow`. Rationale:
the server only indexes *open documents* (`workspace/symbol` iterates the open-doc
store), so `rootUri` buys nothing, and one blocking compiler is better than N.

Responsibilities:

- **Lifecycle.** Lazy-start on the first Turmeric document opened. Spawn via the
  existing free function `trowel::ResolveTurBinary()` (`src/repl/repl_session.h`)
  with args `{"lsp"}` and the same `TUR_STDLIB_DIR=<sibling stdlib>` pin that
  `ReplSession::start` applies (`src/repl/repl_session.cpp:131-138`). Restart on
  crash with backoff, capped (e.g. 3 attempts) then disable and report once in the
  status bar. `initialize` → `initialized`; `shutdown` + `exit` on app quit.
- **Document registry.** `QHash<QString /*uri*/, DocState>` where `DocState` holds
  the version, the owning `EditorView*`, and the debounce timer. Attach on tab
  open and on `filePathChanged` when `EditorView::language() == Language::Turmeric`;
  detach (`didClose`) on tab close or language change away.
  **Buffers with no path are skipped in v1** (the server needs a real `file://`
  URI to remap diagnostics) — documented, not silently ignored.
- **Change pipeline.** Per-document 250 ms coalescing `QTimer`; on fire send a
  full-text `didChange` (server is `textDocumentSync: 1`). Track one outstanding
  analysis at a time; if a change lands while one is in flight, re-arm rather
  than stack.
- **Staleness guard.** A monotonic generation counter per document; completion and
  hover replies that arrive after the version or caret moved are dropped.
- **Public API** consumed by the UI: `openDocument/closeDocument/documentChanged`,
  `requestCompletion(EditorView*, int pos, cb)`, `requestHover(EditorView*, int pos, cb)`,
  `diagnosticsFor(uri)`, and signals `diagnosticsUpdated(uri)`, `stateChanged()`.

### 4. Position conversion — `src/lsp/lsp_position.{h,cpp}`

`LspPositionFromPos(ScintillaEdit*, int pos)` and `PosFromLspPosition(...)`,
wrapping the existing `EditorView::lineColFromPos` / `posFromLineCol`.
A single `kPositionEncoding` constant selects byte vs UTF-16 columns; **phase 1
ships bytes to match `lsp_util.c`**, with the UTF-16 branch written and a comment
pointing at the phase-2 `positionEncoding` negotiation. Isolating this in one
file is the whole point — flipping it later is a one-line change.

### 5. `EditorView` changes — `src/editor/editor_view.{h,cpp}`

- Add `int docVersion_` and connect `ScintillaEditBase::modified`, bumping the
  version on `SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT` and emitting a new
  `contentChanged(int version)` signal. Nothing listens to `modified` today.
- Enable the reserved symbol margin: `applyDefaultStyling()` currently sets
  `setMarginWidthN(kSymbolMargin, 0)` (`editor_view.cpp:50`) — give it width ~16,
  `setMarginSensitiveN` true, and `markerDefine` an error and a warning marker.
- Diagnostic indicators: `indicSetStyle(kDiagError, INDIC_SQUIGGLE)` /
  `indicSetFore(...)`, likewise for warning; expose
  `setDiagnostics(const QVector<Diagnostic>&)` which clears the previous
  indicator ranges and markers before painting the new batch.
- Hover plumbing: `setMouseDwellTime(500)`; connect `dwellStart(x,y)` /
  `dwellEnd`.
- Completion plumbing: connect `charAdded(int)`; expose the word-start position
  for `lengthEntered`.

### 6. UI wiring — `src/app/main_window.{h,cpp}`

- **Diagnostics.** Subscribe to `LspManager::diagnosticsUpdated(uri)`, resolve the
  uri to the `EditorView` in this window, call `setDiagnostics`. On `updateUi`,
  show the diagnostic under the caret via `statusBar()->showMessage(...)` — the
  established idiom throughout `main_window.cpp`.
- **Completion.** Fire on `charAdded` for the server's trigger characters
  (`(`, space) and on an explicit **Ctrl+Space** `QAction` added to the Edit menu.
  On reply: `autoCSetSeparator('\n')` then `autoCShow(lengthEntered, list)`.
  The server returns a *bare `CompletionItem[]`*, not a `CompletionList` — the
  parser must accept both.
- **Hover.** On `dwellStart` map the point to a position, request hover, and show
  the result with `callTipShow(pos, text)`; `callTipCancel` on `dwellEnd`. The
  server returns markdown with fenced code blocks — strip the fences to plain
  text for the calltip. Also add a caret-based "Show Documentation" menu action.
- **Control.** A "Restart Language Server" action next to the existing
  "Restart REPL In…" action (recent commit `5f72d0d`), plus an `lsp/enabled`
  (default true) and `lsp/serverPath` `QSettings` pair with a checkbox in
  `src/app/preferences_view.cpp`, mirroring `repl/turBinary`.

### 7. Theme — `resources/turmeric-dark.theme.json` + `src/editor/theme_loader.{h,cpp}`

Add `diagnosticError` / `diagnosticWarning` / `diagnosticHint` entries to the
`styles` block (it is a `QHash<QString, StyleSpec>` keyed by name, so this is
declarative) and apply them to the indicator/marker colors in
`ApplyThemeToEditor`.

### 8. Control API + smoke tests

Trowel has no C++ unit tests; testing is pytest driving a real process over the
control socket, and the house rule in `docs/plans/smoke-tests.md` is **tests never
sleep** — every wait is a `wait.*` command with an explicit timeout.

- New handlers in `src/control/control_handlers.cpp` (dispatch table ~line 686):
  `lsp.status`, `lsp.diagnostics`, `lsp.completions`, `lsp.hover`, `lsp.restart`,
  and `wait.diagnostics` built on the existing `WaitCtx` / `ArmTimeout` helpers.
- New `tests/smoke/test_lsp.py`. `tests/smoke/fixtures/syntax_error.tur` already
  exists and is exactly the diagnostics fixture needed; `hello.tur` covers
  completion and hover.

### 9. Build

Add the new `src/lsp/*.cpp` files to the **explicit** source list in
`CMakeLists.txt` (~lines 45–60) — there is no globbing. No new dependencies:
`Qt6::Core` (QJson, QProcess) is already linked.

---

## Phase 2 — Turmeric server improvements (`../turmeric/doc-stuff`)

Landed upstream, then consumed by Trowel via a version bump. Ordered by value:

1. **Stop recompiling to `/tmp` on every keystroke** — `run_doc_analysis`
   (`src/lsp/lsp.c:112-177`) writes a temp file and runs a full compile inline on
   the single-threaded loop, so hover and completion queue behind it. At minimum
   debounce server-side; better, support `$/cancelRequest`.
2. **`general.positionEncoding` negotiation** (LSP 3.17) advertising `utf-8`, so
   the byte columns both sides already use become protocol-correct rather than
   accidentally-compatible. Then flip nothing in Trowel — it already matches.
3. **`textDocument/signatureHelp`** — the calltip logic already exists, unwired,
   in `src/cli/lsp_lite.c` (`calltip` method).
4. **`textDocument/formatting`** backed by `tur format`. Today VS Code shells out
   client-side (`vscode-syntax-ext/extension.js:11-34`) and Trowel blocks the UI
   doing the same in `MainWindow::formatFile()` (`main_window.cpp:908`).
5. **`\uXXXX` decoding** in `unescape_json` (`src/lsp/lsp.c:89`) — currently only
   `\" \\ \n \r \t` are handled, a real correctness bug for non-ASCII source.
6. **Incremental sync** (`textDocumentSync: 2`). `on_did_change`
   (`src/lsp/lsp.c:231`) currently reads only `contentChanges[0].text` and would
   silently mishandle ranges.
7. **Doc refresh** — `docs/guides/lsp-guide.md` says completion is "Not yet
   supported" (it is) and that the VS Code extension is syntax-only (it ships a
   LanguageClient); `docs/archive/lsp-hover-definition-completion-plan.md`'s
   status header is likewise stale.

Consuming it in Trowel means cutting a Turmeric release, then bumping
`TROWEL_TURMERIC_VERSION` **and all three per-arch SHA-256s** in
`CMakeLists.txt:109-125`.

---

## Verification

1. `just build` — must stay clean under `-Wall -Wextra -Wpedantic -Werror`.
2. `just smoke` — existing suite must not regress; new `test_lsp.py` must pass:
   open `fixtures/syntax_error.tur` → `wait.diagnostics` returns ≥1 error with a
   plausible range; `lsp.completions` in `hello.tur` returns a known stdlib
   symbol; `lsp.hover` over a known symbol returns a type string.
3. `just run` and drive it by hand — the real check:
   - open a `.tur` with an error → squiggle under the offending form, marker in
     the gutter, message in the status bar when the caret is on it; fix the error
     → both clear within ~250 ms.
   - type `(` → completion popup with stdlib symbols; typing narrows it; Escape
     dismisses; Enter inserts.
   - hover a symbol → calltip with its type and docstring.
   - "Restart Language Server" recovers after `pkill -f 'tur lsp'`.
   - `lsp/enabled = false` → no `tur lsp` child spawns at all.
4. Independent protocol sanity check outside the editor:
   `printf 'Content-Length: …\r\n\r\n{…}' | .../tur lsp` — the probe used to
   confirm the capability set above.
5. Confirm no stray `tur lsp` processes survive app quit (`pgrep -f 'tur lsp'`).

## Risks

- **Latency under the blocking server.** A large file recompiled per debounce may
  starve completion/hover. Mitigated by the 250 ms debounce, the single-in-flight
  cap, and 2 s request timeouts; genuinely fixed only by phase 2 item 1.
- **Non-ASCII source** is mis-positioned by both sides in phase 1 (bytes vs
  UTF-16). Contained to `lsp_position.cpp`; resolved by phase 2 item 2.
- **Untitled buffers get no LSP** in v1 — a visible gap worth calling out in the
  status bar rather than failing silently.
