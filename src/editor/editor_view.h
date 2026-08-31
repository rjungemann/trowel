#pragma once

#include "app/tab_content.h"
#include "editor/lexers.h"
#include "lsp/lsp_diagnostic.h"
#include "lsp/lsp_location.h"
#include "lsp/lsp_symbol.h"

#include <QFont>
#include <QString>
#include <QVector>

#include <utility>

class ScintillaEdit;
class QLineEdit;
class QTimer;

namespace trowel {

// The vertical half of the bracket-pair guide, drawn as a transparent overlay
// over Scintilla's viewport.
//
// Scintilla's own indentation guides are the wrong tool: they sit at multiples
// of the indent width in the single STYLE_INDENTGUIDE colour, and a lisp opener
// is rarely at a multiple of the indent width — they would draw a line in the
// wrong column in the wrong colour.
//
// Defined in the .cpp. If minimap phase 4 ever lands its own overlay pass, this
// should fold into it rather than stack a second transparent widget over the
// same viewport (see minimap.md and editor-intelligence.md §4.4).
class BracketGuideOverlay;

// Scintilla indicator and marker slots used for diagnostics.
//
// Indicators 0-7 are reserved by Scintilla's own conventions; 8 upward
// (INDICATOR_CONTAINER) are free for the container. Markers 25-31 are reserved
// for folding, so diagnostics take the low numbers.
namespace diag {
constexpr int kErrorIndicator = 8;
constexpr int kWarningIndicator = 9;
constexpr int kErrorMarker = 0;
constexpr int kWarningMarker = 1;
}

// Occurrence highlighting takes the next free slot above the diagnostics.
//
// A translucent box, never a squiggle: an occurrence is not a problem and must
// not read as one.
namespace occurrence {
constexpr int kIndicator = 10;
}

// The active bracket-pair guide: a single straight line under the expression
// enclosing the caret, in that pair's own nesting-depth colour.
//
// 11 is left free so the definition/use split occurrence highlighting
// deliberately does not make (see editor-intelligence.md §3.2) stays cheap to
// add later without renumbering anything.
namespace bracketguide {
constexpr int kIndicator = 12;
}

// Debugger markers. Markers 0-1 are diagnostics, 25-31 are folding, so 2-6 are
// free. The breakpoint margin is a dedicated margin (kBreakpointMargin = 3)
// so click-to-toggle does not steal clicks from the diagnostic symbol margin,
// and the two decorations stop competing for 12px.
namespace dbg {
constexpr int kBreakpointMarker = 2;
constexpr int kBreakpointDisabledMarker = 3;
constexpr int kCurrentLineMarker = 4;
constexpr int kSelectedFrameMarker = 5;
constexpr int kBreakpointMargin = 3;
}

class EditorView : public TabContent {
    Q_OBJECT
public:
    explicit EditorView(QWidget* parent = nullptr);

    bool loadFile(const QString& path);
    bool saveFile(const QString& path);
    bool saveCurrent();

    Kind kind() const override { return Kind::Editor; }
    QString displayName() const override;
    QString filePath() const override { return path_; }
    bool isModified() const override;
    bool isEmpty() const override;
    QByteArray text() const;
    QByteArray textInRange(int startPos, int endPos) const;
    std::pair<int, int> selectionRange() const;

    void setFont(const QFont& font);
    QFont currentFont() const { return currentFont_; }

    // Toggle rainbow (depth-colored) brackets and re-lex the whole document.
    void setRainbowBrackets(bool enabled);
    bool rainbowBrackets() const { return rainbow_; }

    // The bracket pair enclosing `pos`, or a pair of -1s when there is none.
    //
    // Ours to compute: SCI_BRACEMATCH only answers when the caret is already
    // adjacent to a brace, and Scintilla has no notion of "the pair enclosing
    // this position". The scan counts only bracket characters the lexer did not
    // style as a comment or a string, which is what makes it skip a `(` inside
    // either without a second parser.
    //
    // Bounded: it gives up rather than stall, because it runs on every caret
    // move. Giving up paints nothing, which is the honest failure.
    std::pair<int, int> enclosingBracketPair(int pos) const;

    // Paint the guide for the pair enclosing the caret, clearing the previous
    // one first. A no-op when the preference is off.
    void updateBracketGuide();
    void clearBracketGuide();
    void setBracketPairGuides(bool enabled);
    bool bracketPairGuides() const { return bracketGuides_; }
    static bool bracketPairGuidesDefault();

    // Language this buffer is highlighted as, derived from its path and any
    // `#lang` directive it carries.
    Language language() const { return language_; }

    // This buffer's `#lang` line, newline-terminated, or empty. Used to keep
    // the directive attached when only part of the buffer is run.
    QByteArray langDirectiveLine() const;

    // Default rainbow-bracket preference, read from QSettings.
    static bool rainbowBracketsDefault();

    // Monotonic edit counter, bumped on every text insertion/deletion. Used as
    // the LSP document version; unlike isModified() it never resets on save.
    int docVersion() const { return docVersion_; }

    // Paint a diagnostic batch: squiggle indicators over the offending ranges
    // and markers in the symbol margin. Clears the previous batch first.
    void setDiagnostics(const QVector<LspDiagnostic>& diagnostics);
    const QVector<LspDiagnostic>& diagnostics() const { return diagnostics_; }
    // Message of the first diagnostic covering `pos`, or empty. Used for the
    // status-bar readout as the caret moves.
    QString diagnosticMessageAt(int pos) const;

    // --- Debugger ---
    // One breakpoint mark to paint in the gutter. `line` is 1-based; `enabled`
    // false draws a hollow marker; `pending` true (not yet verified by the
    // adapter) draws a hollow marker too, so the send-timing delay is visible
    // rather than mysterious (constraint 6: mid-run setBreakpoints is not
    // processed until the next stop).
    struct BreakpointMark {
        int line = 0;
        bool enabled = true;
        bool pending = false;
    };
    // Replace all breakpoint markers in this buffer. Clears the previous set
    // first, mirroring setDiagnostics.
    void setBreakpointMarkers(const QVector<BreakpointMark>& marks);
    // Paint the current-execution line (frame 0) or a selected frame. `line`
    // is 1-based; `isTopFrame` true uses the current-line marker, false the
    // selected-frame marker (visually distinct). Reveals the line.
    void setExecutionLine(int line, bool isTopFrame);
    void clearExecutionLine();

    // Show an LSP completion list. Labels are sorted here rather than by the
    // caller — Scintilla requires a sorted list unless told otherwise.
    void showCompletions(const QStringList& labels, int lengthEntered);
    // Show hover text as a call tip, stripping the markdown fences the server
    // wraps type signatures in.
    void showHover(int pos, const QString& markdown);

    // Show the document outline as a Scintilla user list: the completion
    // widget with a different list type and no insertion side effect, so it
    // inherits the popup's theme styling for free.
    //
    // Document order is preserved (SC_ORDER_CUSTOM); alphabetising would
    // destroy the one thing an outline is for. The entry the caret is in is
    // preselected, which answers "where am I" better than a highlight the user
    // has to hunt for.
    void showSymbolList(const QVector<LspSymbol>& symbols);
    // Show a single non-actionable row — "Nothing defined yet", or why the
    // outline is unavailable. Choosing it does nothing.
    void showOutlineMessage(const QString& message);
    const QVector<LspSymbol>& outlineSymbols() const { return outlineSymbols_; }
    // Index of the symbol the caret is in, or -1.
    //
    // c2mp's rule, ported: the name wins when the caret is actually on one,
    // otherwise the smallest containing range wins — someone editing the middle
    // of a function is not sitting on its name.
    int symbolIndexAtCaret(const QVector<LspSymbol>& symbols) const;

    // Paint occurrence highlights, clearing the previous set first.
    void setOccurrences(const QVector<LspRange>& ranges);
    void clearOccurrences();
    const QVector<LspRange>& occurrences() const { return occurrences_; }
    std::pair<int, int> bracketGuideSpan() const { return bracketGuideSpan_; }

    // Where the vertical overlay is drawn, in viewport pixels. `visible` is
    // false for a single-line pair (no vertical extent) and when there is no
    // enclosing pair at all. Exposed so a test can assert on the line that was
    // painted rather than on the pair that was computed.
    struct GuideLine {
        bool visible = false;
        int x = 0;
        int top = 0;
        int bottom = 0;
    };
    GuideLine bracketGuideLine() const { return guideLine_; }

    // Show an arbitrary chooser as a user list. Rows are displayed verbatim and
    // the chosen row's index comes back through `listRowChosen` — unlike the
    // outline, the caller owns what a row means.
    void showChooserList(const QStringList& rows);

    // Apply one `TextEdit` in place, without disturbing the selection.
    //
    // Callers must wrap a batch in beginEditGroup/endEditGroup and apply it in
    // *descending* position order: every range in a WorkspaceEdit is stated
    // against the document as the server saw it, so an earlier edit that
    // changes length invalidates every later one.
    void replaceRange(const LspRange& range, const QString& text);
    void beginEditGroup();
    void endEditGroup();

    // Show the rename input over `range`, pre-filled with `placeholder`.
    //
    // Non-modal by design. A modal dialog would block the control socket's
    // event loop, so every smoke test that reached a rename would hang rather
    // than fail — and a rename that cannot be tested is the one operation in
    // this editor least able to afford it.
    void showRenameInput(const LspRange& range, const QString& placeholder);
    void hideRenameInput();
    bool renameInputVisible() const;
    QString renameInputText() const;

    // Make this buffer unwritable. Used for stdlib files opened by a definition
    // jump: they live inside the app bundle, where an edit either invalidates
    // the code signature or is lost at the next upgrade. A lock is the honest
    // UI for that; a save error after the fact is not.
    void setReadOnly(bool readOnly);
    bool isReadOnly() const;

    // Control API surface.
    ScintillaEdit* sciWidget() const { return sci_; }
    void setText(const QByteArray& text);
    int cursorPos() const;
    int anchorPos() const;
    void setCursorPos(int pos);
    void setSelection(int anchor, int caret);
    std::pair<int, int> lineColFromPos(int pos) const;
    int posFromLineCol(int line, int col) const;
    int lineCount() const;
    int styleAt(int pos) const;

signals:
    // Text changed. Carries the new docVersion() so a listener can tell edits
    // apart from styling-only notifications.
    void contentChanged(int version);
    // The user asked for completions, either by typing a trigger character or
    // via the explicit action. `pos` is the caret.
    void completionRequested(int pos);
    // The mouse came to rest over `pos`.
    void hoverRequested(int pos);
    void hoverEnded();
    // The user asked where the symbol at `pos` is defined.
    void definitionRequested(int pos);
    // The server answered. An invalid location means "no definition" — the
    // window reports that rather than leaving the request looking pending.
    //
    // Resolved here and acted on by MainWindow because the LSP wiring is
    // per-buffer while navigation is per-window: the reply has to cross that
    // boundary, and a signal is how the rest of this class already does it.
    void definitionResolved(const LspLocation& location);
    // An outline row was chosen. Carries the symbol's name position; the window
    // turns it into a jump so the same history stack covers outline and
    // definition navigation alike.
    void outlineSymbolChosen(int line, int character);
    // A row of the last showChooserList() was picked.
    void listRowChosen(int index);
    // The rename input was confirmed with Enter, or dismissed with Escape.
    void renameCommitted(const QString& newName);
    void renameCancelled();
    // The user clicked the breakpoint margin at `line` (1-based). MainWindow
    // toggles the breakpoint in the model; the model's `changed` signal flows
    // back here as a new set of markers.
    void breakpointToggleRequested(int line);

protected:
    // Watches the rename input for Escape and focus loss.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyDefaultStyling();
    void setPath(const QString& path);
    // Install a lexer for the current language + rainbow setting and re-lex.
    void installLexer();
    // Re-derive the language from the path plus a `#lang` line and re-lex if
    // it changed. Cheap enough to run on every edit.
    void refreshLanguage();
    QByteArray languageProbeText() const;
    // After Enter, carry the previous line's leading whitespace onto the new
    // one. Only active for languages where indentation is load-bearing.
    void autoIndentAfterNewline();
    // Connect this buffer to the application-wide language server.
    void attachLanguageServer();
    void clearDiagnosticDecorations();
    // Resolve an LSP range to Scintilla byte positions, clamped into the
    // document as it stands now.
    std::pair<int, int> rangeForDiagnostic(const LspDiagnostic& d) const;

    ScintillaEdit* sci_;
    QString path_;
    QFont currentFont_;
    bool rainbow_ = true;
    Language language_ = Language::Turmeric;
    int docVersion_ = 0;
    QVector<LspDiagnostic> diagnostics_;
    // The list currently on screen, kept so a selection can be mapped back to
    // a symbol: SCN_USERLISTSELECTION reports the chosen *text*, not an index.
    QVector<LspSymbol> outlineSymbols_;
    QStringList outlineRows_;
    // Coalesces caret movement into one request per settle. Without it, holding
    // an arrow key would queue a request per keystroke against a server that
    // handles them one at a time.
    QTimer* occurrenceDebounce_ = nullptr;
    QVector<LspRange> occurrences_;
    QStringList chooserRows_;
    QLineEdit* renameInput_ = nullptr;
    bool bracketGuides_ = true;
    BracketGuideOverlay* guideOverlay_ = nullptr;
    // The pair the overlay is currently drawing, kept so it can be repositioned
    // on scroll without recomputing the scan.
    int guideOpener_ = -1;
    int guideCloser_ = -1;
    GuideLine guideLine_;
    // Reposition the vertical overlay from guideOpener_/guideCloser_.
    void repositionBracketGuideOverlay();
    // The span the guide currently occupies, so the control API can read back
    // what was painted rather than recomputing it.
    std::pair<int, int> bracketGuideSpan_{-1, -1};
};

}
