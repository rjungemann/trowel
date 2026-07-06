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

    // True when the REPL is executing a command (post-sendCommand, pre-prompt).
    // False when the REPL is at its prompt awaiting user input. Determined by
    // OSC 133;A prompt markers emitted by `tur repl`; stays true if the child
    // doesn't emit them (safe default — clearRepl won't redraw the prompt).
    bool isBusy() const { return busy_; }

    // Write `line` (a UTF-8 string) followed by \r to the REPL. Returns false
    // if the REPL isn't running.
    bool sendCommand(const QByteArray& line);

    // Ask the REPL to redraw its prompt (Ctrl+L → libedit ed-clear-screen).
    // No-op when busy — we don't want to interrupt a running command.
    void redrawPrompt();

    void setTurBinary(const QString& path) { turBinary_ = path; }
    QString turBinary() const { return turBinary_; }

    // Which of the three sources supplied the resolved path.
    enum class BinarySource { Preference, Bundled, Path, None };

    struct Resolution {
        QString path;              // resolved absolute path, empty on failure
        BinarySource source = BinarySource::None;
        QString preferenceTried;   // non-empty if a preference was set but rejected
        QString bundledTried;      // non-empty if a bundled path was checked
    };

    // Runs the preference → bundled → PATH lookup. Pure, side-effect free.
    static Resolution resolveTurBinary(const QString& turBinaryName);

    // Create ~/.config/trowel/config.tur with a commented-out template if it
    // does not already exist. Called once at startup so the "Trowel Settings"
    // menu entry always opens a directory the user can see something in.
    static void ensureConfigFile();

    PtySession* pty() const { return pty_; }

signals:
    void started();
    void stopped(int exitCode);
    void dataReceived(const QByteArray& bytes);
    void busyChanged(bool busy);

private:
    void onStarted();
    void onFinished(int status);
    void onStartFailed(const QString& message);
    void onPtyData(const QByteArray& bytes);
    void setBusy(bool busy);

    TerminalView* view_;
    PtySession* pty_ = nullptr;
    QString turBinary_ = "tur";
    QString lastWorkingDir_;
    // Start busy: idle flips true only after we see the first OSC 133;A. If the
    // child never emits it (older tur, non-tty), we conservatively stay busy so
    // clearRepl doesn't try to poke the process.
    bool busy_ = true;
    // Rolling tail of unmatched bytes (partial OSC sequence spanning chunks).
    QByteArray scanTail_;
};

}
