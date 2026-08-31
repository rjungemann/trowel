#pragma once

#include "lsp/lsp_location.h"

#include <QString>

namespace trowel {

// One entry of `textDocument/documentSymbol`.
//
// Both ranges are kept because the outline needs each for a different job:
// `selection` is the name, which is where a jump lands and what decides
// "the caret is sitting on this symbol"; `range` is the whole definition,
// which decides "the caret is somewhere inside this symbol's body".
struct LspSymbol {
    QString name;
    // LSP SymbolKind. The pinned server emits Function (12), Variable (13) and
    // Struct (23); see LspSymbolKindLabel for how those become words.
    int kind = 0;
    LspRange selection;
    LspRange range;
};

// The word you would say out loud about an entry — `function`, not `Function`,
// and not the LSP enum's spelling.
//
// Deliberately coarse. The outline exists to answer "what is in this file",
// and a column of exact LSP taxonomy answers a question nobody asked.
inline QString LspSymbolKindLabel(int kind) {
    switch (kind) {
        case 12: return QStringLiteral("function");
        case 6:  return QStringLiteral("method");
        case 13: return QStringLiteral("value");
        case 14: return QStringLiteral("constant");
        case 23: return QStringLiteral("type");
        case 5:  return QStringLiteral("type");
        case 11: return QStringLiteral("interface");
        case 10: return QStringLiteral("enum");
        case 22: return QStringLiteral("enum case");
        // Anything the server grows later still shows a row; only its label is
        // missing, which beats hiding the symbol entirely.
        default: return QString();
    }
}

}
