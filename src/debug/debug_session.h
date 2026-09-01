#pragma once

#include "debug/dap_client.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>

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
    // `replay` records the whole run first and then serves the session from
    // the recording (`launch` with `"replay": true`). That inverts the
    // lifecycle: `launch` takes as long as the program does, and every
    // subsequent question is answered from a trace cursor rather than from a
    // live interpreter. Reverse execution only works in this mode; `evaluate`
    // only works outside it.
    void start(const QString& program, const QString& workingDir,
               const QStringList& extraEnv, bool stopOnEntry, bool replay = false);

    // Whether this session is serving a recording. Drives which controls the
    // view offers — the two capabilities are exclusive, so this is not a
    // detail the UI can leave out.
    bool isReplay() const { return replay_; }

    // The file this session is debugging. The breakpoint set that gets pushed
    // belongs to this path, not to whatever buffer is in front.
    const QString& program() const { return program_; }

    // Whether this session was launched to pause at the entry frame. Read on
    // restart, so a respawn behaves the same way the session it replaces did
    // rather than reverting to the run-to-completion default.
    bool stopsOnEntry() const { return stopOnEntry_; }

    // Resume / step. No-ops unless Paused. Stepping commands map to the DAP
    // `next` / `stepIn` / `stepOut` requests.
    void resume();
    void stepOver();
    void stepIn();
    void stepOut();

    // Reverse execution, served from a recording. No-ops unless Paused *and*
    // in replay: a live interpreter cannot run backwards, and the adapter
    // answers "not supported while paused" rather than pretending.
    //
    // The adapter maps replay steps back onto *lines* for all four of
    // stepIn/next/stepBack/reverseNext, deliberately — an editor draws a line
    // marker, and four keypresses that leave it in place read as a hung
    // debugger.
    void stepBack();
    void reverseStepOver();
    void reverseContinue();

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

    // The frame the inspection panes are showing. `id` is the frame index,
    // 0 = innermost (constraint 11), so selection maps straight onto the list
    // row. Selecting re-issues `scopes` + `variables` for that frame and
    // emits `variablesUpdated` when they land.
    int selectedFrameId() const { return selectedFrameId_; }
    void selectFrame(int frameId);

    // Evaluate `expression` against the selected frame. `cb` is called once
    // with the rendered result, or with `ok == false` and the adapter's error
    // message. A no-op unless Paused.
    //
    // `frameId` is clamped to 0 by the adapter when negative, and `evaluate`
    // refuses outright in a recording — there is no live frame to evaluate
    // against. The refusal arrives as a normal error, so callers get the
    // adapter's own wording rather than a guess.
    using EvaluateCallback = std::function<void(bool ok, const QString& text)>;
    void evaluate(const QString& expression, EvaluateCallback cb);

    // Debuggee output accumulated from `output` events since the session
    // began (or since `clearOutput`). Plain text — DAP `output` events carry
    // a category but no ANSI stream, and there is no debuggee stdin.
    const QString& output() const { return output_; }
    void clearOutput();

    // Exit code from the `exited` event. -1 until the program exits. A
    // user-stopped session reports -1 (the adapter `_exit(0)`s on
    // disconnect, so 0 would be a lie — report "stopped by user" instead).
    int exitCode() const { return exitCode_; }

    // How many times this session has stopped, counting from 0 at launch.
    //
    // Stepping does not change `state()` — a step is Paused → Paused — so the
    // state machine cannot answer "has my step landed yet?", and neither can
    // the frame contents, since a step often stays on the same line. This
    // counter is the only thing that can. It increments immediately before
    // `stopped` is emitted, so a slot on that signal already sees the new
    // value.
    int stopCount() const { return stopCount_; }

signals:
    void stateChanged(State state);
    // A chunk of debuggee stdout arrived (category "stdout" or "console").
    void outputReceived(const QString& text);
    // The program stopped (breakpoint / step / entry / pause). `reason` is the
    // DAP reason string.
    //
    // Deferred until `stackTrace` and the innermost frame's `variables` have
    // both answered, so `frames()` and `variables()` are populated when a slot
    // reads them. The obvious spelling — emit on the event, refresh in the
    // background — makes every consumer read an empty frame list exactly once
    // per stop, which is a marker that never appears and a variables pane that
    // is always one stop behind.
    void stopped(const QString& reason);
    // The frame list changed (a new stop). Separate from `stopped` so a view
    // can repaint without caring why.
    void framesUpdated();
    // The variables for the selected frame changed — a new stop, or the user
    // picking a different frame.
    void variablesUpdated();
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
    // Both take a continuation rather than returning: they are two and three
    // round trips respectively, and the callers all need to act *after* the
    // last one lands.
    void refreshFrames(std::function<void()> done);
    void refreshVariables(int frameId, std::function<void()> done);
    void finishTerminated(int code, bool userStopped);

    DapClient* client_;
    State state_ = State::Idle;
    QString program_;
    bool stopOnEntry_ = false;
    bool replay_ = false;
    bool userStopped_ = false;
    int exitCode_ = -1;
    int stopCount_ = 0;
    int selectedFrameId_ = 0;
    QVector<Frame> frames_;
    QVector<Variable> variables_;
    QString output_;
};

}
