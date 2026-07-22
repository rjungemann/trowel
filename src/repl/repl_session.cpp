#include "repl/repl_session.h"

#include "repl/pty_session.h"
#include "repl/terminal_view.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace trowel {

namespace {

QString ifExecutable(const QString& path) {
    if (path.isEmpty()) return {};
    const QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile() || !fi.isExecutable()) return {};
    return fi.absoluteFilePath();
}

// Path to the bundled `tur` shipped inside Trowel.app. Empty string on dev
// builds where no binary was staged (falls through to PATH).
QString bundledTurPath() {
    const QString appDir = QCoreApplication::applicationDirPath();
#ifdef Q_OS_MACOS
    return QDir::cleanPath(appDir + "/../Resources/turmeric/tur");
#else
    return QDir::cleanPath(appDir + "/turmeric/tur");
#endif
}

} // namespace

QString ResolveTurBinary() {
    const QString override = QSettings().value("repl/turBinary").toString();
    QString resolved = ifExecutable(override);
    if (resolved.isEmpty()) resolved = ifExecutable(bundledTurPath());
    if (resolved.isEmpty()) resolved = QStandardPaths::findExecutable("tur");
    return resolved;
}

ReplSession::ReplSession(TerminalView* view, QObject* parent)
    : QObject(parent)
    , view_(view)
{}

bool ReplSession::isRunning() const {
    return pty_ && pty_->isRunning();
}

void ReplSession::start(const QString& workingDir) {
    if (isRunning()) return;

    lastWorkingDir_ = workingDir;

    if (pty_) {
        pty_->deleteLater();
        pty_ = nullptr;
    }
    pty_ = new PtySession(this);
    view_->attach(pty_);

    connect(pty_, &PtySession::finished, this, &ReplSession::onFinished);
    connect(pty_, &PtySession::startFailed, this, &ReplSession::onStartFailed);
    connect(pty_, &PtySession::dataReceived, this, &ReplSession::onPtyData);

    // Each REPL launch starts busy; the first prompt marker flips us idle.
    busy_ = true;
    scanTail_.clear();

    // Resolution order:
    //   1. QSettings "repl/turBinary" — user override (absolute path).
    //   2. Bundled binary inside Trowel.app (drag-install path).
    //   3. `tur` on the user's PATH (dev builds, homebrew, mise, …).
    const QString override = QSettings().value("repl/turBinary").toString();
    const QString bundled = bundledTurPath();
    const QString onPath = QStandardPaths::findExecutable(turBinary_);

    QString resolved = ifExecutable(override);
    if (resolved.isEmpty()) resolved = ifExecutable(bundled);
    if (resolved.isEmpty()) resolved = onPath;

    if (resolved.isEmpty()) {
        QString msg = QString("[trowel] could not locate `%1`. Tried:").arg(turBinary_);
        msg += QString("\n  1. QSettings repl/turBinary = %1")
                   .arg(override.isEmpty() ? QStringLiteral("(unset)") : override);
        msg += QString("\n  2. bundled = %1").arg(bundled);
        msg += QString("\n  3. PATH lookup for `%1`").arg(turBinary_);
        msg += "\nTerminal input is disabled until this is resolved.";
        view_->showBanner(msg);
        return;
    }

    // Pin the stdlib to whichever `tur` we resolved: each release ships `tur`
    // and `stdlib/` side by side, so use the stdlib next to the chosen binary.
    // This keeps binary and stdlib in the same version even when the ambient
    // environment (e.g. a mise `TUR_STDLIB_DIR` export pinning an older
    // toolchain) would otherwise leak in and cause a version mismatch. Only
    // override when that sibling stdlib actually exists — a bare shim without an
    // adjacent stdlib falls through to the inherited environment.
    QStringList extraEnv;
    const QString siblingStdlib =
        QFileInfo(resolved).absolutePath() + QStringLiteral("/stdlib");
    if (QDir(siblingStdlib).exists()) {
        extraEnv << QStringLiteral("TUR_STDLIB_DIR=") + siblingStdlib;
    }

    if (!pty_->start(resolved, {"repl"}, workingDir, extraEnv)) {
        // startFailed will fire and report.
        return;
    }
    onStarted();
}

void ReplSession::restart(const QString& workingDir) {
    stop();
    view_->showBanner("[trowel] restarting REPL…");
    start(workingDir.isEmpty() ? lastWorkingDir_ : workingDir);
}

void ReplSession::stop() {
    if (!pty_) return;
    pty_->terminate();
}

bool ReplSession::sendCommand(const QByteArray& line) {
    if (!isRunning()) return false;
    QByteArray payload = line;
    if (!payload.endsWith('\r') && !payload.endsWith('\n')) payload.append('\r');
    // Optimistically enter busy state. The next OSC 133;A from the REPL flips
    // back to idle. If the child doesn't emit markers we simply stay busy,
    // which just disables clearRepl's prompt-redraw and hurts nothing else.
    setBusy(true);
    pty_->write(payload);
    return true;
}

void ReplSession::redrawPrompt() {
    if (!isRunning() || busy_) return;
    // Ctrl+L = libedit's ed-clear-screen: emits terminal clear codes (which
    // TerminalView drops as unknown CSI) then redraws the prompt with any
    // in-progress input. Trowel has already wiped its own document; this just
    // brings the prompt line back.
    pty_->write(QByteArray("\x0c"));
}

void ReplSession::setBusy(bool busy) {
    if (busy_ == busy) return;
    busy_ = busy;
    emit busyChanged(busy_);
}

// Scan a byte stream for OSC 133;A (prompt-start) markers. On match, flip idle.
// Handles chunk boundaries by carrying an unterminated OSC tail into scanTail_.
// Format: ESC ] 1 3 3 ; A  {BEL | ESC \}
void ReplSession::onPtyData(const QByteArray& bytes) {
    static constexpr char kPromptA[] = "\x1b]133;A";
    static constexpr int kPromptALen = sizeof(kPromptA) - 1;

    QByteArray buf = scanTail_ + bytes;
    scanTail_.clear();

    int i = 0;
    while ((i = buf.indexOf(kPromptA, i)) >= 0) {
        int term = i + kPromptALen;
        if (term >= buf.size()) break; // partial — need more bytes
        char t = buf.at(term);
        if (t == '\x07') {
            setBusy(false);
            i = term + 1;
        } else if (t == '\x1b') {
            if (term + 1 >= buf.size()) break; // partial ST
            if (buf.at(term + 1) == '\\') {
                setBusy(false);
                i = term + 2;
            } else {
                i = term + 1;
            }
        } else {
            i = term;
        }
    }

    // Preserve any trailing partial ESC sequence for the next chunk. Cap the
    // carry so a stream full of stray ESCs can't grow unbounded.
    int lastEsc = buf.lastIndexOf('\x1b');
    if (lastEsc >= 0 && buf.size() - lastEsc <= 32) {
        scanTail_ = buf.mid(lastEsc);
    }

    emit dataReceived(bytes);
}

void ReplSession::onStarted() {
    view_->showBanner(QString("[trowel] %1 repl started").arg(turBinary_));
    emit started();
}

void ReplSession::onFinished(int status) {
    view_->showBanner(QString("[trowel] repl exited with code %1").arg(status));
    view_->detach();
    emit stopped(status);
}

void ReplSession::onStartFailed(const QString& message) {
    view_->showBanner(QString("[trowel] failed to start: %1").arg(message));
    emit stopped(-1);
}

}
