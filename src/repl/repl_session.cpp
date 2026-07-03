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
    connect(pty_, &PtySession::dataReceived, this, &ReplSession::dataReceived);

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
    pty_->write(payload);
    return true;
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
