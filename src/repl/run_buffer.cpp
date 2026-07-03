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

QString extensionFor(const EditorView* editor) {
    const QString path = editor->filePath();
    if (path.endsWith(".tur.sweet")) return ".tur.sweet";
    if (path.endsWith(".sweet"))     return ".tur.sweet";
    return ".tur";
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

RunResult writeScratchAndLoad(ReplSession* repl,
                              const QByteArray& contents, const QString& ext) {
    RunResult r;
    const QString dir = scratchDir();
    const quint32 stamp = QRandomGenerator::global()->generate();
    const QString path = QString("%1/buffer-%2%3")
        .arg(dir)
        .arg(stamp, 8, 16, QChar('0'))
        .arg(ext);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        r.message = QString("Could not write scratch file at %1").arg(path);
        return r;
    }
    file.write(contents);
    file.close();

    const QString escaped = escapeForTurmericString(path);
    const QByteArray cmd = QString("(load \"%1\")").arg(escaped).toUtf8();
    if (!repl->sendCommand(cmd)) {
        r.message = "REPL is not running.";
        return r;
    }
    r.ok = true;
    r.scratchPath = path;
    r.message = QString("Loaded %1").arg(QFileInfo(path).fileName());
    return r;
}

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

    // If saved on disk and clean, load the file in place.
    if (!editor->filePath().isEmpty() && !editor->isModified()) {
        const QString escaped = escapeForTurmericString(editor->filePath());
        const QByteArray cmd = QString("(load \"%1\")").arg(escaped).toUtf8();
        if (!repl->sendCommand(cmd)) {
            r.message = "REPL is not running.";
            return r;
        }
        r.ok = true;
        r.message = QString("Loaded %1").arg(QFileInfo(editor->filePath()).fileName());
        return r;
    }

    // Dirty or untitled — write to scratch first.
    return writeScratchAndLoad(repl, editor->text(), extensionFor(editor));
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
    const QByteArray contents = editor->textInRange(startPos, endPos);
    if (contents.isEmpty()) {
        r.message = "Empty selection.";
        return r;
    }
    return writeScratchAndLoad(repl, contents, extensionFor(editor));
}

}
