#pragma once

#include <QObject>
#include <QString>

namespace trowel {

class PtySession;
class TerminalView;

class ReplSession : public QObject {
    Q_OBJECT
public:
    explicit ReplSession(TerminalView* view, QObject* parent = nullptr);

    void start(const QString& workingDir = {});
    void restart(const QString& workingDir = {});
    void stop();
    bool isRunning() const;

    // Write `line` (a UTF-8 string) followed by \r to the REPL. Returns false
    // if the REPL isn't running.
    bool sendCommand(const QByteArray& line);

    void setTurBinary(const QString& path) { turBinary_ = path; }
    QString turBinary() const { return turBinary_; }

signals:
    void started();
    void stopped(int exitCode);

private:
    void onStarted();
    void onFinished(int status);
    void onStartFailed(const QString& message);

    TerminalView* view_;
    PtySession* pty_ = nullptr;
    QString turBinary_ = "tur";
    QString lastWorkingDir_;
};

}
