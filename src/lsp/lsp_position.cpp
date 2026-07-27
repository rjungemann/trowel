#include "lsp/lsp_position.h"

#include <ScintillaEdit.h>

namespace trowel {

namespace {

// Length in bytes of the UTF-8 sequence starting with `lead`. Returns 1 for
// continuation bytes and invalid leads so a malformed buffer still advances.
int Utf8SequenceLength(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;
}

} // namespace

int Utf16ColumnFromByteColumn(const QByteArray& lineBytes, int byteCol) {
    const int limit = qBound(0, byteCol, int(lineBytes.size()));
    int utf16 = 0;
    for (int i = 0; i < limit;) {
        const int len = Utf8SequenceLength(static_cast<unsigned char>(lineBytes[i]));
        // Anything outside the BMP is a surrogate pair: two UTF-16 code units.
        utf16 += (len == 4) ? 2 : 1;
        i += len;
    }
    return utf16;
}

int ByteColumnFromUtf16Column(const QByteArray& lineBytes, int utf16Col) {
    if (utf16Col <= 0) return 0;
    int utf16 = 0;
    int i = 0;
    while (i < lineBytes.size()) {
        if (utf16 >= utf16Col) break;
        const int len = Utf8SequenceLength(static_cast<unsigned char>(lineBytes[i]));
        utf16 += (len == 4) ? 2 : 1;
        i += len;
    }
    return qMin(i, int(lineBytes.size()));
}

LspPosition LspPositionFromPos(ScintillaEdit* sci, int pos) {
    LspPosition out;
    if (!sci) return out;

    const int clamped = qBound(0, pos, int(sci->textLength()));
    const int line = int(sci->lineFromPosition(clamped));
    const int lineStart = int(sci->positionFromLine(line));
    const int byteCol = clamped - lineStart;

    out.line = line;
    if (kPositionEncoding == PositionEncoding::Utf8) {
        out.character = byteCol;
    } else {
        out.character = Utf16ColumnFromByteColumn(sci->getLine(line), byteCol);
    }
    return out;
}

int PosFromLspPosition(ScintillaEdit* sci, const LspPosition& position) {
    if (!sci) return 0;

    const int lastLine = int(sci->lineFromPosition(sci->textLength()));
    const int line = qBound(0, position.line, lastLine);
    const int lineStart = int(sci->positionFromLine(line));
    // lineLength includes the EOL; clamping to it keeps an out-of-range
    // character from walking into the next line.
    const int lineBytes = int(sci->lineLength(line));

    int byteCol;
    if (kPositionEncoding == PositionEncoding::Utf8) {
        byteCol = qBound(0, position.character, lineBytes);
    } else {
        byteCol = ByteColumnFromUtf16Column(sci->getLine(line), position.character);
    }
    return lineStart + byteCol;
}

QJsonObject LspPositionToJson(const LspPosition& position) {
    return QJsonObject{{"line", position.line}, {"character", position.character}};
}

LspPosition LspPositionFromJson(const QJsonObject& obj) {
    LspPosition p;
    p.line = obj.value("line").toInt();
    p.character = obj.value("character").toInt();
    return p;
}

}
