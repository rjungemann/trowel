#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QStringList>

class QProcess;

namespace trowel {

// Speaks LSP base-protocol framing (`Content-Length: N\r\n\r\n<body>`) to a
// child process over its stdin/stdout.
//
// Deliberately NOT built on PtySession like ReplSession is. The REPL needs a
// tty for libedit; a language server needs a clean byte stream, and a pty would
// corrupt it (ONLCR would rewrite \n as \r\n inside JSON payloads). QProcess
// with plain pipes is the right transport here.
//
// Framing is chunk-boundary safe: reads accumulate into buf_ and only whole
// messages are emitted, the same carry-the-partial-tail idiom ReplSession uses
// for its OSC scans.
class LspTransport : public QObject {
    Q_OBJECT
public:
    explicit LspTransport(QObject* parent = nullptr);
    ~LspTransport() override;

    // `extraEnv` holds "KEY=VALUE" entries applied over the inherited
    // environment — used to pin TUR_STDLIB_DIR, matching ReplSession::start.
    bool start(const QString& program, const QStringList& args,
               const QString& workingDir = {}, const QStringList& extraEnv = {});
    bool isRunning() const;

    void send(const QJsonObject& msg);

    // Ask the child to exit, then kill it if it doesn't. Safe to call when not
    // running.
    void terminate();

signals:
    void messageReceived(const QJsonObject& msg);
    void started();
    void finished(int exitCode);
    // Emitted when the child could not be launched, or when the stream desynced
    // badly enough that we gave up on it.
    void startFailed(const QString& message);

private:
    void onReadyRead();
    void onReadyReadStderr();
    void drainBuffer();
    // Tear down after a framing error — the stream cannot be resynchronized
    // once we've lost the header boundary.
    void failStream(const QString& why);

    QProcess* proc_ = nullptr;
    QByteArray buf_;
};

}
