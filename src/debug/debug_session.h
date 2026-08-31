#pragma once

#include "debug/dap_client.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

namespace trowel {

// One debug run: a `tur dap` child process, the DAP handshake, and the
// state machine that drives it. Per window (not per application) — a debug
// run is bound to a window's active buffer and its REPL working directory.
//
// Lifecycle:
//
//   Idle → Starting     (process spawned, `initialize` in flight)
//        → Configuring  (`initialized` received; push setBreakpoints)
//        → Running      (`configurationDone` sent)
//        ⇄ Paused       (`stopped` / resume)
//        → Terminated   (`exited` + `terminated`, or process death)
//        → Idle
//
// There is no `attach` (constraint 2): a debug session is a separate child
// process with its own fresh environment, sibling to the REPL. State loaded
// into the REPL is not visible here.
class DebugSession : public QObject {
    Q_OBJECT
public:
    enum class State {
        Idle,
        Starting,
        Configuring,
        Running,
        Paused,
        Terminated,
    };

    // One stack frame, as `tur dap` reports it. `id` is the frame index
    // (0 = innermost); `line` is 1-based.
    struct Frame {
        int id = 0;
        QString name;
        QString filePath;
        int line = 0;
        int column = 0;
    };

    // One variable in a scope. `variablesReference` is always 0 upstream
    // (constraint 7), so there is no expansion machinery — this is a flat
    // list rendered as a string.
    struct Variable {
        QString name;
        QString value;
        QString type;
    };

    // One breakpoint to push to the adapter.
    struct BreakpointSpec {
        int line = 0;
        bool enabled = true;
        QString condition;
    };

    explicit DebugSession(QObject* parent = nullptr);
    ~DebugSession() override;

    State state() const { return state_; }
    bool isRunning() const;  // any non-Idle/Terminated state with a live process

    // Launch `program` (a .tur file path) under the interpreter with captured
    // output. `workingDir` is set on the child process — `launch` carries no
    // `cwd` (constraint 4), so this is the only lever. `stopOnEntry` pauses at
    // the entry frame instead of running to completion.
    //
    // Requires a saved file: breakpoints bind by basename (constraint 5) and
    // stack frames carry the path, so a scratch file with a mangled name would
    // silently fail to bind breakpoints. The caller is responsible for saving.
    void start(const QString& program, const QString& workingDir,
               const QStringList& extraEnv, bool stopOnEntry);

    // Resume / step. No-ops unless Paused. Stepping commands map to the DAP
    // `next` / `stepIn` / `stepOut` requests.
    void resume();
    void stepOver();
    void stepIn();
    void stepOut();

    // Push the breakpoint set for `path` to the adapter. A full replacement
    // per source — exactly what `tur dap` expects (it clears the file's set
    // before adding the new one). Mid-run requests are not processed until
    // the next stop (constraint 6); send during Configuring and on each stop.
    void setBreakpoints(const QString& path, const QVector<BreakpointSpec>& bps);

    // Stop the session. While paused, sends `terminate` (the adapter
    // `_exit(0)`s — the real exit code is lost, constraint 10). While
    // running, the adapter is not reading stdin (constraint 6), so the only
    // option is to kill the process.
    void stop();

    // The frames / variables from the last `stopped` event. Valid while
    // Paused; cleared on resume and on terminate.
    const QVector<Frame>& frames() const { return frames_; }
    const QVector<Variable>& variables() const { return variables_; }

    // Debuggee output accumulated from `output` events since the session
    // began (or since `clearOutput`). Plain text — DAP `output` events carry
    // a category but no ANSI stream, and there is no debuggee stdin.
    const QString& output() const { return output_; }
    void clearOutput();

    // Exit code from the `exited` event. -1 until the program exits. A
    // user-stopped session reports -1 (the adapter `_exit(0)`s on
    // disconnect, so 0 would be a lie — report "stopped by user" instead).
    int exitCode() const { return exitCode_; }

signals:
    void stateChanged(State state);
    // A chunk of debuggee stdout arrived (category "stdout" or "console").
    void outputReceived(const QString& text);
    // The program stopped (breakpoint / step / entry / pause). `reason` is the
    // DAP reason string. Frames are already refreshed when this fires.
    void stopped(const QString& reason);
    // The program resumed running.
    void resumed();
    // The session is ready to receive breakpoints: during Configuring (before
    // configurationDone) and on each stop. The owner pushes the model's
    // breakpoint set for the program file in response. Mid-run requests are
    // not processed until the next stop (constraint 6), so this is the only
    // reliable send window.
    void pushBreakpointsRequested();
    // The program exited. `code` is the real exit code when available, -1
    // when the session was stopped by the user (constraint 10).
    void programExited(int code);
    // The adapter process itself died or failed to start. `message` is a
    // short diagnostic for the status bar.
    void sessionFailed(const QString& message);

private:
    void setState(State s);
    void onEvent(const QString& event, const QJsonObject& body);
    void onInitialize();
    void onInitialized();
    void onStopped(const QJsonObject& body);
    void refreshFrames();
    void refreshVariables(int frameId);
    void finishTerminated(int code, bool userStopped);

    DapClient* client_;
    State state_ = State::Idle;
    QString program_;
    bool stopOnEntry_ = false;
    bool userStopped_ = false;
    int exitCode_ = -1;
    int selectedFrameId_ = 0;
    QVector<Frame> frames_;
    QVector<Variable> variables_;
    QString output_;
};

}
