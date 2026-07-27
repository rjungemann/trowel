#include "lsp/lsp_transport.h"

#include <QJsonDocument>
#include <QJsonParseError>
#include <QProcess>
#include <QProcessEnvironment>

namespace trowel {

namespace {

// A single LSP message larger than this means the stream desynced — a bogus
// Content-Length read out of the middle of a body. Bail rather than try to
// allocate it. Real messages are kilobytes; whole-file didChange bodies for a
// pathological source file are still far under this.
constexpr int kMaxMessageBytes = 64 * 1024 * 1024;

// Header block with no terminator this long is likewise a desync.
constexpr int kMaxHeaderBytes = 8 * 1024;

constexpr int kTerminateGraceMs = 500;

} // namespace

LspTransport::LspTransport(QObject* parent)
    : QObject(parent)
{}

LspTransport::~LspTransport() {
    // Don't leave an orphaned server behind when the app tears down.
    terminate();
}

bool LspTransport::isRunning() const {
    return proc_ && proc_->state() != QProcess::NotRunning;
}

bool LspTransport::start(const QString& program, const QStringList& args,
                         const QString& workingDir, const QStringList& extraEnv) {
    if (isRunning()) return true;

    if (proc_) {
        proc_->deleteLater();
        proc_ = nullptr;
    }
    buf_.clear();

    proc_ = new QProcess(this);
    proc_->setProcessChannelMode(QProcess::SeparateChannels);
    if (!workingDir.isEmpty()) proc_->setWorkingDirectory(workingDir);

    if (!extraEnv.isEmpty()) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (const QString& entry : extraEnv) {
            const int eq = entry.indexOf('=');
            if (eq > 0) env.insert(entry.left(eq), entry.mid(eq + 1));
        }
        proc_->setProcessEnvironment(env);
    }

    connect(proc_, &QProcess::readyReadStandardOutput, this, &LspTransport::onReadyRead);
    connect(proc_, &QProcess::readyReadStandardError, this, &LspTransport::onReadyReadStderr);
    connect(proc_, &QProcess::started, this, &LspTransport::started);
    connect(proc_, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus) { emit finished(exitCode); });
    connect(proc_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            emit startFailed(proc_ ? proc_->errorString()
                                   : QStringLiteral("failed to start"));
        }
    });

    proc_->start(program, args);
    // Don't waitForStarted here — QProcess::started / errorOccurred report
    // asynchronously and the UI thread must not block on the server.
    return true;
}

void LspTransport::send(const QJsonObject& msg) {
    if (!isRunning()) return;
    const QByteArray body = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    QByteArray frame = "Content-Length: ";
    frame += QByteArray::number(body.size());
    frame += "\r\n\r\n";
    frame += body;
    proc_->write(frame);
}

void LspTransport::terminate() {
    if (!proc_) return;
    if (proc_->state() != QProcess::NotRunning) {
        proc_->closeWriteChannel();
        proc_->terminate();
        if (!proc_->waitForFinished(kTerminateGraceMs)) {
            proc_->kill();
            proc_->waitForFinished(kTerminateGraceMs);
        }
    }
}

void LspTransport::onReadyRead() {
    if (!proc_) return;
    buf_ += proc_->readAllStandardOutput();
    drainBuffer();
}

void LspTransport::onReadyReadStderr() {
    if (!proc_) return;
    // The server writes diagnostics chatter here. Drain it so the pipe can't
    // fill and deadlock the child, but don't surface it as editor state.
    const QByteArray err = proc_->readAllStandardError();
    if (!err.isEmpty()) qWarning("[lsp] %s", err.trimmed().constData());
}

void LspTransport::drainBuffer() {
    for (;;) {
        const int headerEnd = buf_.indexOf("\r\n\r\n");
        if (headerEnd < 0) {
            if (buf_.size() > kMaxHeaderBytes) {
                failStream(QStringLiteral("no header terminator in %1 bytes")
                               .arg(buf_.size()));
            }
            return;
        }

        // Only Content-Length matters; Content-Type is optional and ignored.
        int contentLength = -1;
        const QByteArray header = buf_.left(headerEnd);
        for (const QByteArray& line : header.split('\n')) {
            const QByteArray trimmed = line.trimmed();
            if (!trimmed.startsWith("Content-Length:")) continue;
            bool ok = false;
            contentLength = trimmed.mid(int(strlen("Content-Length:"))).trimmed().toInt(&ok);
            if (!ok) contentLength = -1;
            break;
        }

        if (contentLength < 0 || contentLength > kMaxMessageBytes) {
            failStream(QStringLiteral("bad Content-Length (%1)").arg(contentLength));
            return;
        }

        const int bodyStart = headerEnd + 4;
        if (buf_.size() - bodyStart < contentLength) return;  // wait for the rest

        const QByteArray body = buf_.mid(bodyStart, contentLength);
        buf_.remove(0, bodyStart + contentLength);

        QJsonParseError err{};
        const QJsonDocument doc = QJsonDocument::fromJson(body, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            // A single malformed body doesn't desync the stream — the framing
            // already told us where it ended — so drop it and keep going.
            qWarning("[lsp] dropping unparseable message: %s",
                     qPrintable(err.errorString()));
            continue;
        }
        emit messageReceived(doc.object());
    }
}

void LspTransport::failStream(const QString& why) {
    buf_.clear();
    const QString msg = QStringLiteral("protocol desync: ") + why;
    terminate();
    emit startFailed(msg);
}

}
