#pragma once

#include "app/tab_content.h"
#include "editor/lexers.h"
#include "lsp/lsp_diagnostic.h"

#include <QFont>
#include <QString>
#include <QVector>

#include <utility>

class ScintillaEdit;

namespace trowel {

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

    // Language this buffer is highlighted as, derived from its path.
    Language language() const { return language_; }

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

    // Show an LSP completion list. Labels are sorted here rather than by the
    // caller — Scintilla requires a sorted list unless told otherwise.
    void showCompletions(const QStringList& labels, int lengthEntered);
    // Show hover text as a call tip, stripping the markdown fences the server
    // wraps type signatures in.
    void showHover(int pos, const QString& markdown);

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

private:
    void applyDefaultStyling();
    void setPath(const QString& path);
    // Install a lexer for the current language + rainbow setting and re-lex.
    void installLexer();
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
};

}
