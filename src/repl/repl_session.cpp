#include "repl/repl_session.h"

#include "repl/pty_session.h"
#include "repl/terminal_view.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>

namespace trowel {

ReplSession::ReplSession(TerminalView* view, QObject* parent)
    : QObject(parent)
    , view_(view)
{}

bool ReplSession::isRunning() const {
    return pty_ && pty_->isRunning();
}

void ReplSession::ensureConfigFile() {
    const QString dir = QDir(QDir::homePath()).filePath(".config/trowel");
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath("config.tur");
    if (QFileInfo::exists(path)) return;

    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream(&out) <<
        ";; Trowel configuration — turmeric manifest syntax.\n"
        ";; Lines starting with ';;' are comments.\n"
        ";;\n"
        ";; :tur-binary   Absolute path to the `tur` executable. Overrides the\n"
        ";;               bundled binary and PATH. Leave commented to use the\n"
        ";;               binary shipped inside Trowel.app, falling back to PATH.\n"
        ";;\n"
        ";; :tur-binary \"/absolute/path/to/tur\"\n";
}

ReplSession::Resolution ReplSession::resolveTurBinary(const QString& turBinaryName) {
    Resolution r;

    // 1. Config-file override — look for `:tur-binary "…"` in
    //    ~/.config/trowel/config.tur. Turmeric-manifest syntax, so `;;`
    //    starts a comment. We match one key, not a full sexp parse; if
    //    Trowel grows more knobs we can graduate to a real reader.
    const QString configPath = QDir(QDir::homePath()).filePath(".config/trowel/config.tur");
    QFile configFile(configPath);
    if (configFile.exists() && configFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString body = QTextStream(&configFile).readAll();
        // Strip `;;` line comments so a commented-out example doesn't win.
        static const QRegularExpression commentRe(QStringLiteral(";;[^\n]*"));
        body.remove(commentRe);
        static const QRegularExpression keyRe(
            QStringLiteral(R"re(:tur-binary\s+"([^"]+)")re"));
        const auto match = keyRe.match(body);
        if (match.hasMatch()) {
            const QString value = match.captured(1).trimmed();
            r.preferenceTried = value;
            const QFileInfo info(value);
            if (info.isFile() && info.isExecutable()) {
                r.path = info.absoluteFilePath();
                r.source = BinarySource::Preference;
                return r;
            }
        }
    }

    // 2. Bundled binary alongside the Trowel executable.
    //    macOS bundle: Trowel.app/Contents/MacOS/{trowel,tur}
    //    Non-bundle:   next to the executable, still worth a look.
    const QString appDir = QCoreApplication::applicationDirPath();
    if (!appDir.isEmpty()) {
        const QString candidate = QDir(appDir).absoluteFilePath(turBinaryName);
        r.bundledTried = candidate;
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable()) {
            r.path = info.absoluteFilePath();
            r.source = BinarySource::Bundled;
            return r;
        }
    }

    // 3. PATH lookup — current behavior.
    const QString onPath = QStandardPaths::findExecutable(turBinaryName);
    if (!onPath.isEmpty()) {
        r.path = onPath;
        r.source = BinarySource::Path;
        return r;
    }

    return r;
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

    const Resolution resolution = resolveTurBinary(turBinary_);
    if (resolution.path.isEmpty()) {
        QStringList tried;
        if (!resolution.preferenceTried.isEmpty()) {
            tried.append(QString("preference (%1)").arg(resolution.preferenceTried));
        }
        if (!resolution.bundledTried.isEmpty()) {
            tried.append(QString("bundled (%1)").arg(resolution.bundledTried));
        }
        tried.append(QString("PATH (`%1`)").arg(turBinary_));
        view_->showBanner(
            QString("[trowel] `%1` not found — tried %2. Install turmeric or set its "
                    "location in preferences. Terminal input is disabled until then.")
                .arg(turBinary_, tried.join(", ")));
        return;
    }

    if (!pty_->start(resolution.path, {"repl"}, workingDir)) {
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
