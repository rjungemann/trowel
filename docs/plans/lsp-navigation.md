# Navigation: outline, go-to-definition, occurrences — plan

> **Status:** Proposed
> **Related:** [`lsp-support.md`](lsp-support.md) (phase 1 landed; this is the
> follow-up it deferred), [`minimap.md`](minimap.md) (owns the minimap; see §2),
> [`PLAN.md`](PLAN.md) §11
> **Upstream counterpart:** `../turmeric/docs/upcoming/try-turmeric-navigation-and-minimap-plan.md`
> **Reference implementation (design only):** `~/Projects/c2mir-playground/c2mp/vite-wasm`

---

## 0. Summary

The Try Turmeric plan brings four things to the web playground: a minimap, hover,
go-to-definition, and an outline. This is the Trowel half of the same request.
The shape is different in three ways that determine everything below.

**One: the minimap is already planned, in detail, and is not re-planned here.**
[`minimap.md`](minimap.md) is 490 lines of Scintilla-specific design — strip
cache, `StyleSink` refactor, off-thread rendering, proportional slide. Nothing in
the web plan improves on it, because the web plan's entire minimap section is
"flip `minimap.enabled` to true and add theme colors": Monaco ships a minimap and
Scintilla does not. The only thing this plan owes `minimap.md` is one new
decoration lane (§2).

**Two: the server Trowel talks to is a pinned binary, so T0 is a version bump.**
Trowel was six releases behind at `v0.33.2`; it is now pinned to **`v0.39.0`**
(`CMakeLists.txt:119`), which was done ahead of this plan and is verified in
§4.1. That already picked up `signatureHelpProvider` and
`documentFormattingProvider` — both of which [`lsp-support.md`](lsp-support.md)
§"Phase 2" still lists as *Not started*, because they landed upstream after that
list was written.

It did **not** pick up the two things this plan most needs. Try Turmeric's **M5**
adds real symbol kinds and `textDocument/documentHighlight`, and neither is in
`v0.39.0` — confirmed against the staged binary in §4.1. Per the sequencing the
author set, **Try Turmeric completes before this plan starts**, so T0 is a
*second* bump to whichever release carries M5. That bump is load-bearing rather
than housekeeping: it is what turns T2's kind column and T3 entirely from "work
around the server" into "call the server".

**Three: go-to-definition is nearly free here and was the web plan's hardest
phase.** Try Turmeric's M4 spends most of its length on a problem Trowel does not
have — the stdlib lives in an Emscripten MEMFS the page cannot read, so M4 needs a
new WASM export (`turi_wasm_read_file`), a worker message type, and invented
read-only virtual buffers. In Trowel the stdlib is a real directory on a real
disk, pinned next to the resolved binary by `LspManager` exactly as `ReplSession`
pins it (`src/lsp/lsp_manager.cpp:127-128`). `on_definition`
(`../turmeric/src/lsp/lsp.c:898`) returns `file:///…/stdlib/list.tur` and
`MainWindow::openPath()` already opens that. What is left is an LSP request, a
menu action, read-only-ness, and a jump-back stack.

What genuinely does not exist in Trowel today, verified by grep over `src/`:

| Capability | Trowel | Try Turmeric |
|---|---|---|
| Diagnostics, completion, hover | shipping | shipping |
| `textDocument/definition` | **no request is ever sent** | provider registered, gives up at the stdlib |
| `textDocument/documentSymbol` | **no request is ever sent** | provider registered, no UI reads it |
| Occurrence highlight | **nothing, not even the textual fallback** | `occurrencesHighlight: true`, unbacked |
| Navigation history | **none** | none |

Monaco gave the web plan a free `editor.action.quickOutline` and a free
Cmd-click. Scintilla gives neither. Trowel has to build the surface — which is
why §5 spends its length on the outline UI and the web plan spends one paragraph
on it.

**c2mp is worth reading and not worth porting**, for the same reason the web plan
gives: it hand-rolls a C symbol scanner and a canvas minimap to reach a floor
`tur lsp` and Scintilla are already above. What it contributes is decisions, and
this plan adopts the same ones (§8).

---

## 1. What exists (repo facts)

| Thing | State | Anchor |
|---|---|---|
| `LspManager`, one server for the app, URI-keyed doc registry | shipping | `src/lsp/lsp_manager.h:33` |
| `LspClient::request(method, params, reply, timeoutMs)` | shipping, generic | `src/lsp/lsp_client.h` |
| Byte-offset position conversion, one switch | shipping | `src/lsp/lsp_position.{h,cpp}` |
| Diagnostics → squiggle + margin marker | shipping | `src/editor/editor_view.cpp:132-139` |
| Completion via `autoCShow` | shipping | `src/editor/editor_view.cpp:405-410` |
| Hover via `dwellStart` → `callTipShow` | shipping | `src/editor/editor_view.cpp:69-74,426` |
| Diagnostic indicator slots 8, 9; markers 0, 1 | shipping | `src/editor/editor_view.h:22-27` |
| Symbol margin, 12px, enabled | shipping | `src/editor/editor_view.cpp:126-127` |
| Side bar: `addSideBarAction(QAction*)` | shipping | `src/app/main_window.cpp:333` |
| `lsp.*` control commands (6 of them) | shipping | `src/control/control_handlers.cpp:879-884` |
| `tests/smoke/test_lsp.py`, 12 tests | shipping | — |
| Turmeric pin `v0.39.0` + three per-arch SHA-256s | `CMakeLists.txt:119,128,133,136` | — |

Server side as pinned (`v0.39.0`), probed live from the staged binary and read
from `../turmeric/doc-stuff/src/lsp/lsp.c:695-720`:
advertises `positionEncoding: utf-8`, `hoverProvider`, `definitionProvider`,
`documentSymbolProvider`, `workspaceSymbolProvider`, `documentFormattingProvider`,
`signatureHelpProvider`, `completionProvider`. It does **not** advertise
`documentHighlightProvider`, and `lsp_symbol_kind` (`lsp.c:962`) is still the
two-line function the web plan complains about:

```c
static int lsp_symbol_kind(const LspSymbol *sym) {
    return (strncmp(sym->type_str, "(fn", 3) == 0) ? 12 : 13;
}
```

A `defstruct`, a `defmacro`, a `definstance` and a `def` are indistinguishable.
Both of those are Try Turmeric M5's job, upstream, before this plan begins.

---

## 2. The minimap — what this plan owes `minimap.md`, and nothing more

[`minimap.md`](minimap.md) stands as written. Its phase 4 already lists
diagnostics, selection and caret-line overlays "painted in `paintEvent` over the
cached image, never baked into it". This plan adds **one row to that list**:

- **Occurrences.** When T3 lands, `EditorView` holds a set of occurrence ranges
  for the symbol under the caret. Draw them into the same overlay pass, as a
  low-alpha wash rather than a fill — c2mp's `MARK_ALPHA` comment
  (`minimap.js:57-60`) is the reasoning and it applies unchanged: *"the token
  colours underneath have to stay legible, and a mark that hides the code it
  marks defeats the purpose."*

And **one ordering constraint**: an occurrence lane fed by Monaco's or
Scintilla's word-based matching is a *wrong answer painted down the whole file* —
`total` inside `subtotal`, inside a comment, inside a string. That is exactly the
line c2mp draws (`symbols.js:717-730`). So either T3 lands before minimap phase
4, or minimap phase 4 ships without the occurrence lane. It must not ship with a
textual one.

Everything else about the minimap — geometry, cache, threading, prefs, theme
keys — is `minimap.md`'s and is not restated here. If the two documents ever
disagree about the minimap, `minimap.md` wins.

---

## 3. Phases

| Phase | Scope | Depends on |
|---|---|---|
| **T0a** | Bump to `v0.39.0`; verify capabilities | — (**done**, §4.1) |
| **T0b** | Bump again to the M5 release | Try Turmeric M5 shipped upstream |
| **T1** | Go-to-definition, read-only stdlib tabs, navigation history | T0a + the §4.2 measurement |
| **T2** | Outline: `documentSymbol` request + a user-list surface | T0a (kind labels need T0b) |
| **T3** | Occurrence highlight on `documentHighlight` | **T0b** |
| **T4** | Hover completeness — *conditional, may be empty* | T0b |

T1, T2 and T3 are independent of each other and independently shippable. None of
them touches `../turmeric`; if any does, the plan has gone wrong (§9).

T1 and T2 can begin now. T3 and T4 cannot, and the honest reading is that this
plan is **gated on Try Turmeric finishing**, exactly as the author sequenced it —
splitting T0 into a landed half and an owed half is bookkeeping, not a way around
that gate.

---

## 4. T0 — consume the upstream work

Not a spike. The Try Turmeric plan opens with a measurement phase (its M0)
because the web client and the server ship in one bundle and the author is
holding both. Trowel talks to a *pinned binary*, so the same questions have
different answers depending on which binary, and measuring the old pin would be
measuring something we are about to stop using. **Bump first, then measure once.**

### 4.1 The bump — half done

**Already landed: `v0.33.2` → `v0.39.0`.** `CMakeLists.txt:119` plus the three
per-arch SHA-256s at `:128`, `:133`, `:136` (macos-arm64, linux-x86_64,
linux-aarch64), taken verbatim from the release's `sha256sums.txt`; `mise.toml`
bumped to match so the dev toolchain and the bundled one do not diverge. The
comment at `:115-118` is explicit that the version is a non-cache variable so
this stays a single edit; that was honored. Verified: `just build` clean,
`FetchContent` accepted the hash, the staged
`Resources/turmeric/tur` reports `v0.39.0`, and its `stdlib/` came with it.

That bump was worth more than this plan alone needs. Probed live from the staged
binary, it picked up:

- **`textDocument/signatureHelp`** — `lsp-support.md` phase 2 item 3, listed as
  not started; now advertised, with `(` as the trigger character. Wiring it to
  `callTipShow` is a natural companion to T2 but is **not scoped here**; note it
  in `lsp-support.md`'s Deviations rather than silently absorbing it.
- **`textDocument/documentFormattingProvider`** — item 4. `MainWindow::formatFile()`
  still blocks the UI shelling out to `tur format`. Also not scoped here, and
  also worth recording as newly-possible.

Update `lsp-support.md`'s phase 2 §Done/§Not-started to match. A plan that lies
about what the server can do is how the next reader loses an afternoon.

**The M5 bump has since landed: `v0.39.0` → `v0.42.0`.** Done under
[`editor-intelligence.md`](editor-intelligence.md) §2 — `CMakeLists.txt:119`
plus the three per-arch SHA-256s, and `mise.toml` to match. Probed live from the
staged binary, it carries both halves of M5:

- **`documentHighlightProvider: true`** is advertised. **T3 is unblocked.**
- **`lsp_symbol_kind`** (`../turmeric/src/lsp/lsp.c:1044`) is now a switch over
  `LSP_KIND_FUNCTION` / `VALUE` / `STRUCT` / `ENUM`, not the two-line `strncmp`
  quoted in §1. **T2's kind column is no longer dead weight.**

It also brought `renameProvider` (with `prepareProvider`), `referencesProvider`,
`$/cancelRequest`, and the `tur trace` recorder — none of which this plan
scopes; they are [`editor-intelligence.md`](editor-intelligence.md)'s.

Two consequences for this plan, both worth acting on before T3 starts:

- **T0b is closed.** All four phases are now buildable.
- **T3 is also §3.2 of `editor-intelligence.md`**, which was written against the
  same newly-available capability. Build it under whichever plan is picked up
  first and delete the other section; do not build it twice.

The §1 and §3 tables still describe `v0.39.0` and are stale on both counts. They
are left as written rather than rewritten in place, because the phase table's
`T0b` row is the record of why this plan was sequenced the way it was.

### 4.2 The one measurement that is genuinely native-specific

Try Turmeric's M0 will have already recorded, in its own document, whether hover
and definition answer on stdlib names. Read those answers; do not re-derive them.
What that spike **cannot** answer, because it runs against MEMFS, is:

> When `tur lsp` is spawned by `LspManager` with `TUR_STDLIB_DIR` pinned to the
> sibling stdlib (`src/lsp/lsp_manager.cpp:127-128`), does
> `textDocument/definition` on a stdlib name return a `file://` URI naming a path
> that **exists on disk and is readable**?

Measure it directly, outside the editor, the way `lsp-support.md` §Verification
item 4 already establishes:

```
printf 'Content-Length: …\r\n\r\n{…}' | \
  TUR_STDLIB_DIR=<staged>/stdlib <staged>/tur lsp
```

Record the answer **in this document**. Three outcomes:

- **A real, readable path.** T1 proceeds as written.
- **A path that does not exist** (e.g. relative, or naming the build tree of
  whoever compiled the release). T1 keeps the in-workspace half and drops the
  stdlib half; the fix is upstream and is a separate plan.
- **`null`.** `find_symbol` never saw the symbol, which is the same hole T4
  covers. T1 shrinks to cross-file navigation between open project files, which
  is still worth having.

Exit criteria: the build is green on the new pin, `just smoke` passes unchanged,
and that one answer is written down.

---

## 5. T1 — go-to-definition

### 5.1 The request

`LspManager` gains one method, shaped exactly like the two that exist
(`lsp_manager.h:61-68`) including their staleness guard, which is not optional
here — a definition reply that arrives after the user kept typing would jump them
somewhere unrelated:

```cpp
struct LspLocation { QString uri; int line = 0; int character = 0; };
using DefinitionCallback = std::function<void(const LspLocation&)>;
void requestDefinition(EditorView* view, int pos, DefinitionCallback cb);
```

The server returns a bare `Location` object, not an array and not a
`LocationLink` (`lsp.c:940-957` writes a single `{"uri":…,"range":…}`). **Parse
all three shapes anyway** — the same defensive note `lsp-support.md` §6 makes
about `CompletionItem[]` vs `CompletionList`, for the same reason: the server is
under active development and the client should not break on a conforming change.
An empty array and `null` both mean "no definition", and must be reported that
way rather than silently doing nothing.

### 5.2 Where it lands

Three cases, in `MainWindow`, because this is navigation and navigation is
window-level even though the LSP wiring is per-buffer (`lsp-support.md`
Deviation 1):

1. **Same document** — `setCursorPos` after `SCI_SCROLLCARET`; no tab change.
2. **Another file already open in this window** — `activateBuffer(indexOfPath(…))`,
   then position. `indexOfPath` exists (`main_window.h:149`).
3. **A file not open** — `openPath()`, then position. This is the stdlib case and
   also the ordinary cross-file case in a project.

A file open in a *different* window is deliberately case 3: opening a second view
is correct and `LspManager` already supports it (`lsp_manager.h:30-32` — documents
are keyed by URI, `didOpen` on first attach, `didClose` on last detach). Do not
raise the other window; a definition jump that flings focus to another display is
hostile.

### 5.3 Read-only stdlib buffers

A definition inside the bundled stdlib opens a tab whose content the user must
not edit. Detect it by prefix against the same directory `LspManager` pinned —
one accessor on `LspManager` (`QString stdlibDir() const`), not a second
independent derivation of the path, or the two will drift.

- `sci_->setReadOnly(true)` for that buffer, and `EditorView::isReadOnly()`
  reported through the control API.
- A lock affordance in `TabBar` (`src/app/tab_bar.cpp`), matching how the
  modified-dot is already drawn.
- **Excluded from recent files** (`rememberRecentFile`, `main_window.h:145`).
  "Open Recent ▸ list.tur" landing in an unwritable app-bundle file is a puzzle,
  not a feature.
- **Excluded from session restore** (`sessionState()`, `main_window.h:56`). This
  is the Trowel analogue of the web plan's `persistTabs` exclusion and the
  reasoning is sharper here: the path
  `Trowel.app/Contents/Resources/turmeric/stdlib/list.tur` is stable across
  upgrades, so a restored tab would silently present *last version's* stdlib as
  current after a `TROWEL_TURMERIC_VERSION` bump. Stale and indistinguishable
  from fresh is worse than absent.
- Closable like any other tab; reused rather than duplicated (case 2 above
  handles that for free).

Why read-only at all, since it is a real file: on macOS it lives inside a signed
app bundle and writing there invalidates the signature; on any platform the edit
is lost at the next upgrade. The honest UI is a lock, not a save error.

### 5.4 Keyboard and discoverability

Scintilla ships no navigation history and no Cmd-click-to-definition — both are
Monaco freebies the web plan gets to assume. Add, in `setupMenus()`
(`main_window.cpp`, alongside `completeAction_` at `:290` and `showDocAction_`
at `:296`):

| Action | Shortcut | Notes |
|---|---|---|
| Go to Definition | `F12` | Free on all three platforms |
| Back | `Ctrl+Alt+Left` | See conflict note |
| Forward | `Ctrl+Alt+Right` | |

`Ctrl+Alt+Left/Right` is a workspace switcher under several Linux desktops. If
that bites, fall back to VS Code's `Ctrl+Alt+-` / `Ctrl+Alt+Shift+-`. Decide once
and put it in [`docs/guides/keyboard-shortcuts.md`](../guides/keyboard-shortcuts.md),
which currently documents every binding and would otherwise go stale.

**Cmd/Ctrl+click is a follow-up, not part of T1.** Scintilla exposes click-to-act
through hotspot styles or `indicatorClick`, both of which mean painting something
over every identifier in the buffer. That is a real feature with a real cost and
it should not ride along inside a phase that otherwise adds one request and a
menu item.

The history stack itself: `QVector<{QString path; int pos;}>` on `MainWindow`,
pushed before every jump (including outline jumps in T2), capped around 20,
forward entries dropped on a new jump. Store a **path and offset, not an
`EditorView*`** — a tab can move between windows by drag and drop
(`test_drag_drop.py` covers that today), and a raw pointer in a history stack
would be a dangling pointer waiting for a Tuesday. An entry whose file is no
longer open reopens it; an entry whose file is gone is skipped, not an error.

---

## 6. T2 — the outline

`documentSymbol` is answered by the server (`lsp.c:970`) and Trowel never asks.
`LspManager` gains `requestDocumentSymbols(EditorView*, cb)` returning name,
kind, and range.

**Fill on open, never kept in sync.** c2mp's reason (`main.js:426-427`) is exact:
*"the buffer changes on every keystroke and this is looked at once in a while."*
It matters more here than there, because the Turmeric server is single-threaded
and blocking. Upstream phase 2 item 1 already landed — documents are marked dirty
and analyzed immediately before any request that reads symbols — so an outline
opened mid-keystroke describes the buffer as it is now, and there is nothing to
add on the client side.

### 6.1 The surface: `SCI_USERLISTSHOW`

Recommended for T2a, and it is a genuinely good fit rather than a shortcut:

- The list machinery is **already wired** — `autoCSetSeparator`,
  `autoCSetIgnoreCase`, the popup styling that follows the theme
  (`editor_view.cpp:405-410`). A user list is the same widget with a different
  list type and no insertion side effect; the selection arrives on
  `userListSelection` and Trowel decides what to do with it.
- **Document order is preservable** via `autoCSetOrder(SC_ORDER_CUSTOM)`. Default
  alphabetical ordering would destroy the one thing an outline is for.
- **Prefix filtering as you type** comes free.
- **"Where am I" comes free too, and better than the web plan's marker.** Try
  Turmeric marks the caret-containing entry `is-current` with a CSS class. A user
  list cannot style one row — but it can *preselect* one, with `SCI_AUTOCSELECT`.
  Opening the list already scrolled to where you are is a better answer to the
  same question than a highlight you have to hunt for.

The caret-containing rule itself is c2mp's, ported verbatim
(`symbols.js:775-795`): the name wins when the caret is actually on one,
otherwise the **smallest containing range** wins — because someone editing the
middle of a function is not on its name.

Rows read `name — kind`, one string. The kind wording is c2mp's `KIND_LABEL`
(`symbols.js:752`) and its comment is the spec: *"what you would say out loud
about the entry"* — `function`, `type`, `macro` — not the LSP enum name. This is
where T0's bump pays off: before Try Turmeric M5, every non-function is
`Variable` and the column says nothing.

**States that are not "a list":**

- No symbols → a single disabled row, `Nothing defined yet` (c2mp `main.js:435`).
- LSP unavailable or still starting (`LspManager::state()` ≠ `Ready`) → say so.
  An empty outline that implies an empty file is the failure the web plan calls
  out, and it is worse here because a Trowel user may not know a server exists.
- **Unsaved buffer** → `LspManager::kSkipUnsavedReason`, in the status bar, the
  way completion and documentation already report it (`lsp-support.md`
  Deviation 6).
- **Non-Turmeric buffer** → the action is disabled, via
  `updateEditorActionsEnabled()` (`main_window.h:156`). Trowel highlights nine
  languages (`src/editor/scanner_*.cpp`) and has a language server for one. A
  Symbols button that silently does nothing in a `.py` file is a bug report.
  Building a scanner-backed outline for the other eight is a real idea and an
  explicitly separate plan.

Reachable from the **Edit menu** with a shortcut and from the **side bar** via
`addSideBarAction` (`main_window.cpp:333`), so it sits with Complete and Show
Documentation. `Ctrl+Shift+O` — the web plan's binding, and VS Code's — is
**taken** by Open Directory (`main_window.cpp:195`). Do not move Open Directory.
Suggest `Ctrl+Shift+M`; the author's call, and whatever it is goes in
`keyboard-shortcuts.md`.

### 6.2 T2b — the palette, only if T2a proves it

If the user list turns out to be too small or prefix-only filtering too blunt on
a large file, the next step is a `QDialog` quick-open with fuzzy matching — a
palette, which Trowel has no precedent for and which would then also want to
serve `workspace/symbol` (advertised, unused) and recent files. That is a
worthwhile feature and it is a different plan. **Do not build it speculatively.**

A docked outline pane is the natural end state for a native editor and is *not*
proposed here: `DirectoryView` already occupies the sidebar-tree niche and shares
the tab stack with editors, so a dock is a layout project. The web plan rejects a
docked pane for its own reasons; Trowel rejects it for different ones, and both
should say so out loud so nobody re-proposes it as an easy win.

---

## 7. T3 — occurrence highlight

Depends on Try Turmeric M5 landing `textDocument/documentHighlight` upstream. If
it did not, **skip this phase entirely** — do not build the client-side textual
fallback the web plan offers as a consolation. That fallback exists there because
Monaco was *already* doing textual matching under
`occurrencesHighlight: true` and filtering it was strictly an improvement. Trowel
highlights nothing today, so a textual version would be a regression from
"honest" to "confidently wrong", and it would then feed the minimap's occurrence
lane (§2) with wrong data spread down the whole file.

When it is available:

- Request on caret-settle, not on every `updateUi` — reuse the existing 250 ms
  debounce shape rather than inventing a second one.
- Paint with **indicator slot 10**. Slots 8 and 9 are diagnostics
  (`editor_view.h:22-27`); 10 upward are free. Use a translucent
  `INDIC_ROUNDBOX`, not another squiggle — an occurrence is not a problem and
  must not read as one.
- Clear the previous set before painting, exactly as `clearDiagnosticDecorations`
  does (`editor_view.h:121`).
- New theme key `occurrenceHighlight`, falling back to `selectionBg` at low alpha
  so existing user theme files keep working — the same fallback discipline
  `minimap.md` §"Theme integration" establishes.
- Then, and only then, the minimap lane in §2.

---

## 8. T4 — hover completeness (conditional)

`on_hover` (`lsp.c:821`) resolves the word under the caret against
`find_symbol`, which scans only what `tur_collect_symbols` put in *this
document's* index. If stdlib names never enter that index, hover returns
`{"contents":""}` (`lsp.c:857`) and the user sees nothing on exactly the names a
newcomer types most.

**This phase is empty unless T0's reading of Try Turmeric's M0 says the hole is
real after M5.** If it is, note that Trowel's fix is *not* the web plan's fix.
Try Turmeric falls back to `web/public/doc-names.json` and `docs-pack/`, a
docstring table it already ships for its docs pane. **Trowel ships no such
table**, and inventing one — bundling a docs pack into the app and keeping it in
sync with `TROWEL_TURMERIC_VERSION` — is a larger and worse feature than fixing
the collector upstream, where one fix serves the web playground, Trowel, the VS
Code extension, and anything else that speaks to `tur lsp`.

So: if the hole survives, **the deliverable of T4 is a filed upstream issue with
the reproduction from T0's probe**, not client code. Say that plainly rather than
letting the phase quietly become a Trowel workaround.

---

## 9. What is deliberately not here

- **The minimap.** [`minimap.md`](minimap.md), except the one lane in §2.
- **Server changes.** Try Turmeric's M5 does them upstream first. If this plan
  ends up editing `../turmeric`, the sequencing assumption broke and the phase
  should stop and re-plan rather than fork the work.
- **Try Turmeric's F1** — the "Try our C Interpreter" footer link. Trowel has no
  footer. A Help-menu link is a fine idea and a one-line change that belongs to
  whoever is next in the Help menu, not to a navigation plan.
- **Semantic tokens.** Trowel's scanners are its visual identity
  (`minimap.md` §"Real styles, not a density heuristic") and the server does not
  advertise them.
- **Rename, code actions, inlay hints.** No server support.
- **`workspace/symbol`.** Advertised and unused; it wants the palette from §6.2.
- **Cmd-click to definition.** §5.4.
- **An outline for the other eight languages.** §6.1.

---

## 10. What c2mp is for

The same as in the web plan: decisions, not code. Its 862-line `symbols.js` and
443-line `minimap.js` exist because c2mir exports no symbol table and a textarea
has no minimap — both floors Trowel is already above, with `tur lsp` and with
Scintilla's per-line lexer state (`PackLexState`/`UnpackLexState`,
`src/editor/lexer_adapter.cpp`). **Do not port either file.**

Adopted here, each with its source:

| Decision | c2mp anchor | Where it lands |
|---|---|---|
| Outline built on open, not kept in sync | `main.js:426` | §6 |
| Kind labels are speech, not enum names | `symbols.js:752` | §6.1 |
| Caret entry: name wins, else smallest containing range | `symbols.js:775-795` | §6.1 |
| Occurrences are token-based; a mention is not a use | `symbols.js:717-730` | §2, §7 |
| Marks are washes, not fills | `minimap.js:57-60` | §2 |
| Diagnostics get an outboard lane; occurrences do not | `minimap.js:38-45` | §2 |
| A decoration that fails removes itself rather than degrading the editor | `minimap.js:342` | review criterion, everywhere |
| Empty state says "Nothing defined yet" | `main.js:435` | §6.1 |

The last one in the first group is worth stating as a review rule and not just a
citation: **nothing in this plan may be able to break editing.** If the outline
request times out, the definition reply is malformed, or the highlight indicator
fails to paint, the editor keeps working and the user finds out through the
status bar.

---

## 11. Control API and tests

House rule from [`smoke-tests.md`](smoke-tests.md): **tests never sleep.** Every
wait is a `wait.*` command with an explicit timeout, built on the existing
`WaitCtx` / `ArmTimeout` helpers in `src/control/control_handlers.cpp`.

New handlers, added to the dispatch table alongside the six `lsp.*` commands at
`control_handlers.cpp:879-884`:

| Command | Returns | Phase |
|---|---|---|
| `lsp.definition` | `{uri, line, character}` or null | T1 |
| `lsp.symbols` | `[{name, kind, line, character, current}]` | T2 |
| `lsp.highlights` | `[{start, end}]` | T3 |
| `nav.history` | `{back: [...], forward: [...]}` | T1 |
| `editor.is_read_only` | bool | T1 |

New `tests/smoke/test_lsp_navigation.py`:

- **T1** — definition within one file moves the caret; definition into a second
  project file opens a tab and lands on the right line; definition into the
  stdlib opens a tab that reports `is_read_only`; that tab is absent from
  `tabPaths()` after a session round-trip and absent from recent files; Back
  returns to the origin position, Forward returns to the target; Back with an
  empty stack is a no-op, not an error.
- **T2** — the list carries the buffer's own top-level names in document order;
  `current` tracks the caret through both the on-a-name and inside-a-body cases;
  the unavailable-LSP state is distinct from the empty state; the action is
  disabled in a `.py` buffer; an unsaved buffer reports `kSkipUnsavedReason`.
- **T3** — a name used three times reports three ranges; a same-spelled word
  inside a comment and inside a string is **not** among them (this is the whole
  point of the phase and the one assertion that would catch a regression back to
  textual matching); indicator 10 ranges match, read back the way
  `lsp.decorations` already reads back diagnostics (`lsp-support.md` Deviation 5
  — asserting on the model does not prove a decoration was painted).

Fixtures: `tests/smoke/fixtures/` already has `hello.tur` and `syntax_error.tur`.
Add one multi-definition file covering `def`, `defn`, `defstruct` and `defmacro`
— it serves the outline, the kind labels, and the occurrence cases at once.

---

## 12. Verification

1. `just build` — clean under `-Wall -Wextra -Wpedantic -Werror`.
2. `just smoke` — the existing 12 LSP tests must not regress across the T0 bump.
   A behavior change there is a finding about the new Turmeric release and should
   be recorded, not patched around.
3. `just run` and drive it by hand:
   - `F12` on a project-local symbol → caret moves, no tab churn.
   - `F12` on `map` → `stdlib/list.tur` opens, positioned on the definition, tab
     shows the lock, typing into it does nothing.
   - Back → previous file *and* previous position. Forward → returns.
   - Drag that tab to another window, then Back — no crash (§5.4's reason for
     storing paths).
   - Open the outline in a 300-line file → document order, kinds are meaningful
     rather than all `variable`, the entry you are sitting in is preselected.
   - Outline in a `.py` buffer → the action is greyed, not silent.
   - Outline in an unsaved buffer → the status bar says why.
   - Put the caret on a name that also appears in a comment and in a string →
     highlights on the uses, not the mentions.
   - Kill the server (`pkill -f 'tur lsp'`) mid-session, then use all three
     features → the editor stays fully usable and says what is wrong.
4. `pgrep -f 'tur lsp'` after quit — no survivors (unchanged from
   `lsp-support.md`, re-run because T0 changed the binary).

---

## 13. Risks

- **T0's sequencing assumption.** Everything here assumes Try Turmeric completes
  first and M5 ships upstream. If it slips, T2 still works but its kind column is
  useless, and T3 has nothing to call. The mitigation is ordering, not code: do
  not start T2 or T3 until the bump is done and the capability list is read back
  from the running binary.
- **A version bump is not free.** Six releases of a compiler is six releases of
  behavior change reaching the REPL, the run path, and the formatter as well as
  the LSP. The `v0.39.0` bump came through clean — build and full smoke suite
  green, no test touched — but that is one data point, not a rule, and the
  diagnostics tests are the ones most likely to move next time. Land each bump
  alone, so a regression has one suspect.
- **Read-only stdlib tabs leaking into persisted state.** Two exclusions
  (recent files, session restore) are the whole mitigation, both cheap, both
  tested (§11).
- **A textual occurrence fallback creeping in** because `documentHighlight`
  slipped and the feature is *so close*. It would be visibly wrong down the whole
  file once the minimap lane exists. §7 says skip; hold that line.
- **Dangling `EditorView*` in the history stack.** Tabs move between windows.
  Paths and offsets, never pointers (§5.4).
- **Blocking server under three new request types.** Definition and highlight are
  cheap reads off an index the server already built, and the upstream debounce
  landed, so the risk is lower than at `lsp-support.md` phase 1 — but three more
  request kinds on one blocking process is still three more. Reuse the existing
  2 s timeout and the staleness guard on every one of them; do not add a request
  that has no timeout.
- **`Ctrl+Shift+O` looks free and is not** (Open Directory,
  `main_window.cpp:195`). More generally: `keyboard-shortcuts.md` is the register,
  and every binding this plan adds goes in it in the same commit, or the doc
  becomes fiction.
