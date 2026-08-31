#include "debug/debug_session.h"

#include "repl/repl_session.h"  // ResolveTurBinary

#include <QJsonArray>

namespace trowel {

namespace {
// How long to wait for the adapter to answer `initialize` before concluding
// the bundled `tur` has no DAP server (version skew — see the plan's risks).
constexpr int kInitializeTimeoutMs = 5000;
}  // namespace

DebugSession::DebugSession(QObject* parent)
    : QObject(parent)
    , client_(new DapClient(this))
{
    connect(client_, &DapClient::eventReceived, this, &DebugSession::onEvent);
    connect(client_, &DapClient::startFailed, this, [this](const QString& why) {
        setState(State::Idle);
        emit sessionFailed(why);
    });
    connect(client_, &DapClient::finished, this, [this](int code) {
        // The adapter process died. If we were mid-run, this is the same as
        // the program exiting (the adapter exits with the debuggee's code);
        // if we were still starting, it's a failure.
        if (state_ == State::Idle || state_ == State::Terminated) return;
        finishTerminated(code, userStopped_);
    });
}

DebugSession::~DebugSession() {
    stop();
}

bool DebugSession::isRunning() const {
    return state_ != State::Idle && state_ != State::Terminated && client_->isRunning();
}

void DebugSession::start(const QString& program, const QString& workingDir,
                         const QStringList& extraEnv, bool stopOnEntry) {
    if (state_ != State::Idle && state_ != State::Terminated) return;
    program_ = program;
    stopOnEntry_ = stopOnEntry;
    userStopped_ = false;
    exitCode_ = -1;
    frames_.clear();
    variables_.clear();
    output_.clear();

    const QString tur = ResolveTurBinary();
    if (tur.isEmpty()) {
        emit sessionFailed(QStringLiteral("could not locate the `tur` binary"));
        return;
    }

    setState(State::Starting);
    if (!client_->start(tur, {"dap"}, workingDir, extraEnv)) {
        setState(State::Idle);
        emit sessionFailed(QStringLiteral("could not start `tur dap`"));
        return;
    }
    onInitialize();
}

void DebugSession::onInitialize() {
    // `tur dap` answers initialize with its capabilities, then an
    // `initialized` event. We send `launch` once initialize succeeds, then
    // wait for the `initialized` event before sending configurationDone.
    client_->request(
        "initialize",
        QJsonObject{
            {"clientID", "trowel"},
            {"adapterID", "tur"},
            {"linesStartAt1", true},
            {"columnsStartAt1", true},
            {"pathFormat", "path"},
        },
        [this](const QJsonValue& body, const DapError* err) {
            if (err) {
                stop();
                setState(State::Idle);
                emit sessionFailed(err->message);
                return;
            }
            (void)body;
            // Launch the program. `launch` carries no `cwd` (constraint 4);
            // the working directory was set on the child process at spawn.
            QJsonObject args{
                {"program", program_},
                {"stopOnEntry", stopOnEntry_},
            };
            client_->request("launch", args,
                [this](const QJsonValue&, const DapError* err) {
                    if (err) {
                        stop();
                        emit sessionFailed(err->message);
                    }
                    // The `initialized` event drives configurationDone next.
                });
        },
        kInitializeTimeoutMs);
}

void DebugSession::onInitialized() {
    // Push breakpoints (none in phase 1 — phase 3 adds the model) and send
    // configurationDone, which starts the program.
    setState(State::Configuring);
    // The owner pushes the program file's breakpoints now, before
    // configurationDone — the only guaranteed-processed send window.
    emit pushBreakpointsRequested();
    client_->request("configurationDone", QJsonObject{},
        [this](const QJsonValue&, const DapError* err) {
            if (err) {
                stop();
                emit sessionFailed(err->message);
                return;
            }
            // The program is now running. If stopOnEntry was set, a `stopped`
            // event with reason "entry" will arrive and flip us to Paused;
            // otherwise we stay Running until a breakpoint/step or the program
            // exits.
            setState(State::Running);
        });
}

void DebugSession::onEvent(const QString& event, const QJsonObject& body) {
    if (event == "initialized") {
        onInitialized();
        return;
    }
    if (event == "stopped") {
        onStopped(body);
        return;
    }
    if (event == "output") {
        const QString text = body.value("output").toString();
        if (!text.isEmpty()) {
            output_ += text;
            emit outputReceived(text);
        }
        return;
    }
    if (event == "exited") {
        const int code = body.value("exitCode").toInt(-1);
        finishTerminated(code, userStopped_);
        return;
    }
    if (event == "terminated") {
        finishTerminated(exitCode_, userStopped_);
        return;
    }
    // `thread` and other events are not relevant to a single-threaded adapter.
}

void DebugSession::onStopped(const QJsonObject& body) {
    const QString reason = body.value("reason").toString();
    setState(State::Paused);
    // Re-push breakpoints now that the adapter is reading stdin again —
    // mid-run setBreakpoints is not processed until the next stop (constraint 6).
    emit pushBreakpointsRequested();
    refreshFrames();
    emit stopped(reason);
}

void DebugSession::refreshFrames() {
    client_->request(
        "stackTrace", QJsonObject{{"threadId", 1}},
        [this](const QJsonValue& body, const DapError* err) {
            frames_.clear();
            if (err) { emit stopped({}); return; }
            const QJsonArray arr = body.toObject().value("stackFrames").toArray();
            for (const QJsonValue& f : arr) {
                const QJsonObject fo = f.toObject();
                Frame frame;
                frame.id = fo.value("id").toInt(0);
                frame.name = fo.value("name").toString();
                frame.line = fo.value("line").toInt(0);
                frame.column = fo.value("column").toInt(0);
                const QJsonObject src = fo.value("source").toObject();
                frame.filePath = src.value("path").toString();
                frames_.append(frame);
            }
            if (!frames_.isEmpty()) {
                selectedFrameId_ = frames_.first().id;
                refreshVariables(selectedFrameId_);
            }
        });
}

void DebugSession::refreshVariables(int frameId) {
    // `scopes` returns one Locals scope whose reference is frameId + 1
    // (constraint 7). `variables` against that reference returns the flat
    // variable list.
    client_->request(
        "scopes", QJsonObject{{"frameId", frameId}},
        [this, frameId](const QJsonValue& body, const DapError* err) {
            if (err) return;
            const QJsonArray scopes = body.toObject().value("scopes").toArray();
            int varRef = 0;
            for (const QJsonValue& s : scopes) {
                const QJsonObject so = s.toObject();
                if (so.value("name").toString() == "Locals" ||
                    so.value("variablesReference").toInt(0) != 0) {
                    varRef = so.value("variablesReference").toInt(0);
                    break;
                }
            }
            if (varRef == 0) {
                variables_.clear();
                return;
            }
            client_->request(
                "variables", QJsonObject{{"variablesReference", varRef}},
                [this](const QJsonValue& body, const DapError* err) {
                    variables_.clear();
                    if (err) return;
                    const QJsonArray arr = body.toObject().value("variables").toArray();
                    for (const QJsonValue& v : arr) {
                        const QJsonObject vo = v.toObject();
                        Variable var;
                        var.name = vo.value("name").toString();
                        var.value = vo.value("value").toString();
                        var.type = vo.value("type").toString();
                        variables_.append(var);
                    }
                });
            (void)frameId;
        });
}

void DebugSession::resume() {
    if (state_ != State::Paused) return;
    client_->request("continue", QJsonObject{{"threadId", 1}},
        [this](const QJsonValue&, const DapError* err) {
            if (err) return;
            setState(State::Running);
            frames_.clear();
            variables_.clear();
            emit resumed();
        });
}

void DebugSession::stepOver() {
    if (state_ != State::Paused) return;
    client_->request("next", QJsonObject{{"threadId", 1}}, nullptr);
}

void DebugSession::stepIn() {
    if (state_ != State::Paused) return;
    client_->request("stepIn", QJsonObject{{"threadId", 1}}, nullptr);
}

void DebugSession::stepOut() {
    if (state_ != State::Paused) return;
    client_->request("stepOut", QJsonObject{{"threadId", 1}}, nullptr);
}

void DebugSession::setBreakpoints(const QString& path,
                                  const QVector<BreakpointSpec>& bps) {
    // `tur dap` matches breakpoints by basename (constraint 5), but the
    // request carries the full source.path — the adapter reduces it. We send
    // the absolute path; the basename collision is surfaced client-side by
    // the model.
    QJsonArray arr;
    for (const BreakpointSpec& b : bps) {
        QJsonObject bp{
            {"line", b.line},
        };
        if (!b.condition.isEmpty()) bp.insert("condition", b.condition);
        arr.append(bp);
    }
    QJsonObject args{
        {"source", QJsonObject{{"path", path}}},
        {"breakpoints", arr},
    };
    client_->request("setBreakpoints", args, nullptr);
}

void DebugSession::stop() {
    if (state_ == State::Idle || state_ == State::Terminated) {
        client_->stop();
        return;
    }
    // While paused, `terminate` makes the adapter `_exit(0)`. While running,
    // the adapter is not reading stdin (constraint 6), so we have to kill.
    userStopped_ = true;
    if (state_ == State::Paused && client_->isRunning()) {
        client_->request("terminate", QJsonObject{}, nullptr, 1000);
    }
    client_->stop();
    finishTerminated(-1, true);
}

void DebugSession::clearOutput() {
    output_.clear();
}

void DebugSession::finishTerminated(int code, bool userStopped) {
    const bool wasLive = state_ != State::Idle && state_ != State::Terminated;
    setState(State::Terminated);
    frames_.clear();
    variables_.clear();
    client_->stop();
    exitCode_ = userStopped ? -1 : code;
    if (wasLive) emit programExited(exitCode_);
    setState(State::Idle);
}

void DebugSession::setState(State s) {
    if (state_ == s) return;
    state_ = s;
    emit stateChanged(s);
}

}
