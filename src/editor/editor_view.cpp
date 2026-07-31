#include "editor/editor_view.h"

#include "editor/lexers.h"
#include "editor/theme_loader.h"
#include "lsp/lsp_manager.h"

#include <ScintillaEdit.h>

#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontInfo>
#include <QSettings>
#include <QTextStream>
#include <QVBoxLayout>

namespace trowel {

namespace {
constexpr int kLineNumberMargin = 0;
constexpr int kSymbolMargin = 1;
constexpr int kFoldMargin = 2;
}

EditorView::EditorView(QWidget* parent)
    : TabContent(parent)
    , sci_(new ScintillaEdit(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 4, 0, 0);
    layout->addWidget(sci_);

    applyDefaultStyling();

    rainbow_ = rainbowBracketsDefault();
    installLexer();
    ApplyThemeToEditor(sci_, LoadBuiltinDarkTheme());

    connect(sci_, &ScintillaEditBase::savePointChanged, this, [this](bool dirty) {
        emit modifiedChanged(dirty);
    });

    // Text edits bump the document version. Style and marker notifications also
    // arrive on this signal, so filter to actual content changes — otherwise
    // painting diagnostics would itself look like an edit and loop.
    connect(sci_, &ScintillaEditBase::modified, this,
            [this](Scintilla::ModificationFlags type, Scintilla::Position, Scintilla::Position,
                   Scintilla::Position, const QByteArray&, Scintilla::Position,
                   Scintilla::FoldLevel, Scintilla::FoldLevel) {
        constexpr auto kContentChange =
            Scintilla::ModificationFlags::InsertText | Scintilla::ModificationFlags::DeleteText;
        if ((type & kContentChange) == Scintilla::ModificationFlags::None) return;
        docVersion_++;
        // A `#lang` line can be typed, pasted, or edited away at any moment,
        // so the language is re-derived per edit rather than only on open.
        refreshLanguage();
        emit contentChanged(docVersion_);
    });

    // The server advertises `(` and space as triggers. We honor `(` only:
    // space fires on nearly every keystroke in a lisp, and each request forces
    // a didChange plus a full compile on the server's single thread. Explicit
    // completion (Ctrl+Space) covers the rest.
    connect(sci_, &ScintillaEditBase::charAdded, this, [this](int ch) {
        if (ch == '\n') autoIndentAfterNewline();
        if (ch == '(') emit completionRequested(cursorPos());
    });

    connect(sci_, &ScintillaEditBase::dwellStart, this, [this](int x, int y) {
        const int pos = int(sci_->positionFromPoint(x, y));
        if (pos >= 0) emit hoverRequested(pos);
    });
    connect(sci_, &ScintillaEditBase::dwellEnd, this, [this](int, int) {
        sci_->callTipCancel();
        emit hoverEnded();
    });

    attachLanguageServer();
}

// Talk to the language server from here rather than from MainWindow: the
// wiring is per-buffer, not per-window, and a tab can outlive the window it was
// created in (drag-and-drop between windows). Doing it in the view means it
// travels with the buffer and isn't duplicated per window.
void EditorView::attachLanguageServer() {
    LspManager* lsp = LspManager::instance();

    connect(this, &EditorView::filePathChanged, lsp, [this, lsp](const QString&) {
        // Covers both opening a file into a fresh tab and Save As, which can
        // change the language out from under us.
        lsp->closeDocument(this);
        lsp->openDocument(this);
    });
    connect(this, &EditorView::contentChanged, lsp, [this, lsp](int) {
        lsp->documentChanged(this);
    });
    connect(this, &QObject::destroyed, lsp, [this, lsp] { lsp->closeDocument(this); });

    connect(this, &EditorView::completionRequested, lsp, [this, lsp](int pos) {
        lsp->requestCompletion(this, pos, [this, pos](const QStringList& labels) {
            const int wordStart = static_cast<int>(sci_->wordStartPosition(pos, true));
            showCompletions(labels, qMax(0, pos - wordStart));
        });
    });
    connect(this, &EditorView::hoverRequested, lsp, [this, lsp](int pos) {
        lsp->requestHover(this, pos, [this, pos](const QString& text) {
            showHover(pos, text);
        });
    });

    connect(lsp, &LspManager::diagnosticsUpdated, this, [this, lsp](const QString& uri) {
        if (uri != LspManager::UriForPath(path_)) return;
        setDiagnostics(lsp->diagnosticsFor(uri));
    });
}

void EditorView::applyDefaultStyling() {
    sci_->styleSetFont(STYLE_DEFAULT, "Menlo");
    sci_->styleSetSize(STYLE_DEFAULT, 12);
    sci_->styleClearAll();

    sci_->setMarginTypeN(kLineNumberMargin, SC_MARGIN_NUMBER);
    sci_->setMarginWidthN(kLineNumberMargin, 44);
    // The symbol margin carries diagnostic markers. It stays narrow rather than
    // hidden so lines don't shift horizontally the moment an error appears.
    sci_->setMarginTypeN(kSymbolMargin, SC_MARGIN_SYMBOL);
    sci_->setMarginWidthN(kSymbolMargin, 12);
    sci_->setMarginMaskN(kSymbolMargin,
                         (1 << diag::kErrorMarker) | (1 << diag::kWarningMarker));
    sci_->setMarginWidthN(kFoldMargin, 0);

    sci_->markerDefine(diag::kErrorMarker, SC_MARK_CIRCLE);
    sci_->markerDefine(diag::kWarningMarker, SC_MARK_CIRCLE);
    sci_->indicSetStyle(diag::kErrorIndicator, INDIC_SQUIGGLE);
    sci_->indicSetStyle(diag::kWarningIndicator, INDIC_SQUIGGLE);
    // Colors come from the theme; these are visible fallbacks for a theme that
    // omits the diagnostics block.
    sci_->indicSetFore(diag::kErrorIndicator, 0x0000CC);
    sci_->indicSetFore(diag::kWarningIndicator, 0x00A0D0);

    // Hover: how long the mouse must rest before dwellStart fires.
    sci_->setMouseDwellTime(500);

    sci_->setCaretLineVisible(true);
    sci_->setCaretLineLayer(SC_LAYER_UNDER_TEXT);
    // Actual color set by the theme; alpha capped at ~64/255 so text remains
    // legible on the caret line.

    sci_->setUseTabs(false);
    sci_->setTabWidth(2);
    sci_->setIndent(2);
    sci_->setBackSpaceUnIndents(true);
    sci_->setEOLMode(SC_EOL_LF);

    sci_->setViewWS(SCWS_INVISIBLE);
    // Start at 1px so tracking can grow the width to fit the longest visible
    // line — leaving the default 2000px would show a horizontal scrollbar even
    // for empty buffers.
    sci_->setScrollWidth(1);
    sci_->setScrollWidthTracking(true);
    sci_->setHScrollBar(true);
    sci_->setEndAtLastLine(false);
}

void EditorView::setFont(const QFont& font) {
    currentFont_ = font;
    const QByteArray family = font.family().toUtf8();
    const int size = font.pointSize() > 0 ? font.pointSize() : 12;
    // Apply font to every style we know about individually. Do NOT use
    // styleClearAll — that would copy STYLE_DEFAULT to all styles and wipe the
    // per-style foreground colors installed by ApplyThemeToEditor.
    sci_->styleSetFont(STYLE_DEFAULT, family.constData());
    sci_->styleSetSize(STYLE_DEFAULT, size);
    sci_->styleSetFont(STYLE_LINENUMBER, family.constData());
    sci_->styleSetSize(STYLE_LINENUMBER, size);
    // Covers every language's block plus the rainbow bracket styles, all of
    // which live outside Scintilla's predefined 32..39 range.
    for (int s = 0; s <= kMaxStyleId; ++s) {
        sci_->styleSetFont(s, family.constData());
        sci_->styleSetSize(s, size);
    }
}

bool EditorView::rainbowBracketsDefault() {
    return QSettings().value("editor/rainbowBrackets", true).toBool();
}

void EditorView::installLexer() {
    // Scintilla takes ownership and releases any previously installed lexer.
    sci_->setILexer(reinterpret_cast<sptr_t>(CreateLexerForLanguage(language_, rainbow_)));
    // Re-lex the whole document so existing text picks up the new styling
    // immediately.
    sci_->colourise(0, -1);
}

void EditorView::autoIndentAfterNewline() {
    // Sweet-expression source is indentation-sensitive, so losing the indent on
    // every Enter is actively painful there. The other languages Trowel handles
    // are brace- or paren-delimited and have gotten along without auto-indent,
    // so they keep the plain behavior rather than inherit a half-rule.
    if (language_ != Language::TurmericSweet) return;

    const int pos = cursorPos();
    const int line = static_cast<int>(sci_->lineFromPosition(pos));
    if (line <= 0) return;
    // Only act when the caret is at the head of the new line. Splitting a line
    // in the middle carries its own text along; injecting indent there would
    // push that text rightward instead of lining it up.
    if (pos != static_cast<int>(sci_->positionFromLine(line))) return;

    const QByteArray prev = sci_->getLine(line - 1);
    int n = 0;
    while (n < prev.size() && (prev[n] == ' ' || prev[n] == '\t')) ++n;
    if (n == 0) return;

    const QByteArray indent = prev.left(n);
    sci_->insertText(pos, indent.constData());
    setCursorPos(pos + n);
}

void EditorView::setRainbowBrackets(bool enabled) {
    if (rainbow_ == enabled) return;
    rainbow_ = enabled;
    installLexer();
}

bool EditorView::loadFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return false;
    }
    const QByteArray contents = file.readAll();
    sci_->setText(contents.constData());
    sci_->emptyUndoBuffer();
    sci_->setSavePoint();
    setPath(path);
    emit modifiedChanged(false);
    return true;
}

bool EditorView::saveFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray contents = sci_->getText(sci_->textLength() + 1);
    // getText appends a NUL byte; strip it before writing.
    file.write(contents.constData(), contents.size() > 0 && contents.endsWith('\0')
                                         ? contents.size() - 1
                                         : contents.size());
    file.close();
    sci_->setSavePoint();
    setPath(path);
    emit modifiedChanged(false);
    return true;
}

bool EditorView::saveCurrent() {
    if (path_.isEmpty()) return false;
    return saveFile(path_);
}

QString EditorView::displayName() const {
    if (!path_.isEmpty()) return QFileInfo(path_).fileName();
    return {};
}

bool EditorView::isModified() const {
    return sci_->modify();
}

bool EditorView::isEmpty() const {
    return sci_->textLength() == 0;
}

QByteArray EditorView::text() const {
    QByteArray raw = sci_->getText(sci_->textLength() + 1);
    if (raw.endsWith('\0')) raw.chop(1);
    return raw;
}

QByteArray EditorView::textInRange(int startPos, int endPos) const {
    if (endPos <= startPos) return {};
    return sci_->textRange(startPos, endPos);
}

std::pair<int, int> EditorView::selectionRange() const {
    const int start = sci_->selectionStart();
    const int end = sci_->selectionEnd();
    return {start, end};
}

void EditorView::setText(const QByteArray& t) {
    sci_->setText(t.constData());
}

int EditorView::cursorPos() const {
    return static_cast<int>(sci_->currentPos());
}

int EditorView::anchorPos() const {
    return static_cast<int>(sci_->anchor());
}

void EditorView::setCursorPos(int pos) {
    sci_->gotoPos(pos);
}

void EditorView::setSelection(int anchor, int caret) {
    sci_->setSel(anchor, caret);
}

std::pair<int, int> EditorView::lineColFromPos(int pos) const {
    const int line = static_cast<int>(sci_->lineFromPosition(pos));
    const int lineStart = static_cast<int>(sci_->positionFromLine(line));
    return {line, pos - lineStart};
}

int EditorView::posFromLineCol(int line, int col) const {
    const int lineStart = static_cast<int>(sci_->positionFromLine(line));
    const int lineEnd = static_cast<int>(sci_->lineEndPosition(line));
    const int p = lineStart + col;
    return p > lineEnd ? lineEnd : p;
}

int EditorView::lineCount() const {
    return static_cast<int>(sci_->lineCount());
}

int EditorView::styleAt(int pos) const {
    // styleIndexAt, not styleAt: the latter goes through Document::StyleAt(),
    // which returns a signed char, so any style id above 127 (the CMake, TOML,
    // sh, and Python bands all are) would come back negative.
    return static_cast<int>(sci_->styleIndexAt(pos));
}

std::pair<int, int> EditorView::rangeForDiagnostic(const LspDiagnostic& d) const {
    // The buffer may have been edited since the server produced this range, so
    // clamp rather than trust it. See lsp_position.h on why `character` is a
    // byte offset here.
    const int docEnd = static_cast<int>(sci_->textLength());
    const int lastLine = static_cast<int>(sci_->lineFromPosition(docEnd));

    auto resolve = [&](int line, int character) {
        const int l = qBound(0, line, lastLine);
        const int lineStart = static_cast<int>(sci_->positionFromLine(l));
        const int lineEnd = static_cast<int>(sci_->lineEndPosition(l));
        return qBound(lineStart, lineStart + character, lineEnd);
    };

    int start = resolve(d.startLine, d.startChar);
    int end = resolve(d.endLine, d.endChar);
    if (end < start) std::swap(start, end);
    // A zero-width range paints nothing. Widen it by one character so an
    // insertion-point diagnostic is still visible.
    if (end == start) end = qMin(start + 1, docEnd);
    return {start, end};
}

void EditorView::clearDiagnosticDecorations() {
    const int docEnd = static_cast<int>(sci_->textLength());
    for (int indicator : {diag::kErrorIndicator, diag::kWarningIndicator}) {
        sci_->setIndicatorCurrent(indicator);
        sci_->indicatorClearRange(0, docEnd);
    }
    sci_->markerDeleteAll(diag::kErrorMarker);
    sci_->markerDeleteAll(diag::kWarningMarker);
}

void EditorView::setDiagnostics(const QVector<LspDiagnostic>& diagnostics) {
    diagnostics_ = diagnostics;
    clearDiagnosticDecorations();

    for (const LspDiagnostic& d : diagnostics_) {
        const bool isError = d.severity <= LspDiagnostic::Error;
        const auto [start, end] = rangeForDiagnostic(d);
        if (end <= start) continue;

        sci_->setIndicatorCurrent(isError ? diag::kErrorIndicator : diag::kWarningIndicator);
        sci_->indicatorFillRange(start, end - start);
        sci_->markerAdd(sci_->lineFromPosition(start),
                        isError ? diag::kErrorMarker : diag::kWarningMarker);
    }
}

QString EditorView::diagnosticMessageAt(int pos) const {
    for (const LspDiagnostic& d : diagnostics_) {
        const auto [start, end] = rangeForDiagnostic(d);
        if (pos >= start && pos <= end) return d.message;
    }
    return {};
}

void EditorView::showCompletions(const QStringList& labels, int lengthEntered) {
    if (labels.isEmpty()) {
        sci_->autoCCancel();
        return;
    }
    // Scintilla splits on the separator and expects the list pre-sorted; the
    // server returns document order, so sort here.
    QStringList sorted = labels;
    sorted.sort();
    sorted.removeDuplicates();

    sci_->autoCSetSeparator('\n');
    // Don't steal Enter/Tab when the only candidate is what the user already
    // typed — that would turn a deliberate newline into an acceptance.
    sci_->autoCSetChooseSingle(false);
    sci_->autoCSetIgnoreCase(false);
    sci_->autoCShow(lengthEntered, sorted.join('\n').toUtf8().constData());
}

void EditorView::showHover(int pos, const QString& markdown) {
    // The server wraps signatures in ``` fences and the docstring below them.
    // Call tips are plain text, so drop the fence lines rather than showing
    // literal backticks.
    QStringList lines;
    for (const QString& line : markdown.split('\n')) {
        if (line.trimmed().startsWith(QLatin1String("```"))) continue;
        lines << line;
    }
    while (!lines.isEmpty() && lines.first().trimmed().isEmpty()) lines.removeFirst();
    while (!lines.isEmpty() && lines.last().trimmed().isEmpty()) lines.removeLast();
    if (lines.isEmpty()) return;

    sci_->callTipShow(pos, lines.join('\n').toUtf8().constData());
}

void EditorView::setPath(const QString& path) {
    if (path_ == path) return;
    path_ = path;
    // Covers opening a file into a fresh tab as well as Save As to a new
    // extension.
    refreshLanguage();
    emit filePathChanged(path_);
}

void EditorView::refreshLanguage() {
    const Language lang = LanguageForBuffer(path_, languageProbeText());
    if (lang == language_) return;
    language_ = lang;
    installLexer();
}

QByteArray EditorView::langDirectiveLine() const {
    return LangDirectiveLine(languageProbeText());
}

QByteArray EditorView::languageProbeText() const {
    // Two lines, not one: a `#lang` directive may sit on line 2 when line 1 is
    // a `#!` shebang. Bounding the probe keeps this affordable on every edit.
    const int probeLines = 2;
    const int end = (lineCount() > probeLines)
                        ? static_cast<int>(sci_->positionFromLine(probeLines))
                        : static_cast<int>(sci_->textLength());
    return textInRange(0, end);
}

}
