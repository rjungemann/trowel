#pragma once

#include <QHash>
#include <QString>
#include <QVector>

namespace trowel {

// A place in a file, as the server names it: a document URI plus a zero-based
// line/character pair.
//
// Lives in its own header rather than in lsp_manager.h so EditorView can carry
// one in a signal without pulling the whole manager into its interface — the
// same split lsp_diagnostic.h already makes.
struct LspLocation {
    QString uri;
    int line = 0;
    int character = 0;

    // False when the server had no answer. `textDocument/definition` returns
    // null for any name its index never saw, which after §4.2.1 includes every
    // name in a file that failed to analyze — so "no location" is a routine
    // reply to be reported, not an error to be swallowed.
    bool isValid() const { return !uri.isEmpty(); }
};

// A half-open span of a document, in the server's zero-based line/character
// coordinates. Converted to Scintilla byte offsets at the point of use.
struct LspRange {
    int startLine = 0;
    int startCharacter = 0;
    int endLine = 0;
    int endCharacter = 0;

    // Does this range cover (line, character)? End is exclusive, matching LSP.
    bool contains(int line, int character) const {
        if (line < startLine || line > endLine) return false;
        if (line == startLine && character < startCharacter) return false;
        if (line == endLine && character >= endCharacter) return false;
        return true;
    }
};

// A URI plus a span. `LspLocation` names a point — where to put the caret;
// this names a region — what to select, or what to replace.
struct LspSpan {
    QString uri;
    LspRange range;
};

// One edit from a `WorkspaceEdit`. Ranges are the server's, so they describe
// the document as the server last saw it; applying them out of descending
// order would invalidate the ones that follow.
struct LspTextEdit {
    LspRange range;
    QString newText;
};

// A `WorkspaceEdit`, keyed by document URI.
//
// A free alias rather than a member of LspManager so consumers that only apply
// edits — MainWindow — do not have to pull in the manager's whole interface.
using LspWorkspaceEdit = QHash<QString, QVector<LspTextEdit>>;

}
