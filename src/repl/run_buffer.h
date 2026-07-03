#pragma once

#include <QString>

namespace trowel {

class EditorView;
class ReplSession;

struct RunResult {
    bool ok = false;
    QString message;      // human-readable status / error
    QString scratchPath;  // if the buffer was written to a temp file, its path
};

// Send the editor's whole buffer to the running REPL via `(load "…")`.
// If the buffer is dirty or untitled, it's first written to a scratch file
// under the platform cache dir. The extension follows the current file's
// extension when known; otherwise `.tur`.
RunResult RunBuffer(EditorView* editor, ReplSession* repl);

// Same, but only the given byte range of the buffer.
RunResult RunRange(EditorView* editor, ReplSession* repl, int startPos, int endPos);

}
