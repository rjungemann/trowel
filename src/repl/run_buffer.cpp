#include "repl/run_buffer.h"

#include "editor/editor_view.h"
#include "repl/repl_session.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QStandardPaths>

namespace trowel {

namespace {

QString scratchDir() {
    const QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    const QString dir = base + "/scratch";
    QDir().mkpath(dir);
    return dir;
}

// Scratch-file extension for the buffer's language. `.tur.sweet` is the only
// suffix the toolchain maps to a non-default reader, and it is what makes a
// dirty sweet buffer parse as sweet. The buffer's own `#lang` line, when it
// has one, travels with the contents and covers everything the suffix cannot.
QString extensionFor(const EditorView* editor) {
    return editor->language() == Language::TurmericSweet ? ".tur.sweet" : ".tur";
}

// Escape a filesystem path for embedding in a turmeric string literal.
QString escapeForTurmericString(const QString& path) {
    QString out;
    out.reserve(path.size() + 2);
    for (QChar c : path) {
        if (c == '\\' || c == '"') out.append('\\');
        out.append(c);
    }
    return out;
}

// Whether `:run` is safe for a file with this extension.
//
// `:run` is the right command and is used wherever it works. It is broken
// upstream for any extension that selects a non-default reader — `.tur.sweet`
// and `.sweet`. Measured against the bundled v0.42.2: `:run` on a `.tur.sweet`
// file prints `;; run:` then `;; ready` and defines nothing at all, silently.
//
// The cause is visible in the toolchain source. cmd_run (repl.c) builds a
// fresh env and calls repl_preload_stdlib_and_natives on it, then
// turi_eval_file (eval.c) sees a non-default reader in the extension and calls
// turi_env_reset_to_prelude — throwing away the preload nobody re-runs. The
// file then fails to elaborate, and cmd_run drops "elaboration error" on the
// floor rather than printing it. repl.c's own comment above
// repl_preload_stdlib_and_natives names this exact hazard.
//
// The condition is purely extension-driven upstream, so testing the extension
// here matches it exactly: a `.tur` carrying a `#lang` *layer* still takes the
// default reader and is still fine.
bool runCommandIsSafeFor(const QString& path) {
    const QString lower = QFileInfo(path).fileName().toLower();
    return !lower.endsWith(".tur.sweet") && !lower.endsWith(".sweet");
}

// Run a whole program: `:run <path>`, the REPL's own entry point for "execute
// this file".
//
// NOT `(load "<path>")`. `load` evaluates the top-level forms and stops, so a
// program shaped the way Turmeric programs are shaped — a `defn main` and
// little else at the top level — defined `main`, printed `=> #<fn main>`, and
// never ran. Run Buffer reported success on a program that had not executed.
//
// `:run` (repl.c, cmd_run) evaluates the file and then invokes `main` when it
// resolves to a closure, staying silent when there is none, so a script of
// bare top-level forms still behaves as it always did. It also installs a
// fresh environment first, which is why no separate `:reset` is sent — that
// was this function's job before and is now `:run`'s.
//
// The path is the rest of the line, unquoted and unescaped: cmd_run takes
// everything after `:run ` verbatim, so a path with spaces needs no quoting
// and quoting it would make the quotes part of the filename.
QByteArray runCommandFor(const QString& path) {
    return QString(":run %1").arg(path).toUtf8();
}

// Evaluate a fragment in the current environment: `(load "<path>")`.
//
// Deliberately still `load`, and deliberately no reset — a selection is
// evaluated against whatever the session already holds, and must not invoke
// `main` just because the buffer it came from defines one.
QByteArray loadCommandFor(const QString& path) {
    return QString("(load \"%1\")").arg(escapeForTurmericString(path)).toUtf8();
}

// Write `contents` to a scratch file and hand the path back, or set `r.message`
// and return empty on failure.
QString writeScratch(const QByteArray& contents, const QString& ext,
                     RunResult& r) {
    const QString dir = scratchDir();
    const quint32 stamp = QRandomGenerator::global()->generate();
    const QString path = QString("%1/buffer-%2%3")
        .arg(dir)
        .arg(stamp, 8, 16, QChar('0'))
        .arg(ext);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        r.message = QString("Could not write scratch file at %1").arg(path);
        return {};
    }
    file.write(contents);
    file.close();
    return path;
}

RunResult writeScratchAndLoad(ReplSession* repl,
                              const QByteArray& contents, const QString& ext) {
    RunResult r;
    const QString path = writeScratch(contents, ext, r);
    if (path.isEmpty()) return r;
    if (!repl->sendCommand(loadCommandFor(path))) {
        r.message = "REPL is not running.";
        return r;
    }
    r.ok = true;
    r.scratchPath = path;
    r.message = QString("Loaded %1").arg(QFileInfo(path).fileName());
    return r;
}

// Send whichever of the two a whole-buffer run should use for this path, and
// describe what was done. `:reset` is sent only on the load fallback: `:run`
// installs a fresh env itself, and a stray reset before it would be redundant.
RunResult sendWholeBuffer(ReplSession* repl, const QString& path) {
    RunResult r;
    const bool canRun = runCommandIsSafeFor(path);
    if (!canRun && !repl->sendCommand(QByteArray(":reset"))) {
        r.message = "REPL is not running.";
        return r;
    }
    if (!repl->sendCommand(canRun ? runCommandFor(path) : loadCommandFor(path))) {
        r.message = "REPL is not running.";
        return r;
    }
    r.ok = true;
    r.message = QString(canRun ? "Ran %1" : "Loaded %1")
                    .arg(QFileInfo(path).fileName());
    return r;
}

RunResult writeScratchAndRun(ReplSession* repl,
                             const QByteArray& contents, const QString& ext) {
    RunResult r;
    const QString path = writeScratch(contents, ext, r);
    if (path.isEmpty()) return r;
    r = sendWholeBuffer(repl, path);
    if (r.ok) r.scratchPath = path;
    return r;
}

// True when `name` is one of the two spellings the toolchain accepts for a
// project manifest. See the Turmeric "developing spices" guide: `build.tur`
// and `build.tur.sweet` are equivalent everywhere, with the plain one winning
// when both are present.
bool isBuildManifestName(const QString& name) {
    return name.compare("build.tur", Qt::CaseInsensitive) == 0
        || name.compare("build.tur.sweet", Qt::CaseInsensitive) == 0;
}

// Extensions that make a file Turmeric source. Mirrors LanguageForPath's
// extension table minus its catch-all fallback.
bool hasTurmericExtension(const QString& name) {
    const QString lower = name.toLower();
    return lower.endsWith(".tur") || lower.endsWith(".tur.sweet")
        || lower.endsWith(".sweet");
}

}

EvalMode EvalModeForPath(const QString& path) {
    if (path.isEmpty()) return EvalMode::Buffer;  // untitled scratch buffer
    const QString name = QFileInfo(path).fileName();
    if (isBuildManifestName(name)) return EvalMode::Project;
    return hasTurmericExtension(name) ? EvalMode::Buffer : EvalMode::Disabled;
}

QString ProjectDirForPath(const QString& buildManifestPath) {
    if (buildManifestPath.isEmpty()) return {};
    const QFileInfo info(buildManifestPath);
    if (!isBuildManifestName(info.fileName())) return {};
    return info.absolutePath();
}

RunResult RunBuffer(EditorView* editor, ReplSession* repl) {
    RunResult r;
    if (!editor || !repl) {
        r.message = "Editor or REPL is not available.";
        return r;
    }
    if (!repl->isRunning()) {
        r.message = "REPL is not running — start it from Run > Restart REPL.";
        return r;
    }

    // A whole-buffer run gets a fresh env — `:run` installs one itself, and
    // the load fallback sends `:reset` first. Selections go through RunRange
    // instead and keep the current env.

    // If saved on disk and clean, run the file in place — so any error the
    // REPL reports names the user's own file rather than a scratch copy.
    if (!editor->filePath().isEmpty() && !editor->isModified()) {
        return sendWholeBuffer(repl, editor->filePath());
    }

    // Dirty or untitled — write to scratch first.
    return writeScratchAndRun(repl, editor->text(), extensionFor(editor));
}

RunResult RunRange(EditorView* editor, ReplSession* repl, int startPos, int endPos) {
    RunResult r;
    if (!editor || !repl) {
        r.message = "Editor or REPL is not available.";
        return r;
    }
    if (!repl->isRunning()) {
        r.message = "REPL is not running — start it from Run > Restart REPL.";
        return r;
    }
    if (endPos <= startPos) {
        r.message = "Empty selection.";
        return r;
    }
    QByteArray contents = editor->textInRange(startPos, endPos);
    if (contents.isEmpty()) {
        r.message = "Empty selection.";
        return r;
    }
    // A selection from mid-buffer leaves the `#lang` line behind, which would
    // run the region under a different reader than the one it was written for.
    // The extension carries the sweet base, but nothing else — not the layers,
    // not the neoteric/curly-infix bases — so re-attach the line itself unless
    // the selection already starts at the top of the file.
    if (startPos > 0) {
        const QByteArray directive = editor->langDirectiveLine();
        if (!directive.isEmpty()) contents.prepend(directive);
    }
    return writeScratchAndLoad(repl, contents, extensionFor(editor));
}

}
