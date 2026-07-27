#pragma once

#include <QByteArray>
#include <QJsonObject>

class ScintillaEdit;

namespace trowel {

// Conversion between Scintilla byte offsets and LSP {line, character}.
//
// THE ENCODING PROBLEM. LSP's default `character` unit is UTF-16 code units.
// Turmeric's server does not honor that: lsp_util.c indexes the line by *byte*
// offset. For ASCII source the two agree, which is why nobody has noticed.
//
// Phase 1 therefore speaks bytes, matching the server we actually talk to
// rather than the spec we both claim to implement. That is a deliberate,
// contained lie: it lives behind kPositionEncoding, and the UTF-16 conversion
// is implemented and tested alongside it. Phase 2 upstream adds LSP 3.17
// `general.positionEncoding` negotiation advertising utf-8, at which point
// bytes become the *correct* answer and this file needs no change at all — the
// Utf16 branch is there for a server that negotiates otherwise.
enum class PositionEncoding {
    Utf8,   // `character` counts bytes — what Turmeric's server does today
    Utf16,  // `character` counts UTF-16 code units — what the spec says
};

constexpr PositionEncoding kPositionEncoding = PositionEncoding::Utf8;

struct LspPosition {
    int line = 0;       // 0-based
    int character = 0;  // unit per kPositionEncoding
};

LspPosition LspPositionFromPos(ScintillaEdit* sci, int pos);
int PosFromLspPosition(ScintillaEdit* sci, const LspPosition& position);

QJsonObject LspPositionToJson(const LspPosition& position);
LspPosition LspPositionFromJson(const QJsonObject& obj);

// Column conversions within one line's bytes. Exposed for testing; `byteCol`
// and the return value are both clamped into the line.
int Utf16ColumnFromByteColumn(const QByteArray& lineBytes, int byteCol);
int ByteColumnFromUtf16Column(const QByteArray& lineBytes, int utf16Col);

}
