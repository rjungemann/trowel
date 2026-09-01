#pragma once

#include "repl/run_buffer.h"

#include <QObject>
#include <QString>

class QProcess;

namespace trowel {

class TerminalView;

// What `tur trace` reported, parsed out of its summary line.
//
// The summary is the only reliable signal. **The exit code is not usable**,
// measured against the staged v0.42.0 binary:
//
//   a 50k-iteration loop        → exit 0,   100004 steps
//   a program whose main is 144 → exit 144, 2 steps
//   a file with no `main`       → exit 0,   0 steps
//   a file that does not compile→ exit 1,   0 steps, and a summary anyway
//
// `tur trace` propagates the traced program's own return value, so a non-zero
// exit means "your program returned that" at least as often as it means
// failure. Everything below is derived from the text instead.
struct TraceSummary {
    bool parsed = false;
    int steps = 0;
    int enters = 0;
    int pops = 0;
    int changes = 0;
    int peakDepth = 0;
    int bytes = 0;
    int outputBytes = 0;
    bool truncated = false;
    // "per expression", "per line", or empty for a server too old to say.
    //
    // Turmeric `e7140c97c` moved the recorder from line granularity to
    // expression granularity and put the unit in the summary line. It changes
    // what a *low step count means*, which is the one thing this class exists
    // to explain — so it is parsed rather than ignored. Empty is treated as
    // "per line": every binary that omits it predates the change.
    QString granularity;

    bool perLine() const { return granularity != QLatin1String("per expression"); }
};

// Why a recording came out the way it did.
//
// A low step count is routinely *not* about the program. Against a `tur` that
// records per source line (v0.42.0 and earlier), a form written on one line
// collapses to a single step however much it evaluates — measured: the same
// `fib 12` records 2 steps written one-form-per-line and 1395 steps spread
// across lines, peak depth 1 versus 12. Turmeric `e7140c97c` moved the
// recorder to per-expression granularity and made the unit explicit in the
// summary; until Trowel's pin moves past it, the collapse is what a user will
// meet.
//
// So the UI speaks from an outcome rather than printing a number: "2 steps" on
// its own is exactly what makes someone conclude the tracer is broken, and the
// honest reading of it depends on which granularity produced it.
enum class TraceOutcome {
    Failed,          // could not start, or no toolchain
    CompileError,    // the file does not compile; there is nothing to replay
    NoMain,          // nothing to enter — `tur trace` records `(main)`
    ShortRecording,  // compiled frames record nothing; this ran but barely
    Recorded,        // a real recording
};

// Does this source define a top-level `main`?
//
// The one rule two features both need. `tur trace` records what `(main)`
// evaluates and `tur dap` only instruments the same call, so a file whose work
// happens at the top level records nothing and stops nowhere — measured: with
// `stopOnEntry` *and* a breakpoint the adapter reports `verified: true`, then
// emits `output`, `exited`, `terminated` and no `stopped` at all. The identical
// file with its body moved into `main` stops on the first try.
//
// `TraceRunner` used to infer this after the fact, from a `0 steps` summary.
// The debugger cannot: it gets a verified breakpoint and silence. Asking the
// source directly is what lets both say so *before* spending a process.
//
// Textual on purpose. A `#lang` line can rebind almost anything, so this is a
// heuristic, and it is the same heuristic Try Turmeric applies before running
// `(main)` after loading a file's forms.
bool DefinesMainEntry(const QByteArray& source);

// Runs `tur trace` over a single file and reports what came back.
//
// Deliberately not through ReplSession, for the same reason ProjectRunner is
// not: the REPL's pty is occupied by an interactive `tur repl`. Output is
// echoed into the same TerminalView so it lands where the user already looks.
//
// This is T1 of editor-intelligence.md and stops there: it records and explains
// and does not build a timeline. Scrubbing needs `tur dap --replay`, which is
// debugger-support.md's client.
class TraceRunner : public QObject {
    Q_OBJECT
public:
    explicit TraceRunner(TerminalView* terminal, QObject* parent = nullptr);

    bool isRunning() const;

    // Trace `filePath`, writing the recording beside it in a temp directory.
    // Returns immediately; the outcome arrives on `finished`.
    RunResult run(const QString& filePath);

    // Where the last recording was written, or empty. Kept so a future replay
    // session has something to open without re-running.
    QString lastRecordingPath() const { return recordingPath_; }

    // Parse a `trace: …` summary line. Exposed for testing the parser without
    // spawning anything.
    static TraceSummary ParseSummary(const QString& text);
    // The sentence the UI shows for an outcome. One place, so the status bar,
    // the terminal banner and the control API cannot disagree.
    static QString ExplanationFor(TraceOutcome outcome, const TraceSummary& summary);

signals:
    void finished(TraceOutcome outcome, TraceSummary summary, const QString& explanation);

private:
    void onReadyRead();
    void onFinished();

    TerminalView* terminal_ = nullptr;
    QProcess* proc_ = nullptr;
    QString recordingPath_;
    // Everything the child wrote, accumulated so the summary can be parsed and
    // an error line spotted after the fact.
    QString transcript_;
};

}
