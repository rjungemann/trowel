# Editor intelligence: rename, the tracer, and bracket-pair guides — plan

> **Status:** Proposed. T0 (the version bump) is **done and verified** (§2).
> **Related:** [`lsp-support.md`](lsp-support.md) (phase 1 landed; this is a
> second follow-up alongside `lsp-navigation.md`),
> [`lsp-navigation.md`](lsp-navigation.md) (owns definition / outline /
> occurrences — see §6 for the overlap this plan resolves),
> [`debugger-support.md`](debugger-support.md) (owns the live DAP session; track
> T is its time axis), [`minimap.md`](minimap.md) (owns viewport overlay
> painting; track G borrows the technique).
> **Upstream source:** `../turmeric/docs/archive/editor-intelligence-follow-through-plan.md`
> (tracks S, A, T — executed 2026-08-30) and
> `../turmeric/docs/archive/try-turmeric-tracer-plan.md` (track T3, the browser
> timeline).
> **Reference implementations (design only):** Try Turmeric
> (`../turmeric/web/main.js`, `../turmeric/web/try/index.html`) and c2mp
> (`~/Projects/c2mir-playground/c2mp`).

---

## 0. Summary

Three tracks, independent of each other, all unblocked by one version bump that
has already landed.

| | What | Where it lands |
|---|---|---|
| **R** | Rename symbol, find references, scope-aware occurrence highlight | `src/lsp/`, `src/editor/`, `src/app/` |
| **T** | The time-travel tracer: record a run, scrub it, step backwards | `src/trace/` (new), a timeline in the REPL pane |
| **G** | The active bracket-pair guide — a depth-colored horizontal line under the current expression, and a vertical line down its left edge | `src/editor/` |

R and G are pure Trowel work against a server and a lexer that already do the
hard part. T is the largest of the three and is the one that needs a new
subprocess protocol, so it is phased so that each phase is independently
shippable.

**None of these touch `../turmeric`.** Everything they need shipped upstream in
`v0.42.0`. If any phase below finds itself editing the Turmeric tree, the plan
has gone wrong — file it as a separate upstream plan the way
`lsp-support.md` phase 2 did.

---

## 1. What exists (repo facts)

### Trowel side

| Thing | State | Anchor |
|---|---|---|
| `LspManager`, one server for the app, URI-keyed doc registry | shipping | `src/lsp/lsp_manager.h` |
| `LspClient::request(method, params, reply, timeoutMs)` | shipping, generic | `src/lsp/lsp_client.h` |
| `LspTransport` — `QProcess` + `Content-Length` framing | shipping | `src/lsp/lsp_transport.h` |
| Byte-offset position conversion, one switch | shipping, already `Utf8` | `src/lsp/lsp_position.{h,cpp}` |
| Diagnostics → squiggle + margin marker | shipping | `src/editor/editor_view.cpp:132-139` |
| Completion via `autoCShow`, hover via `callTipShow` | shipping | `src/editor/editor_view.cpp:405-410,426` |
| Indicator slots 8, 9; marker slots 0, 1 | shipping | `src/editor/editor_view.h`, `namespace diag` |
| `updateUi` already connected (caret-move diagnostics readout) | shipping | `src/app/main_window.cpp:552-555` |
| Rainbow brackets, 7 depth levels, lexer-computed | shipping | `src/editor/scanner_turmeric.cpp:426-446`, `src/editor/scanner.h:148`, `src/editor/lexers.h:224` |
| Rainbow palette, theme-driven | shipping | `resources/turmeric-dark.theme.json:61-67`, `src/editor/theme_loader.cpp:86-92` |
| Side bar action registration | shipping | `src/app/main_window.cpp:333` |
| Horizontal `QSplitter`, editor ‖ REPL | shipping | `src/app/main_window.cpp:95` |
| `lsp.*` control commands (6) + `wait.diagnostics` | shipping | `src/control/control_handlers.cpp:879-884,901` |
| `tests/smoke/test_lsp.py`, 12 tests | shipping | — |

What does **not** exist, verified by grep over `src/`: any `textDocument/rename`,
`prepareRename`, `references` or `documentHighlight` request; any brace matching
at all (`SCI_BRACEHIGHLIGHT` is never called); any indentation or bracket-pair
guide; any tracer, and no `tur trace` or `tur dap` child is ever spawned.

### Server side, as pinned (`v0.42.0`)

Probed live from the staged binary, not read from a changelog:

```
$ printf 'Content-Length: %d\r\n\r\n%s' … | \
    build/macos-debug/trowel.app/Contents/Resources/turmeric/tur lsp
"positionEncoding":"utf-8"  "hoverProvider":true  "definitionProvider":true
"documentSymbolProvider":true  "documentHighlightProvider":true
"renameProvider":{"prepareProvider":true}  "referencesProvider":true
"workspaceSymbolProvider":true  "documentFormattingProvider":true
"signatureHelpProvider":{"triggerCharacters":["("]}
"completionProvider":{"triggerCharacters":["("]}
```

```
$ … tur dap   ← initialize
"supportsStepBack":true  "supportsReverseContinue":true
"supportsConditionalBreakpoints":true  "supportsEvaluateForHovers":true
```

```
$ … tur --version
tur: the Turmeric compiler (v0.42.0)
$ … tur trace w2.tur -o w2.turtrace
trace: 6005 steps, 1 enters, 0 pops, 4001 changes, peak depth 1,
       97076 bytes, 8 bytes of output, truncated no
```

---

## 2. T0 — the version bump (done)

`v0.39.0` → **`v0.42.0`**: `CMakeLists.txt:119` plus the three per-arch SHA-256s
at `:128`, `:133`, `:136`, taken verbatim from the release's `sha256sums.txt`,
and `mise.toml` bumped to match so the dev toolchain and the bundled one do not
diverge. Verified: `just build` clean, `FetchContent` accepted all three hashes,
the staged `Resources/turmeric/tur` reports `v0.42.0`, and the probes in §1 are
its answers.

This bump is what unblocks all three tracks, and it settles two open questions
in sibling plans:

- **`lsp-navigation.md`'s T0b is satisfied.** It was gated on "whichever release
  carries Try Turmeric's M5" — real symbol kinds and `documentHighlightProvider`.
  Both are in `v0.42.0`: `lsp_symbol_kind` (`../turmeric/src/lsp/lsp.c:1044`) is
  now a five-case switch over `LSP_KIND_FUNCTION` / `VALUE` / `STRUCT` / `ENUM`
  rather than the two-line `strncmp` that plan quotes, and
  `documentHighlightProvider` is advertised. **T3 and T4 of that plan are
  unblocked; its §4.1 "still owed" paragraph is now stale.**
- **`debugger-support.md` constraint 8 is stale.** It reads "no `stepBack`". The
  adapter now advertises `supportsStepBack` and `supportsReverseContinue`, under
  a `launch` carrying `"replay": true`. See §5.4.

Both corrections are made in place in those documents by this change; they are
noted here because the bump is what caused them.

---

## 3. Track R — rename, references, occurrence highlight

### 3.1 What the server now does, and why that is the whole story

`../turmeric/src/lsp/lsp_scope.c` is new in this release. The symbol index used
to record only *global* bindings, so every consumer answered a **textual**
question when it had been asked a **lexical** one — a parameter named `x`
highlighted every `x` in the file, and rename could not be written safely at
all. The scope pass records one entry per local binding (`defn` and `fn`
parameters, `let` / `letrec` bindings, `match` binders) carrying the region it
is visible in, riding the same elaboration hook the symbol harvest already sits
on.

Read `../turmeric/docs/guides/lsp-guide.md` §"Scope, highlight, rename and
references" before building any of this. The three facts that shape the client:

1. **Rename is safe or it refuses**, and it refuses *early*. That is why the
   server advertises `"renameProvider":{"prepareProvider":true}` rather than the
   bare boolean — `prepareRename` puts the reason in front of the user before
   they type a new name. There are eight documented refusal cases (macro-
   introduced binder, truncated binding table, stdlib symbol, symbol defined in
   another file, exported module without `--rename-exports`, workspace too large
   to verify, a file that uses the name but does not compile, and "not on an
   identifier" which answers `null`).
2. **Rename is cross-file.** Renaming a top-level name edits this file *and*
   every workspace file that imports its module. The returned `WorkspaceEdit`
   therefore names documents Trowel may not have open, and may not even have
   tabs for.
3. **`references` is the same workspace walk without the edit.** An oversized
   workspace returns a *shorter list* rather than an error — an incomplete list
   of references is still true about every entry in it.

### 3.2 R1 — occurrence highlight — **built, under `lsp-navigation.md` T3**

This section is closed. The ordering note it carried said to delete it rather
than build the feature twice if T3 landed first under the navigation plan's
number, and that is what happened.

As shipped: `LspManager::requestDocumentHighlights`, a 250 ms caret-settle
debounce in `EditorView` (matching the existing didChange cadence rather than
the ~150 ms proposed here), indicator slot **10** with `INDIC_ROUNDBOX` under
the text, and an `occurrenceHighlight` theme key falling back to `selectionBg`
at low alpha. No textual fallback, as both plans required.

**One decision went the other way.** This section proposed a second slot (11)
for the definition, since the server distinguishes it (`kind` 3) from uses
(`kind` 1). That was dropped: `kind` is discarded in the manager. Two washes of
different strength down the same buffer is a second decoration to explain and
to theme, and the definition is already the row the outline preselects and the
place `F12` lands. If it turns out to be wanted, the `kind` is one line away in
`requestDocumentHighlights`.

### 3.3 R2 — find references

- A **"Find References"** action (Shift+F12, the conventional binding) in the
  Edit menu, plus the context menu.
- Results go in a **results list**, not a dialog: reuse the side-pane pattern
  `lsp-navigation.md` §5 designs for the outline. If that lands first, this is a
  second consumer of the same widget; if this lands first, build it so the
  outline can reuse it. Either way, one list widget, two producers.
- Each row is `file:line:col` plus the line's text. Activating a row opens the
  file via `MainWindow::openPath()` and selects the range — the same jump
  `lsp-navigation.md` T1 builds for go-to-definition. **Share the navigation
  history stack with T1**; a references jump that cannot be undone by the same
  "go back" the definition jump uses is a worse tool than either alone.
- `includeDeclaration: true`. The declaration is what the user is usually
  looking for.
- Honour the truncation case: when the server returns a short list for an
  oversized workspace it says nothing in the protocol, so the list must not
  claim completeness. Label the panel "References" and not "All references".

### 3.4 R3 — rename symbol

The load-bearing one, and the one where getting the UI wrong is destructive.

- **F2** on the caret, plus an Edit-menu action, plus the context menu.
- **`prepareRename` first, always.** Send it on invocation and *before* showing
  any input. Three outcomes:
  - a range → show the rename input, pre-filled with the current text;
  - `null` → status bar: "no symbol to rename here"; no input;
  - an error with a message → **show that message verbatim** in a modal or the
    status bar and stop. The server's messages are written to be read by a user
    ("cannot rename a macro-introduced binding", "renaming an exported symbol
    needs `--rename-exports`"); paraphrasing them loses the reason.
- **The input is inline, not a dialog.** Scintilla has no rename widget, so this
  is a small frameless `QLineEdit` positioned at the symbol via
  `SCI_POINTXFROMPOSITION` / `SCI_POINTYFROMPOSITION`, dismissed on Escape,
  committed on Enter. A modal dialog is the fallback if the inline widget fights
  the editor for focus — ship the dialog first if that is faster, and treat the
  inline version as polish.
- **Applying the `WorkspaceEdit` is the risky part.** The reply's `changes` map
  is keyed by URI and may name files with no open tab. Rules:
  - Apply edits to an open buffer **through Scintilla**, in one
    `beginUndoAction` / `endUndoAction` pair per document, **descending by
    position** so earlier offsets stay valid.
  - For a file with no open tab, **open a tab for it** and apply the edit there,
    leaving it dirty. Do not write to disk behind the user's back. A rename that
    silently modified six files on disk is not undoable by Ctrl+Z, and Trowel
    has no VCS integration to fall back on.
  - If *any* document in the edit fails to resolve to a path Trowel can open,
    apply **none** of it and say why. A half-applied cross-file rename is worse
    than a refused one.
  - Cap the blast radius: if the edit names more than N documents (start at 20),
    confirm first, listing them.
- **Timeout.** The workspace half compiles each importing file for its own
  binding table before editing it, so rename is the most expensive request in
  the protocol. `LspClient::request`'s default is 2000 ms and that is too short.
  Give rename and `prepareRename` their own budget (start at 10 s) and show a
  busy indicator; on timeout, apply nothing.
- **`--rename-exports` is a preference, default off**, surfaced in
  `PreferencesView` next to the existing `lsp/enabled` checkbox and passed as an
  argument when `LspManager` spawns the server. It is off upstream by default
  because an exported name is published surface that files outside the workspace
  may import. Changing it restarts the server, which the existing "Restart
  Language Server" path already does.

### 3.5 R4 — control API + smoke tests

New handlers in `src/control/control_handlers.cpp` (dispatch table at `:879`):
`lsp.highlights`, `lsp.references`, `lsp.prepare_rename`, `lsp.rename`. The
house rule in [`smoke-tests.md`](smoke-tests.md) is **tests never sleep**, so
each is a request/reply and anything asynchronous gets a `wait.*` sibling built
on the existing `WaitCtx` / `ArmTimeout` helpers.

`tests/smoke/test_lsp_rename.py`, with fixtures under `tests/smoke/fixtures/`:

| Test | Asserts |
|---|---|
| Rename a `let` binding | only the occurrences in its scope changed |
| Rename the outer of a shadowed pair | the inner binding is untouched |
| `prepareRename` on a stdlib symbol | refuses, with the server's message |
| `prepareRename` on a macro-introduced binder | refuses, with the server's message |
| Rename a top-level name imported by a sibling file | both files edited, both left dirty |
| Highlight a parameter | marks stop at the function, none in comments or strings |
| References with `includeDeclaration` | the declaration is in the list |

The shadowing tests are the ones that matter. They are cheap to write and they
are the only thing standing between "rename works" and "rename quietly corrupted
a file".

---

## 4. Track G — the active bracket-pair guide

### 4.1 What is being built

The feature as it exists in Try Turmeric (`../turmeric/web/main.js:1943-1952`):

```js
bracketPairColorization: { enabled: true, independentColorPoolPerBracketType: false },
guides: {
    bracketPairs: true,
    bracketPairsHorizontal: 'active',
    highlightActiveBracketPair: true
},
```

Monaco draws, for the bracket pair enclosing the caret: a **vertical** guide
down the left edge of the pair, and a **horizontal** segment along the bottom of
the pair's last line, running from the guide column to the closing bracket. Both
take the pair's **nesting-depth color** — the same color as the parens
themselves. In Monaco that falls out of the defaults, keyed off
`editorBracketHighlight.foregroundN`
(`../turmeric/web/main.js:1708-1716`, which is itself mirrored *from* Trowel's
`turmeric-dark.theme.json`). There is no explicit `editorBracketPairGuide.*`
entry anywhere in that tree.

The horizontal segment is the primary ask; the vertical guide is the companion.

### 4.2 Why this is cheaper in Trowel than it looks

Two facts, both verified in the source:

**One: the pair's depth color is already on the closing bracket's style byte.**
`scanner_turmeric.cpp:426-446` emits an opener at depth *d* as
`RainbowStyleForDepth(d)`, then increments; a closer **decrements first** and
then emits `RainbowStyleForDepth(d)`. So a matching pair carries the *same*
style, and the guide color is `sci_->styleAt(openerPos)` mapped through the
theme. Nothing computes a depth twice, and the guide cannot drift out of sync
with the parens it belongs to — which is exactly the property Monaco gets for
free and the reason it looks right.

**Two: the lexer makes the enclosing-pair scan comment-safe and string-safe for
free.** A `(` inside a string or a `;` comment is styled `String` or `Comment`,
never a rainbow style. So a backward scan that only counts characters *styled as
brackets* skips them without a second parser.

**Three, the catch:** `SCI_BRACEMATCH` only answers when the caret is already
adjacent to a brace, and Scintilla has **no** notion of "the pair enclosing this
position". That scan is ours. It is also why `kRainbowLevels = 7` cannot be used
to shortcut it — style cycles mod 7, so depth must be *counted*, not read.

### 4.3 G1 — the horizontal segment

- **Finding the pair.** On `updateUi` with `SC_UPDATE_SELECTION` (or content
  change), scan backward from the caret counting bracket-styled closers until an
  unmatched opener, then `SCI_BRACEMATCH` forward from it — or continue the scan
  — for its closer. **Bound the scan**: cap it at some number of bytes back
  (start at 64 KB) and give up rather than stall, because this runs on every
  caret move. A give-up paints nothing, which is the honest failure.
- **Drawing.** Apply a new indicator (**slot 12**) with `INDIC_PLAIN` — a single
  straight line under a range — over the closing line, from the opener's column
  to the closer inclusive. Because indicators paint over whitespace, the leading
  indentation is covered, which is what makes it read as a segment rather than
  an underline of the text.
- **One indicator, one color.** Only the *active* pair is ever drawn, so the
  indicator's `SCI_INDICSETFORE` can simply be reset per update from the pair's
  style color. No per-depth indicator slots are needed.
- **Clear before painting**, the way `clearDiagnosticDecorations()` already
  does. The guide is transient by definition.
- **Match Monaco's suppression rules** rather than inventing new ones. Check
  against the real thing in Try Turmeric: whether a pair entirely on one line
  gets a segment, and what happens at the top level with no enclosing pair. Write
  the answers down here.
- **Preference + theme.** `editor/bracketPairGuides`, default on, next to the
  existing `editor/rainbowBrackets` checkbox in `PreferencesView` — and it should
  follow rainbow brackets: with rainbow *off*, brackets fall back to the flat
  `Delim` / `CurlyInfix` styles and there is no depth color, so either fall back
  to one neutral color or hide the guide. Pick one and say which. Optionally add
  a `bracketPairGuide` key to the theme's `styles` block for the case where the
  guide should be *dimmer* than the parens — this is the same escape hatch the
  Monaco note names.

### 4.4 G2 — the vertical guide

Scintilla's own indentation guides (`SCI_SETINDENTATIONGUIDES` +
`SCI_SETHIGHLIGHTGUIDE`) are the obvious candidate and are the **wrong tool**:
they are drawn at multiples of the indent width, in the single `STYLE_INDENTGUIDE`
color, and a lisp opener is rarely at a multiple of the indent width. They would
draw a line in the wrong column in the wrong color.

So the vertical guide is a **viewport overlay**: a transparent child widget over
`ScintillaEditBase`'s viewport with `WA_TransparentForMouseEvents`, painting one
line at `SCI_POINTXFROMPOSITION(0, openerPos)` from the opener's row to the
closer's row. This is the same technique [`minimap.md`](minimap.md) phase 4 uses
for its overlay pass; **if minimap phase 4 lands first, reuse its overlay rather
than adding a second one.**

G2 is genuinely more work than G1 and is not what was asked for first. Ship G1,
then G2.

### 4.5 G3 — tests

`lsp.decorations` already reads indicator ranges back out of Scintilla
(`lsp-support.md` Deviations §5) — extend it, or add `editor.decorations`, to
report the guide indicator. Then `tests/smoke/test_bracket_guides.py`: put the
caret inside a known nested form in a fixture and assert the indicator range is
the closing line's span and its color is the depth color of the enclosing pair.
Asserting on a computed pair without asserting on the painted range proves
nothing — that is the lesson `lsp-support.md` already recorded.

---

## 5. Track T — the tracer

### 5.1 What shipped upstream

`tur trace` records every node the interpreter evaluates into a byte buffer that
can be read back, seeked, and replayed. Read
`../turmeric/docs/guides/time-travel-tracing-guide.md` before building any of
this; `../turmeric/src/turi/trace.h` is the format and the reader.

```sh
tur trace <file.tur>                    # record; print a summary
tur trace <file.tur> -o run.turtrace    # record; write the bytes
tur trace <file.tur> --max-steps=50000  # cap the recording
tur trace --dump run.turtrace           # read one back, record by record
```

The format is nine records — `ENTER`, `STEP`, `POP`, `OUTPUT` — over an interned
name table, all little-endian, carrying **deltas, not states**: a `STEP` holds
only the bindings whose rendered value changed since that frame's last step.
Program stdout is captured through a pipe and interleaved as `OUTPUT` records in
step order, so scrubbing backwards rewinds the transcript with the cursor.

### 5.2 Measured on the bundled binary, not quoted from upstream

The upstream numbers are from a Debug + ASan build. These are from the `v0.42.0`
release binary Trowel actually stages, on a 50,000-iteration `while` loop:

| | |
|---|---|
| `tur interpret` (untraced) | **0.024 s** |
| `tur trace` | **0.103 s** — ~4.3x, consistent with upstream's ~5x |
| Recording | 150,005 steps, **2,623,745 bytes** — ~17.5 bytes/step |
| At the 200,000-step default cap | ~3.5 MB |

A few megabytes held in Trowel's own address space is unremarkable — Trowel is a
native app, not a browser tab, which is why Try Turmeric caps at 50,000 and this
plan can keep the native default.

### 5.3 The three constraints that shape the UI

Found by driving the staged binary, and each one is a thing the UI has to say
out loud rather than discover:

1. **The recorder is an interpreter feature, and most real programs are not
   interpreted at the node level.** A `defn` with type annotations compiles, and
   compiled frames record nothing. Measured: `(defn fib [n :int] :int …)` with a
   `main` calling `(fib 12)` records **4 steps, peak depth 2** — the recording
   covers `main`'s body and nothing inside `fib`. The same program without
   annotations records 2 steps. A `while` loop in `main` records 150,005.
   **A user who traces a typical annotated program and gets a four-step timeline
   will conclude the feature is broken.** The UI must say what was recorded and
   why it is short. This is the single biggest risk in the track (§8).
2. **A program with no `main` records nothing.** `tur trace` on a file whose work
   is at top level reported `0 steps` while still printing the program's output.
   Try Turmeric handles this by checking for a top-level `main` and running
   `(main)` after loading the forms; Trowel needs the same rule, and needs to
   explain the empty case rather than showing an empty scrubber.
3. **A program that does not compile reports `0 steps` alongside its error**, on
   the same stream. The tracer exits 0-ish and cheerfully summarises an empty
   recording. Trowel must surface the compile error — which the LSP has usually
   already published as a diagnostic — and must not open a timeline.

### 5.4 Two ways in, and the recommendation

**Option A — drive `tur trace` as a subprocess and parse `.turtrace` in Trowel.**
Full control of the timeline, no DAP. But it means writing a **second decoder for
the format**, in C++, against a C reader that is the single source of truth
upstream. The tracing guide is explicit that Try Turmeric deliberately did *not*
do this — "the page does not decode the format… so there is one decoder for
`.turtrace` and it is the C one" — and it had a wasm boundary as an excuse to.
Trowel would have less excuse and the same bug surface.

**Option B — `tur dap` with `"replay": true`.** The adapter records the whole run
and then serves `stackTrace`, `scopes` and `variables` from a trace cursor,
advertising `supportsStepBack` and `supportsReverseContinue` (verified in §1).
The decoder stays in C. The transport is DAP over stdio — which
[`debugger-support.md`](debugger-support.md) already designs in full, and which
reuses the `LspTransport` framing `lsp-support.md` built.

**Recommend B, and therefore: build `debugger-support.md` phases 1–3 first.**
Replay is *the same client* with three extra requests and a different `launch`
argument. Building a separate tracer client first would mean building the
frames pane, the variables pane, the gutter and the stepping controls twice.

Two deliberate limits carry over from upstream and must be surfaced, not
papered over:

- **`evaluate` refuses in replay.** There is no live frame. Relaunch without
  `"replay"` for a session that can evaluate. The UI should say so where the
  evaluate box is, rather than showing an error per keystroke.
- **Conditional breakpoints become unconditional in replay**, for the same
  reason. That stops more often than asked, never less.

### 5.5 Phases

| Phase | Scope | Gate to the next |
|---|---|---|
| **T1** | A **"Trace"** action beside "Run Buffer" that shells out to `tur trace`, writes to a temp `.turtrace`, and prints the summary line into the REPL pane. No timeline. | A traced fixture reports a plausible step count; the three §5.3 cases each produce a *specific* message, not an empty panel |
| **T2** | `debugger-support.md` phases 1–3 (transport, launch, breakpoints, stack, variables) — **not this plan's work**, listed for ordering | Its own gates |
| **T3** | `"replay": true` launch; wire `stepBack` / `reverseContinue` / `reverseNext` to controls; the gutter follows the cursor | Stepping backward into a returned frame shows that frame's values |
| **T4** | The timeline strip: a slider over `[0, steps)`, first / step-back / step-forward / last, `file:line` for the cursor. Coalesce seeks — a drag issues one seek at a time and remembers only the most recent target | Scrubbing a recursive fixture end to end moves the gutter monotonically and never exceeds peak depth |
| **T5** | Console replay: the REPL pane shows what the program had printed by the cursor's step, and puts the user's own transcript back on close | Scrubbing backwards rewinds the transcript |
| **T6** | A **call-depth ribbon** under the slider (§5.6) | Recursion shape is visible at a glance and the ribbon never disagrees with the frame count |

T1 is worth shipping alone: it is an afternoon, it costs nothing to throw away,
and it is what answers §5.3's three questions with real fixtures before any UI
depends on the answers.

### 5.6 T6 — the depth ribbon

Worth calling out because it is the one piece of the browser timeline that
**Try Turmeric did not build**, and its own plan says so: `../turmeric/docs/archive/try-turmeric-tracer-plan.md`
§9 "Left" lists "a depth ribbon under the slider (c2mp did not build one either;
it is the cheapest way to see recursion shape)". Only a `peak depth N` figure in
the summary banner shipped.

The reason it is cheap upstream is `turi_trace_replay_depth_at`, which **reads an
index without seeking**. The tracing guide is emphatic about why that matters:
asking "where was step N" by seeking, once per candidate, turns a scan of an 80k
recording from 0.02 s into not finishing. A ribbon samples one value per pixel
across the whole run, so it is exactly the caller that would fall into that trap.

Over DAP there is no `depth_at` request. So T6 needs one of:

- a DAP `custom` request upstream that returns a downsampled depth array — small,
  additive, and the right shape; or
- reading the `.turtrace` header and `ENTER`/`POP` records in Trowel *for the
  ribbon only* — depth is a `u16` on those two record types and needs no name
  table, so this is a much smaller decoder than a full reader, and it is the only
  place option A's objection is weak.

**Decide this before T4**, because it is the one part of the track that may need
an upstream change, and `lsp-support.md` phase 2 is the precedent for how that
gets sequenced: land it upstream, cut a release, bump the pin.

---

## 6. Overlap with `lsp-navigation.md`

Both plans now have a claim on occurrence highlight. Resolve it once:

- `lsp-navigation.md` **T3 is the same feature** as §3.2 here. **Resolved:** T3
  shipped under the navigation plan, and §3.2 above is now a record of that
  rather than work to do.
- The **jump-and-go-back stack** is `lsp-navigation.md` T1's. **It exists** —
  `MainWindow::NavEntry` plus `navBack_`/`navForward_`, keyed by path and byte
  offset, with `nav.history` on the control API. §3.3 is a second consumer, not
  a second implementation; push onto it before a Find References jump exactly
  as `jumpToDefinition` and the outline already do.
- The **results list widget** is shared between that plan's outline and this
  plan's references panel. The outline landed first and chose
  `SCI_USERLISTSHOW` (`EditorView::showSymbolList`), which suits a
  single-buffer list. A references panel spans files and probably wants the
  palette `lsp-navigation.md` §6.2 defers — so this is *not* settled by the
  outline having gone first, and §6.2's "do not build it speculatively" still
  applies.
- The **minimap occurrence lane** stays `minimap.md`'s, with the ordering
  constraint `lsp-navigation.md` §2 already states: it must not ship with a
  textual matcher behind it.

---

## 7. Verification

1. `just build` — clean under `-Wall -Wextra -Wpedantic -Werror`.
2. `just smoke` — existing suite unchanged; new `test_lsp_rename.py` and
   `test_bracket_guides.py` pass.
3. `just run`, driven by hand — the real check:
   - **R:** caret on a `let` binding → its occurrences mark and stop at the
     scope; F2 → inline input; rename → the scope changes and the shadowing
     sibling does not; F2 on a stdlib symbol → the server's refusal message;
     rename a name a sibling file imports → both tabs open and dirty.
   - **G:** caret inside a nested form → a depth-colored line under the closing
     line, in the same color as the enclosing parens; move out one level → the
     color changes with it; turn rainbow brackets off → the documented fallback,
     not a stale line.
   - **T:** Trace a fixture with a `while` loop → a plausible step count; trace
     an annotated `defn` → the short-recording explanation, not an empty
     timeline; trace a file that does not compile → the error, no timeline.
4. Confirm no stray `tur trace` or `tur dap` children survive app quit
   (`pgrep -f 'tur (trace|dap)'`), the same check `lsp-support.md` §5 makes for
   `tur lsp`.

---

## 8. Risks

- **The tracer records almost nothing for typed code** (§5.3.1). This is not a
  bug to fix in Trowel and it is not something a better UI hides. T1 exists to
  measure how bad it is against realistic files *before* T3–T6 are built on top;
  if the answer is "four steps for anything anyone writes", the honest outcome is
  to ship T1 and stop, and file the gap upstream.
- **A cross-file rename is the most destructive operation in the editor.** The
  mitigations are in §3.4 — all-or-nothing application, leave buffers dirty,
  confirm above a threshold — and the shadowing smoke tests in §3.5 are what keep
  them honest.
- **The enclosing-pair scan runs on every caret move.** Bounded at 64 KB and
  giving up rather than stalling (§4.3); if it still shows up in a profile on a
  large file, cache the pair and invalidate on edit.
- **Rename's 10 s budget blocks a single-threaded server.** While a rename is in
  flight, diagnostics, completion and hover all queue behind it. Show that the
  editor is busy rather than appearing hung.
- **T6 may need an upstream change** (§5.6). Decide before T4, not during it.
