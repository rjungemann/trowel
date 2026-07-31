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

// What the "run / evaluate" action should do for a given document.
enum class EvalMode {
    // Not a Turmeric document — the run actions are greyed out.
    Disabled,
    // A Turmeric source file (or an untitled buffer, which is Turmeric by
    // default): evaluate the buffer / selection in the REPL.
    Buffer,
    // A `build.tur` manifest: the buffer is a project description, not a
    // script, so "run" means building the project that owns it.
    Project,
};

// Classify a document by its path alone. Deliberately *not* LanguageForPath():
// that falls back to Turmeric for anything it doesn't recognize (so a .txt file
// would look runnable), which is precisely the case this gate exists to reject.
// An empty path is an untitled buffer and stays runnable — those are Turmeric
// scratch buffers.
EvalMode EvalModeForPath(const QString& path);

// Directory that owns `buildManifestPath` (a `build.tur` / `build.tur.sweet`),
// i.e. what `tur build <dir>` should be pointed at. Empty when the path is not
// a manifest.
QString ProjectDirForPath(const QString& buildManifestPath);

// Send the editor's whole buffer to the running REPL via `(load "…")`.
// If the buffer is dirty or untitled, it's first written to a scratch file
// under the platform cache dir. The extension follows the current file's
// extension when known; otherwise `.tur`.
RunResult RunBuffer(EditorView* editor, ReplSession* repl);

// Same, but only the given byte range of the buffer.
RunResult RunRange(EditorView* editor, ReplSession* repl, int startPos, int endPos);

}
