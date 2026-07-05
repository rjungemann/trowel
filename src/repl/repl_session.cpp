#include "repl/repl_session.h"

#include "repl/pty_session.h"
#include "repl/terminal_view.h"

#include <QStandardPaths>

namespace trowel {

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

    const QString resolved = QStandardPaths::findExecutable(turBinary_);
    if (resolved.isEmpty()) {
        view_->showBanner(
            QString("[trowel] `%1` not found on PATH — install turmeric or set its location "
                    "in preferences. Terminal input is disabled until then.")
                .arg(turBinary_));
        return;
    }

    if (!pty_->start(resolved, {"repl"}, workingDir)) {
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
