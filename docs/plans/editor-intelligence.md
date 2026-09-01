# Editor intelligence: rename, the tracer, and bracket-pair guides — plan

> **Status:** **Track R and track G shipped; track T shipped through T1.**
> T0 done and verified (§2), plus a follow-on bump to **`v0.42.1`** (§2.1) for
> the per-expression tracer. R1 had already landed under
> `lsp-navigation.md` T3; R2/R3/R4 and G1/G2/G3 are built and tested. T1 is
> built. Its first conclusion — that typed code cannot be traced — was **wrong
> and is retracted in §5.3.1**; the cause was source layout under line-granular
> stepping, fixed upstream by Turmeric `e7140c97c`. T3–T6 are not blocked, and
> need only a pin bump past that commit (§5.5). Deviations are in §9.
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

### 2.1 The follow-on bump — `v0.42.0` → `v0.42.1` (done)

Cut upstream to carry `e7140c97c`, the per-expression tracer (§5.3.1).
`CMakeLists.txt:123` plus the three per-arch SHA-256s at `:132`, `:137`,
`:140`, taken verbatim from the release's `sha256sums.txt` and cross-checked
against the digests the GitHub release API reports for the same assets; and
`mise.toml` to match.

Verified: `just build` clean, `FetchContent` accepted the macOS hash, the
staged binary reports `v0.42.1`, its `stdlib/` came with it (145 entries), and
the full smoke suite passes **198/198 with no test touched outside
`test_trace.py`** — which is what §13 of `lsp-navigation.md` asks a bump to
demonstrate, since a compiler release reaches the REPL, the run path and the
formatter as well as the tracer.

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

### 3.1.1 Measured against the staged `v0.42.0` binary

Probed over stdio the way `LspManager` spawns it, on a fixture holding a global
`total`, a `let` binding that shadows it, a parameter that shadows it, and the
name inside both a comment and a string. Four of these change the client.

| Question | Answer |
|---|---|
| `prepareRename` success shape | `{"range": …, "placeholder": "total"}` — the placeholder is the current name, so the input pre-fills from the reply rather than from a re-read of the buffer |
| `rename` reply shape | **`{"changes": {uri: [TextEdit]}}`** — the map form. `documentChanges` never appears; parse both anyway, per house style |
| Scope, downward | Renaming the global edits the def and its one real use. The shadowing `let`, the shadowing parameter, the comment and the string are **all untouched** |
| Scope, upward | Renaming the `let` binding edits its two occurrences only; the global is untouched |
| Cross-file | Renaming an exported name returns a **two-document** `changes` map — the defining file (export list *and* def) plus the importing file's `:refer` — and the importing file has no tab |

**The one that reshapes §3.4: a refusal is a JSON-RPC error, not a null result.**

```
prepareRename on a stdlib symbol
  → error {"code": -32600, "message": "cannot rename stdlib symbol"}
prepareRename inside a comment
  → error {"code": -32600, "message": "cannot rename: no definition found for this name"}
prepareRename on a blank line
  → result null
```

§3.4 describes three outcomes and gets them right, but the *mechanism* matters
for the client: seven of the eight documented refusals arrive through
`LspError`, which `LspClient` already carries with its message intact. Only the
genuinely-nothing-there case answers `null`. So the callback needs all three
arms — range, null, and error-with-message — and the error arm is the common
one.

**And one upstream inconsistency, benign but worth pinning.**
**`prepareRename` does not consult lexical context.** It resolves the *word* at
the position against the symbol table, so with the caret on `total` inside
either a comment or a string literal:

- `prepareRename` returns a range **inside the comment or string** and reports
  it as renameable, with placeholder `total`;
- `references`, `documentHighlight` and `rename` all correctly resolve to the
  **global**, and the rename leaves both decoys byte-identical.

So the edit is right and only the *preview range* is wrong. Worth being precise
about the boundary: only a position whose word matches a known symbol behaves
this way. A caret on genuinely empty space answers `null`, and a caret on a
comment word that matches nothing errors with "no definition found" — which is
why an early probe on the middle of a comment looked like a clean refusal and
the quirk was nearly missed.

The consequence is cosmetic — an input box appearing over text that will not
change — and the mitigation is free: T3's occurrence highlight is already
painting the real occurrences when the input opens, so the user can see what
will actually change. Not worked around client-side; three smoke tests pin it
(the quirk itself, plus the comment and string renames landing on the global),
so a change upstream is noticed rather than silently altering behaviour.

**Not covered: the macro-introduced-binder refusal.** The guide documents
`cannot rename a macro-introduced binding`, but a `defmacro` whose expansion
introduces a `let` binder produced the generic "no definition found for this
name" instead, so there is no fixture that reaches that message on `v0.42.0`.
The refusal *path* is covered by the stdlib case, which returns its documented
message verbatim; the macro-specific wording is not, and is recorded here
rather than approximated by a test that asserts something else.

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
- **Match Monaco's suppression rules** rather than inventing new ones. Read out
  of `web/node_modules/monaco-editor/esm/vs/editor/common/model/guidesTextModelPart.js`
  (`getLinesBracketGuides`) rather than inferred from behaviour:

  | Question | Monaco's answer | In Trowel |
  |---|---|---|
  | Does a pair entirely on one line get a segment? | **Yes.** `includeSingleLinePairs` is a hardcoded `true`, and the segment runs from the opener's **end** column to the closer's column — between the brackets, not under them | matched |
  | What happens at the top level? | `activeBracketPairRange` is `undefined`, and with `bracketPairsHorizontal: 'active'` no horizontal guide is emitted at all | matched — nothing is painted |
  | Which pair is active when several contain the caret? | `findLast` over the containing pairs — the **innermost** | matched, by scanning outward from the caret and stopping at the first unmatched opener |
  | Where does a multi-line pair's segment sit? | On the closing line, from `min(openerColumn, closerColumn)` to the closer | matched |
  | Is a caret *on* a bracket inside it? | No — `Range.strictContainsPosition` | **not matched**; the backward scan treats a caret immediately after an opener as inside. The difference shows only when the caret sits exactly on a bracket, where Trowel draws the pair Monaco would skip. Drawing something for a caret on a bracket is the more useful answer, and it is the one Scintilla users expect from `SCI_BRACEHIGHLIGHT` |

  The vertical guide (§4.4) took one correction from the same source. Monaco
  emits a guide **on** each line from the opener's through the one before the
  closer, not in the gap between them. A gap-based spine has zero height
  whenever the closer sits on the very next line — which is the overwhelmingly
  common lisp shape — so it would vanish exactly where it is most wanted.
- **Preference + theme.** `editor/bracketPairGuides`, default on, next to the
  existing `editor/rainbowBrackets` checkbox in `PreferencesView` — and it should
  follow rainbow brackets: with rainbow *off*, brackets fall back to the flat
  `Delim` / `CurlyInfix` styles and there is no depth color, so either fall back
  to one neutral color or hide the guide. Pick one and say which.

  **Picked: fall back to a neutral colour** — `STYLE_INDENTGUIDE`, read back off
  Scintilla like the depth colours are. Hiding the guide would tie two unrelated
  preferences together: rainbow brackets is about *colour*, and the guide is
  about *extent* — how far the expression you are inside reaches. That question
  is worth answering in one colour, and someone who turns rainbow off has said
  nothing about wanting to lose it.

  No `bracketPairGuide` theme key was added. The colour is read from the pair's
  own style with `SCI_STYLEGETFORE`, so there is exactly one place a depth
  colour is defined and the guide cannot drift from the parens it belongs to.
  A separate key would be a second source of truth for the dimmer-guide case,
  which nobody has asked for yet.

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

### 5.3.1 Measured for T1 — including one wrong conclusion, corrected

> **Correction.** This section first reported that *type annotations* suppress
> recording, on the theory that an annotated `defn` compiles and compiled
> frames record nothing. **That was wrong**, and §5.3.1's original table and
> §5.5's "stop" recommendation were both built on it. The real cause is source
> layout, established below. The error is left visible rather than quietly
> rewritten because the false version shipped in code comments and a test, and
> anyone who read those needs to be able to find the retraction.

Run against the staged `v0.42.0` binary.

| Fixture | exit | steps | peak depth |
|---|---|---|---|
| 50k `while` loop in `main` | 0 | 100,004 (1.44 MB) | 1 |
| `fib 12`, annotated, one form per line | **144** | **2** | 1 |
| `fib 12`, **unannotated**, one form per line | 144 | **2** | 1 |
| `fib 12`, annotated, **spread across lines** | 144 | **1395** | **12** |
| top-level work, no `main` | 0 | 0 | 0 |
| a file that does not compile | 1 | 0 | 0 |

**Annotations make no difference** — annotated and unannotated `fib` record
identically. **Source layout makes all of it.** The same program, same types,
differing only in where the newlines fall, records 2 steps at depth 1 or 1395
at depth 12.

The cause is in `eval.c`: the recorder drives the debugger with
`turi_debug_resume_step_in`, whose stop predicate is line-granular — it stops
at "the next node on a *different source line*". A form written on one line
pauses once; every node inside it shares that line, so the interpreter never
stops again and the whole call runs unrecorded. Depth never rises because
`turi_dbg_push` happens during that unrecorded stretch. Dumping a recording
shows it directly: a recursive `addup 50` yields `sites=2` with one STEP inside
`addup` carrying `n=50`, and then nothing.

**This is fixed upstream, and Trowel is now pinned to the fix.** Turmeric
`e7140c97c` ("tracer: record one step per expression, not per source line")
adds `DBG_STEP_NODE` / `turi_debug_resume_step_node` and drives the recorder
with it; it ships in **`v0.42.1`**, which §2.1 bumped to. Its own commit
message reports the same finding independently: *"the same loop recorded 3
steps on one line and 23 broken across four. Per expression, both record 58."*
Interactive `tur debug` stepping stays line-granular, which is what a human
drives and what DAP speaks.

Measured on `v0.42.1`, the two spellings now agree exactly:

| Fixture | v0.42.0 (per line) | v0.42.1 (per expression) |
|---|---|---|
| `trace_dense.tur` | 2 steps, depth 1 | **1591 steps, 177 enters, depth 10** |
| `trace_spread.tur` | 531 steps, depth 10 | **1591 steps, 177 enters, depth 10** |
| `trace_loop.tur` | 404 steps | 1408 steps (~3.5x, the multiplier the commit predicts) |

Identical step, enter, pop and change counts for the dense and spread
spellings — they differ by one byte, which is the filename in the site table.
Layout no longer affects fidelity at all.

Three consequences Trowel had to carry, and one it already did:

- **The summary line format changed** — `trace: N steps (per expression), …`.
  Trowel's parser would have returned `parsed=false` and reported "could not
  run `tur trace`" on the first pin bump past `v0.42.0`. `ParseSummary` now
  accepts both spellings and records the granularity, because *what a low step
  count means* depends on it.
- **The default step cap moved**, 200k → **1M** native (250k browser), since a
  cap bounds the recording and holding the number would have cut every
  recording's reach by the granularity multiplier. At ~17.5 bytes/step that is
  ~17 MB in Trowel's address space at the cap — unremarkable for a native app,
  worth knowing before a timeline holds one.
- **Format v2**: a site now carries `col_end` and the header a granularity
  flag. v1 recordings still read back with `col_end 0`, so a recording made by
  the currently pinned binary stays readable.
- **`tur dap` maps replay steps back onto lines** for
  `stepIn`/`next`/`stepBack`/`reverseNext`, because an editor draws a line
  marker and four keypresses that leave it in place read as a hung debugger.
  That is T3's stepping semantics, decided upstream and for the right reason.

**The fourth constraint the plan missed still stands, and is independent of all
of the above:**

**The fourth constraint: the exit code is unusable as a success signal.**
`tur trace` propagates the traced program's own return value. The annotated
fixture exits **144** — which is `(fib 12)`, not an error. So across the four
cases the exit code takes the values 0, 144, 0 and 1, and it distinguishes
nothing: 0 means both "recorded fine" and "no main", while non-zero means both
"did not compile" and "your program returned that".

Everything is therefore classified from the **text**: the `trace: …` summary
line is parsed, and an `error` line on the stream is checked *first* — because
a broken file still emits a summary and would otherwise be reported identically
to the no-main case. That ordering is the single most load-bearing line in
`TraceRunner::onFinished`, and `test_a_file_that_does_not_compile_reports_the_error`
exists to pin it.

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
| **T3** — **built** | `"replay": true` launch; wire `stepBack` / `reverseContinue` / `reverseNext` to controls; the gutter follows the cursor | Stepping backward into a returned frame shows that frame's values |
| **T4** — **blocked**, §5.5.1–2 | The timeline strip: a slider over `[0, steps)`, first / step-back / step-forward / last, `file:line` for the cursor. Coalesce seeks — a drag issues one seek at a time and remembers only the most recent target | Scrubbing a recursive fixture end to end moves the gutter monotonically and never exceeds peak depth |
| **T5** — **blocked**, §5.5.1–2 | Console replay: the REPL pane shows what the program had printed by the cursor's step, and puts the user's own transcript back on close | Scrubbing backwards rewinds the transcript |
| **T6** — **blocked**, §5.6 and §5.5.1–2 | A **call-depth ribbon** under the slider (§5.6) | Recursion shape is visible at a glance and the ribbon never disagrees with the frame count |

**Read [§5.5.2](#552-the-precedent--what-try-turmeric-actually-does) before
starting any of T4–T6.** Try Turmeric has already built this once; the seek
loop, the coalescing, the ribbon's data source and the last-step output case
are all settled there.

T1 is worth shipping alone: it is an afternoon, it costs nothing to throw away,
and it is what answers §5.3's three questions with real fixtures before any UI
depends on the answers.

**T1 has shipped. Where that leaves T3–T6:**

> **Retracted.** This section previously recommended *not* starting T3–T6, on
> the grounds that the tracer records almost nothing for typed code and §8's
> stop-condition had been met. That rested on the misattribution corrected in
> §5.3.1. There was no upstream gap to file, and §8's stop-condition was never
> actually met.

§8's risk reads "if the answer is 'four steps for anything anyone writes', the
honest outcome is to ship T1 and stop." The answer is **not** that. Ordinarily
formatted code records richly — `fib 12` spread across lines gives 1395 steps
at depth 12 on the pinned binary, and Turmeric `e7140c97c` removes the layout
sensitivity entirely. Try Turmeric's own measured figure, "65 steps both ways,
peak depth 7", is the same recorder on ordinary source.

**So T3–T6 are not blocked**, and the sequencing the plan already set stands
unchanged: T2 first (`debugger-support.md` phases 1–3), because replay is that
same client with three extra requests and a different `launch` argument.

**The one prerequisite is now met.** `v0.42.1` carries `e7140c97c` and Trowel
is pinned to it (§2.1), so recordings are per expression and no longer depend
on how the user punctuates their source. T3–T6 are gated on **T2 alone** —
`debugger-support.md` phases 1–3 — exactly as this plan originally sequenced
them, and for the original reason: replay is that same client with three extra
requests and a different `launch` argument.

### 5.5.1 T3 built; T4–T6 measured and blocked upstream

> **T3 has shipped.** `launch` with `"replay": true`, the reverse-execution
> controls, and the gutter following the cursor. **T4, T5 and T6 cannot be
> built over DAP as it stands** — established by driving the staged `v0.42.1`
> binary, not by reading the source. What each needs upstream is below.

The transcript that settles it, against
`build/macos-release/trowel.app/Contents/Resources/turmeric/tur`:

```
CAPS: {"supportsConditionalBreakpoints":true, "supportsConfigurationDoneRequest":true,
       "supportsEvaluateForHovers":true, "supportsReverseContinue":true,
       "supportsStepBack":true, "supportsTerminateRequest":true}
FRAMES (entry):     main  trace_loop.tur:3
AFTER 3x stepIn:    main  trace_loop.tur:6
AFTER stepBack:     main  trace_loop.tur:5
VARS:               [{"name":"i","value":"0","variablesReference":0}]
EVALUATE:  false  "cannot evaluate in a recording -- relaunch without \"replay\""
GOTO:      false  "not supported in a recording"
GOTOTARGETS: false "not supported in a recording"
SEEK:      false  "not supported in a recording"
REVERSENEXT: true
```

`dap_replay_session` in `../turmeric/src/turi/dap.c` handles exactly
`continue`, `reverseContinue`, `stepIn`, `stepBack`, `next`, `reverseNext`,
`stepOut`, `pause`, `disconnect`/`terminate`, plus the common set
(`stackTrace`, `scopes`, `variables`, `evaluate`, `setBreakpoints`). Everything
else gets "not supported in a recording".

**T4 — the timeline strip — needs two things DAP does not carry.**

1. **A seek.** A slider over `[0, steps)` needs "put the cursor at step N".
   There is no such request: `goto` and `gotoTargets` are both refused above.
   `turi_trace_replay_seek` exists and is exactly right, but nothing exposes
   it. Approximating a seek with repeated `stepBack` is precisely the trap the
   tracing guide warns about — seeking once per candidate turns a scan of an
   80k recording from 0.02 s into not finishing.
2. **The step count.** `turi_trace_replay_steps` is internal. Without it the
   slider has no range, and a slider whose maximum is a guess is worse than no
   slider.

**T5 — console replay — is blocked for a subtler reason.** `output` events are
append-only: `dap_replay_flush_output` returns early whenever the transcript is
no longer than what it has already sent, so a `stepBack` emits *nothing*. The
client therefore cannot learn where to truncate, and T5's gate — "scrubbing
backwards rewinds the transcript" — is unreachable from this end. This is not a
Trowel bug to fix; the adapter would have to send a length or a replace.

**T6 — the depth ribbon — is blocked as §5.6 already predicted**, and the spike
confirms it: no `depth_at`, no custom request, nothing.

**What would unblock all three**, in one small additive change upstream — a
custom request in the replay loop, since that loop already has the cursor and
the reader in hand:

```
replayInfo   → {"steps": N, "index": i, "depth": d}
replaySeek   → {"index": N}                      (turi_trace_replay_seek)
replayDepths → {"depths": [...]}  downsampled    (turi_trace_replay_depth_at)
```

plus either a `replaceOutput` event or a length on the existing `output` event
so a backwards seek can rewind the transcript. `lsp-support.md` phase 2 is the
precedent for the sequencing: land it upstream, cut a release, bump the pin.

> **Superseded in part.** The sketch above is what the branch implements, and
> §5.5.2 corrects two thirds of it against the implementation that already
> exists: `replayDepths` should be a batched *sites* request (depth and
> position together), and the final step's transcript needs the equivalent of
> `turi_wasm_trace_output_full` rather than the fixture edit the branch
> currently carries.

> **Written, on an upstream branch.** `../turmeric` branch `dap-replay-seek`
> (worktree `~/Projects/turmeric/dap-replay-seek`, commit `de6c340bc`) adds
> exactly that, advertised as `supportsTurmericReplayTimeline`:
>
> | Request | Arguments | Body |
> | --- | --- | --- |
> | `replayInfo` | — | `{"steps", "index", "depth", "outputLength"}` |
> | `replaySeek` | `{"index"}` | `{"index": actual}`, then a `stopped` event |
> | `replaySites` | `{"indices"}` **or** `{"buckets"}` | `{"steps", "sites": [{"index","file","line","depth"}, …]}` |
>
> plus a **`replayOutput`** event carrying the whole transcript whenever a
> backwards seek shortens it. `tests/run-dap.sh` covers it at 61 assertions, up
> from 32; ctest 119/119.
>
> The first commit (`de6c340bc`) was derived without reading the implementation
> that already exists and got two things wrong; `51e6b8ada` corrects both
> against it. §5.5.2 records what the precedent shows and what changed.

### 5.5.2 The precedent — what Try Turmeric actually does

This should have come first. Try Turmeric is the working implementation of the
timeline, this plan names it as the reference throughout (§5.4, §5.6), and
§5.5.1 above was nevertheless derived from `dap.c` and a protocol spike alone.
The cost was not abstract: it produced a design that diverges from the
precedent in three places, and it rediscovered a bug upstream had already
fixed — then worked around it in a test fixture.

**Ground truth**, in `../turmeric`:

| What | Where |
| --- | --- |
| The timeline UI and its seek loop | `web/main.js` §"T3: the time-travel timeline" (~5425–5860) |
| The worker side of every trace call | `web/public/eval-worker.js`, `trace-run` / `trace-seek` / `trace-site-at` / `trace-find-line` |
| The C exports those bridge to | `src/web/wasm_glue.c`, `turi_wasm_trace_*` |

**The shape of it.** `traceSeek(index)` sends one `trace-seek` message; the
worker calls `_turi_wasm_trace_seek` then `_turi_wasm_trace_state`, and that
single call returns everything:

```json
{"index": i, "steps": n,
 "frames": [{"fn":…, "file":…, "line":…, "col":…, "endCol":…, "locals":[…]}],
 "output": "the whole transcript at the cursor"}
```

`traceRender` then replaces the console outright —
`traceRenderOutput(state.output)`. **The browser console rewinds because every
seek already carries the full transcript.** Rewinding was never implemented
there; it is what falls out of returning whole state instead of deltas. That is
the answer to "why was this hard for Trowel and free for the page": the page is
in-process with the reader, and DAP is a push protocol whose output events are
append-only deltas with no request meaning "what is the transcript now".

Three corrections follow, all of them things §5.5.1 got wrong. **The first two
are now fixed on the branch** (`51e6b8ada`); the third is accepted.

1. ~~**The last-step output problem has a workaround where upstream has a
   fix.**~~ **Fixed.** `_turi_wasm_trace_output_full()` (`wasm_glue.c:1116`)
   walks the recording and concatenates *every* OUTPUT record, ignoring the
   cursor; `traceSeek` asks for it when `target === steps - 1`, with the
   comment: *"a program whose final act is a println drains it after the final
   STEP"* and an empty console at the end of a run that printed *"reads as a
   broken timeline rather than a precise one"*.

   The branch hit exactly this — `replayInfo` reported `outputLength: 0` at step
   24020 of 24021 — and had resolved it by **editing the fixture to print
   earlier**, a test working around a product bug. `dap_replay_output_full` is
   now that function for the DAP side, the fixture is back to its original
   form, and the driver asserts the final `println` is visible: the test proves
   the fix rather than dodging the bug.

2. ~~**`replayDepths` is the wrong shape.**~~ **Fixed — it is `replaySites`
   now.** The precedent's ribbon data source is `trace-site-at` over a **batch
   of indices**, returning `{file, line, depth}` per index (`wasm_glue.c:1052`)
   — depth *and* position together, in one round trip, explicitly documented as
   "deliberately NOT a seek". T4 needs `file:line` for the cursor anyway and T6
   needs depth, so one request serves both; a depths-only array would have made
   a scrubber ask twice over the same steps.

   `replaySites` takes `{"indices": [...]}` for specific steps or
   `{"buckets": N}` for the whole recording downsampled. Max-per-bucket
   survived as the right reduction, and a bucket now also reports the site of
   the step where its maximum occurred — so clicking a ribbon spike lands where
   the spike is.

3. **A scrub tick costs one round trip in the browser and five over DAP.**
   `trace_state` returns index, steps, frames *and* locals *and* output
   together. The branch's flow is `replaySeek` → `stopped` → `stackTrace` →
   `scopes` → `variables`, plus an output event. That is inherent to DAP —
   clients expect the standard stop flow and Trowel's panes are already wired to
   it — so it is a divergence to accept rather than fix. But it makes T4's
   "coalesce seeks, remember only the most recent target" **load-bearing rather
   than a nicety**, and it is worth knowing that `traceSeek`'s
   `seekPending`/`seekQueued` pair is exactly that coalescing, already written.

**One thing the precedent confirms rather than corrects:** Try Turmeric still
has no depth ribbon. `trace-site-at` is plumbed through the worker and
`main.js` handles the `trace-sites` reply, but nothing sends it — the comment
naming "the depth ribbon" as its caller describes an anticipated one. §5.6 is
right that T6 is unbuilt anywhere; it is now also clear what it should be built
*on*.

**Sequencing, unchanged and still upstream's:** (1) and (2) are fixed; what
remains is merge, cut a release, bump `TROWEL_TURMERIC_VERSION`, and only then
build T4–T6 against a pinned binary that ships the requests. Building against
an unmerged branch would pin Trowel to a `tur` no user has.

**When T4–T6 do start, the Trowel side already has the shapes it needs.**
`DebugSession` should read `supportsTurmericReplayTimeline` off the
`initialize` response rather than inferring it from the pin — `ResolveTurBinary()`
honours a QSettings override and PATH, so the `tur` on the other end of the
pipe is not necessarily the one `TROWEL_TURMERIC_VERSION` names — and the
console rewind is `DebuggerView` gaining a `setOutput()` beside its
`appendOutput()`, driven by a `replayOutput` → `outputReplaced` signal. Neither
is written yet.

**Until then T4–T6 are not started, deliberately.** The reverse-execution
controls T3 shipped are the whole of the time-travel UI that this adapter can
honestly serve, and a slider that cannot seek would be a lie in the shape of a
widget.

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

- ~~**The tracer records almost nothing for typed code.**~~ **Retired — the
  premise was false** (§5.3.1). Annotations are irrelevant; the effect was
  line-granular stepping collapsing densely written source, and Turmeric
  `e7140c97c` records per expression. What T1 actually demonstrated is the
  value of the phase itself: it was built to test this risk against real
  fixtures before T3–T6 depended on the answer, and it caught a wrong answer
  that had already reached code comments and a test.
- **Version skew in the trace summary line.** The format gained a granularity
  field, and a parser that knew only one spelling would report a perfectly good
  binary as "could not run `tur trace`". `ParseSummary` accepts both. Any
  future field is the same hazard — parse leniently, and never key a UI state
  off a failed parse alone.
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

---

## 9. As built — deviations and decisions

Recorded rather than silently absorbed, matching `lsp-navigation.md` §14.

### Track R

1. **The references list reuses the outline's `SCI_USERLISTSHOW` widget**, which
   §6 explicitly said was *not* settled by the outline having landed first — a
   references panel spans files and "probably wants the palette §6.2 defers".
   It still might. But §6.2's "do not build it speculatively" is the stronger
   instruction, and a user list with `file:line  text` rows works: it filters by
   prefix, it is already themed, and it costs nothing. If it proves too small on
   a real workspace, that is the evidence §6.2 was asking for before building a
   palette. Rows deliberately read **References**, never *All references* — an
   oversized workspace silently returns a short list.

2. **Rename's confirmation prompt lives in the UI slot, not in
   `applyWorkspaceEdit`.** The threshold check (§3.4, 20 documents) is policy;
   applying is mechanism. Splitting them is what lets the control API drive a
   cross-file rename in a smoke test without a modal dialog blocking the
   socket's event loop.

3. **The rename input is the inline widget, not the dialog fallback.** §3.4
   permits shipping the dialog first. It was not, because a modal dialog would
   *hang* every test that reached it rather than failing it — and rename is the
   one operation in this editor least able to afford untested paths. The
   inline `QLineEdit` dismisses on Escape and on focus loss.

4. **`applyWorkspaceEdit` also refuses to edit the bundled stdlib.** Not in
   §3.4, but it follows from the same all-or-nothing rule: those files are
   read-only (`lsp-navigation.md` §5.3), so a rename reaching one could only
   ever half-apply. The server refuses stdlib renames anyway; this is the
   client-side backstop.

5. **`editor.begin_rename` was added to the control API**, beyond §3.5's four
   handlers. Same reason `nav.goto_definition` exists: prepareRename is
   asynchronous and its refusal arm can resolve within one event-loop turn, so
   a test that triggers then waits has no race-free moment to observe. It
   connects before triggering.

6. **The macro-introduced-binder refusal is not covered by a test.** See the
   end of §3.1.1 — no fixture reaches that message on `v0.42.0`.

### Track G

7. **`Range.strictContainsPosition` is not matched.** Monaco does not consider a
   caret sitting exactly on a bracket to be inside that pair; Trowel does. The
   full rule table is in §4.3. Drawing something for a caret on a bracket is
   what a Scintilla user expects from `SCI_BRACEHIGHLIGHT`.

8. **The rainbow-off fallback is a neutral colour, not a hidden guide**, and no
   `bracketPairGuide` theme key was added. Both decisions and their reasons are
   written into §4.3 where the plan asked for them.

9. **G2 landed as its own overlay widget** because minimap phase 4 has not
   shipped. §4.4 says to reuse the minimap's overlay if it lands first; it did
   not, so this is the first one. If the minimap arrives, fold
   `BracketGuideOverlay` into its pass rather than stacking a second
   transparent widget over the same viewport — the note is on the forward
   declaration in `editor_view.h` so whoever builds the minimap trips over it.

10. **G2 grew a third part the plan did not ask for: a gutter bar.** §4.4
    designed one vertical element, the spine in the opener's own column. In
    use that turned out to answer the wrong question — the spine tells you
    *how deep* the form is indented, and finding it means first finding the
    opener. A 2px rule right-aligned against the margins, spanning the same
    rows in the same depth colour, answers "which lines is my expression in?"
    from a fixed x at the edge of the window. Try Turmeric draws exactly this,
    which is where the shape comes from.

    Two consequences worth knowing. It is **drawn for single-line pairs**,
    where the spine is deliberately not: the bar marks the rows a form
    occupies, and one row is a valid answer, whereas a spine between two points
    on the same row has zero length. And it is **clipped to the viewport**,
    because a form taller than the window would otherwise hand Qt a rect
    reaching past both edges — the spine got away without this by being a
    `drawLine`, which Qt clips itself.

    Read back through `lsp.decorations` as `bracket_guide_gutter`, the same
    shape as `bracket_guide_vertical`, so the tests assert on the painted rect
    rather than on the pair that produced it.

### Track T

10. **Track T stops after T1 — because T2 is a different plan's work**, not
    because anything is blocked. The earlier "stopped on purpose, the tracer
    cannot see typed code" rationale was wrong and is retracted in §5.3.1 and
    §5.5.

11. **`trace_typed.tur` was renamed `trace_dense.tur`.** Its old name asserted
    the wrong cause. It is now the dense half of a pair with
    `trace_spread.tur`: same program, same annotations, different line
    layout — 2 steps versus 531 under `v0.42.0`, and identical under
    `v0.42.1`. `trace_trivial.tur` was added to keep a genuine
    short-recording case, since dense source is no longer one.

12. **`ParseSummary` accepts both summary formats.** The granularity field is
    optional in the regex and captured when present. This was not
    forward-thinking so much as necessary: without it the `v0.42.1` bump would
    have made every trace report "could not run `tur trace`", because a failed
    parse was the only signal the outcome classifier had.

11. **`TraceOutcome` is an enumeration, not a step count.** The UI speaks from
    the outcome because "2 steps" without a reason is precisely what makes a
    user conclude the tracer is broken (§8). Four outcomes, four distinct
    sentences, pinned pairwise-distinct by a test.

### Still open, by design

- **T3–T6** (§5.5) — gated on T2 (`debugger-support.md` phases 1–3) alone; the
  pin prerequisite is met (§2.1).
- **T6's depth ribbon** (§5.6) — the "decide before T4" call is moot while T4
  is not being built, but the decision itself is unchanged: a DAP `custom`
  request upstream, or a ribbon-only `.turtrace` header reader.
- **A references palette** (§6.2) — build only if the user list proves too blunt.
- **`--rename-exports` as a preference** (§3.4) — the refusal is surfaced
  verbatim, so a user hitting it is told what the flag is; wiring it to a
  checkbox that restarts the server is a small, separate change.
