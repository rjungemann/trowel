#pragma once

#include <QString>

namespace trowel {

// One entry from a textDocument/publishDiagnostics batch, still in LSP
// coordinates — EditorView resolves them to Scintilla positions when painting,
// because the buffer may have moved on since the server saw it.
struct LspDiagnostic {
    // Per the LSP DiagnosticSeverity enum. Absent severity is treated as Error,
    // which is what Turmeric's server emits.
    enum Severity { Error = 1, Warning = 2, Information = 3, Hint = 4 };

    int severity = Error;
    int startLine = 0;
    int startChar = 0;
    int endLine = 0;
    int endChar = 0;
    QString message;
    QString source;
};

}
